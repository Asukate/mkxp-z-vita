# Building HardRPG

The build has three stages: compile pinned dependencies into a workspace,
compile the Vita engine, then package the RGSS picker. No installed game
or prebuilt dependency prefix is required.

## Pinned container (same environment as CI)

On Linux x86_64, install Docker and clone this repository. Build the branch
you intend to test; the workflow checks out the triggering revision.

```bash
git clone https://github.com/Asukate/mkxp-z-vita.git
cd mkxp-z-vita
docker build -f ci/Dockerfile -t hardrpg-build .
mkdir -p build/real-vita
docker run --rm -e JOBS=4 \
  -v "$PWD:/source:ro" -v "$PWD/build/real-vita:/work" \
  hardrpg-build bash ci/run.sh real-vita /work
```

The Dockerfile fixes the official VitaSDK **2026.08** image by immutable
SHA-256 digest. Host tools come from Ubuntu 24.04 package repositories.
Dependency revisions and vendored inputs are locked in `deps/versions.lock`,
which the bootstrap consumes. Tool versions and input hashes are recorded;
these recipes establish source reproducibility, not byte-identical output
across future host package updates.

Outputs are under `build/real-vita/packages/`:

- `HardRPG-HARDRPG01-real-vita.vpk`
- `SHA256SUMS`
- `bootstrap-manifest.txt`, `dependencies-state.json`, `engine-state.json`

Verify the checksums from that directory with `sha256sum -c SHA256SUMS`.
Build logs are under `build/real-vita/logs/`. Docker may create root-owned
outputs; use a directory you reserve for generated build data.

For an emulator package, replace **both** occurrences of `real-vita` in the
mount and command with `vita3k`. Keep the workspaces separate: each profile
uses a different vitaGL build. A Vita3K package is not a hardware release.

## Native Linux build

Set `VITASDK` to your VitaSDK root. The pinned container is the reference
when investigating differences in a native toolchain.

Host prerequisites: C/C++ build tools, git, CMake, Meson, Ninja, pkg-config,
Python 3, Ruby with `erb`, autoconf, automake, libtool, make, wget, patch,
xxd, and zstd. On Ubuntu, install `libtool-bin` as well as `libtool`.

```bash
export VITASDK=/opt/vitasdk
./scripts/bootstrap-vita.sh --profile real-vita -j 4
./scripts/build-vita.sh --profile real-vita -j 4
./scripts/package-launcher.sh --profile real-vita
python3 scripts/verify-launcher-payload.py \
  build/real-vita/packages/HardRPG-HARDRPG01-real-vita.vpk HARDRPG01
```

The default workspace is `build/<profile>/`. Pass the same absolute `--ws DIR`
to all three scripts to build elsewhere; packaging also accepts `--out DIR`
and `--title-id ID`. The default Title ID is `HARDRPG01`.

A workspace records its dependency recipes and SDK. If those change or a
profile differs, choose a new workspace rather than reusing old archives.
The engine receipt records source inputs and installed archive hashes;
packaging rejects an ELF whose source, profile, or dependencies have changed.
Build the engine again after source edits. Existing older, unrecorded build
prefixes are intentionally rejected.

## Checks and package contents

`python3 ci/check-source.py` runs fast source syntax and launcher manifest
checks. `python3 tests/vita-release.py` uses a host C++ compiler to check
diagnostic isolation, map lifetimes, and opacity normalization. `ci/run.sh`
executes both checks followed by bootstrap, build, and packaging. GitHub Actions runs the same script for `real-vita` and `vita3k`,
with read-only repository permissions and seven-day artifact retention.
It does not publish a release or install anything on a device.

Packaging verifies the static payload SHA-256 manifest, exact picker source
round-trip through Ruby Marshal/zlib, the HardRPG SFO title, and requested
Title ID. The VPK contains the engine, picker stub, two preloads, fonts,
LiveArea assets, and license notices. Games, RTP, saves,
and Sony firmware modules are supplied separately.

Successful compilation and packaging are build checks. A new package still
needs a physical-Vita launch/game/return/save test before it is described as
hardware-accepted. See [compatibility](COMPATIBILITY.md) and
[validation results](VALIDATION.md).
