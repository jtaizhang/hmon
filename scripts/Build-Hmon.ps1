[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$StudioPath,
    [ValidateSet('arm64', 'x86_64')][string]$Architecture = 'arm64',
    [switch]$WithGpuProbe
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$version = (Get-Content -LiteralPath (Join-Path $root 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid VERSION' }
$native = Join-Path $StudioPath 'sdk/default/openharmony/native'
$compiler = Join-Path $native 'llvm/bin/clang++.exe'
$strip = Join-Path $native 'llvm/bin/llvm-strip.exe'
$signer = Join-Path $StudioPath 'sdk/default/openharmony/toolchains/lib/binary-sign-tool.jar'
$java = Join-Path $StudioPath 'jbr/bin/java.exe'
foreach ($tool in @($compiler, $strip, $signer, $java)) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Missing SDK tool: $tool" }
}
$dist = Join-Path $root 'dist'
$packageName = "hmon-$version-harmonyos-$Architecture"
$output = Join-Path $dist $packageName
if (Test-Path -LiteralPath $output) {
    throw "Output already exists: $output. Move it aside before rebuilding this release."
}
$temp = Join-Path $root '.cache/clang-tmp'
New-Item -ItemType Directory -Force $output, $temp | Out-Null
$target = if ($Architecture -eq 'arm64') { 'aarch64-linux-ohos' } else { 'x86_64-linux-ohos' }
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
function Build-Signed([string]$source, [string]$name, [string[]]$extra = @()) {
    $unsigned = Join-Path $temp "$name-$Architecture-unsigned"
    $signed = Join-Path $output $name
    & $compiler "--target=$target" "--sysroot=$native/sysroot" '-static-libstdc++' '-std=c++17' '-O2' '-Wall' '-Wextra' '-Werror' '-fstack-protector-strong' '-Wl,-z,relro,-z,now' $source @extra '-o' $unsigned
    if ($LASTEXITCODE -ne 0) { throw "$name compilation failed" }
    & $strip '--strip-all' $unsigned
    if ($LASTEXITCODE -ne 0) { throw "$name strip failed" }
    # Never modify ELF contents after signing.
    & $java '-jar' $signer 'sign' '-selfSign' '1' '-inFile' $unsigned '-outFile' $signed
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $signed)) { throw "$name signing failed" }
}
try {
    $env:TEMP = $temp
    $env:TMP = $temp
    Build-Signed (Join-Path $root 'src/main.cpp') 'hmon-core'
    if ($WithGpuProbe) {
        Build-Signed (Join-Path $root 'src/gpu_probe.cpp') 'hmon-gpu-probe' @('-I', (Join-Path $StudioPath 'sdk/default/hms/native/sysroot/usr/include'), '-ldl')
    }
    $utf8 = [System.Text.UTF8Encoding]::new($false)
    foreach ($item in @('hmon', 'LICENSE', 'README.md')) {
        $content = [IO.File]::ReadAllText((Join-Path $root $item)).Replace("`r`n", "`n")
        [IO.File]::WriteAllText((Join-Path $output $item), $content, $utf8)
    }
    Copy-Item -LiteralPath (Join-Path $root 'docs/USAGE.txt') -Destination $output
    Copy-Item -LiteralPath (Join-Path $root 'CHANGELOG.md') -Destination $output
    $archive = Join-Path $dist "$packageName.tar.gz"
    & tar '-czf' $archive '-C' $dist $packageName
    if ($LASTEXITCODE -ne 0) { throw 'Archive creation failed' }
    $digest = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$archive.sha256", "$digest  $packageName.tar.gz`n", $utf8)
    Write-Output "Built $archive"
    Write-Output "SHA256 $digest"
} finally {
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
}
