# Harmony Brew packaging

## Distribution

The source repository is `jtaizhang/hmon`; the separate tap is
`jtaizhang/homebrew-hmon`. The tap initially installs a signed arm64 release
archive, with an exact SHA-256. It is not an official core formula or bottle.

The launcher and native executable belong together in `libexec`. The public
`bin/hmon` is a symlink to that launcher. The launcher resolves symlinks before
locating `hmon-core` and retains `/usr/bin/sh` for HiShell's command proxy.
Do not replace it with a native exec wrapper: temperature collection depends
on the system shell. Do not strip the signed ELF during packaging.

Before releasing:

1. Build from this repository with `scripts/Build-Hmon.ps1`.
2. Run `hmon --self-test` on the target architecture.
3. Install the tap formula using Harmony Brew and run `brew test`.
4. Launch through the Brew prefix, collect bounded JSON, and check that
   ThermalService values remain available. Test both direct and symlink launch.
5. Upload the archive and its checksum as release assets, then update the tap
   URL and digest together. Never replace an already-published release asset
   with different bytes under the same version.

## Core repository submission

The current upstream is [Harmonybrew/homebrew-core on AtomGit](https://atomgit.com/Harmonybrew/homebrew-core).
Core requires source builds in its official arm64 `ci-runner` container;
prebuilt archives are not eligible. See the official
[contribution rules](https://atomgit.com/Harmonybrew/docs/blob/main/zh-CN/contributor/contribute-formula.md),
[PR self-check](https://atomgit.com/Harmonybrew/docs/blob/main/zh-CN/contributor/self-check-pr.md),
and [custom tap guide](https://atomgit.com/Harmonybrew/docs/blob/main/zh-CN/contributor/custom-tap.md).

For a source formula use the tagged source archive and `make`, then install the
launcher/core into `libexec` and symlink the launcher into `bin`. Let Harmony
Brew supply its own compilation environment and signing pipeline. Test core
self-tests and bounded JSON on the OHOS container (explicit `--emulator` labels
that data); additionally verify on real PC HiShell because the container cannot
prove that command-proxy temperature collection works.

An upstream submission also needs `brew style`, `brew audit`, and the official
container's `brew install -s` / `brew test` results. Do not claim these checks ran
based on Windows cross-compilation or real-device tests alone. Upstream asks
contributors to create the final PR manually with its template. Maintainers
decide whether a new project's popularity and maintenance meet core criteria.
