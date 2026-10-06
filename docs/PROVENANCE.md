# Source provenance

HardRPG is based on `mkxp-z/mkxp-z@1f412ce`, itself derived from Ancurio's
mkxp. Original copyright headers and component licenses are retained.
The public repository starts with one source snapshot; this does not change
the authorship or licensing of its upstream code.

The native launcher and alternative RGSS picker use the same game runtime.
[VALIDATION.md](VALIDATION.md) describes build and hardware checks, while
[COMPATIBILITY.md](COMPATIBILITY.md) lists observed game routes.

`deps/versions.lock` pins project-managed public dependencies. The Vita Ruby
source tree and the SDL shim
are bundled as source under `deps/vendor/`, so a source clone has all
project-managed source inputs. `scripts/bootstrap-vita.sh` builds dependencies into
a disposable workspace prefix. The user supplies vitaSDK separately.

Hardware logs, crash dumps, benchmarks, game data, and saves are not build
inputs or part of the public source. The launcher VPK contains no games or saves.

HardRPG 0.2.1 Alpha uses Title ID `HARDRPG01` and `ux0:/data/hardrpg/`.
Games and RTP can also be selected from external directories without moving
them. No automatic save migration is performed. Shared engine code and
original license/copyright headers are retained.
