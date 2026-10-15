# Harmony Brew packaging

## Distribution

The source repository is `jtaizhang/hmon`; the separate tap is
`jtaizhang/homebrew-hmon`. The tap distributes a signed arm64 release archive
and a Harmony Brew bottle, with exact SHA-256 checksums. It is an independent
tap, not an official core formula. The bottle avoids requiring developer tools
on the user's PC.

Harmony Brew looks for shorthand tap names on AtomGit. Use the full GitHub URL:

```sh
brew tap jtaizhang/hmon https://github.com/jtaizhang/homebrew-hmon.git
brew install jtaizhang/hmon/hmon
```

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
5. Upload the archive and its checksum as release assets.
6. In the official arm64 container, install with `--build-bottle`, run
   `brew test`, then generate the bottle metadata with `brew bottle`.
   Keep the generated OHOS tag, digest and rebuild value. Test installing the
   bottle before publishing its generated formula to the tap.
7. Upload the bottle under a new release asset name and update the tap using
   the generated metadata. Never replace an already-published release asset
   with different bytes under the same version.

The container's `brew --cache --force-bottle` may select the Linux tag. When
preloading a locally generated OHOS bottle for testing, use
`brew --cache --bottle-tag=arm64_ohos jtaizhang/hmon/hmon`. This only selects the
cache location; installation must still validate the generated checksum and
pass the normal formula tests. Runtime verification must also pass on HiShell.

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

The v0.2.7 source formula passed source installation, `brew test`, `brew style`
and `brew audit --strict` in the official container:
https://github.com/jtaizhang/homebrew-hmon/actions/runs/38715567501
The tested candidate is on the tap repository's `source-build` branch. This
does not mean Harmony Brew maintainers have reviewed or accepted the formula.
