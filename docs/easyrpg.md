# EasyRPG backend

HardRPG uses one game library. RPG Maker XP, VX, VX Ace and LCF projects appear
together, use the same search and folder controls, and launch directly from
their game row. An LCF project contains `RPG_RT.ldb` and `RPG_RT.lmt`; filename
matching is case insensitive. Its badge is `2K/2K3`: EasyRPG determines the
2000/2003 engine variant from the database. No Windows executable is required.
The same detection and launch path applies to EasyRPG games using LCF data.

The runtime is EasyRPG Player **0.8.1.1**, commit
`78328fa29f465315291e161130e6682f69410370`, with liblcf **0.8.1**, commit
`92c4450a1bc1acb58bd02bbb99b57e5036919cdf`. This is the official maintained
Vita port. Development snapshots can contain additional changes; changing
the pinned stable version requires a separate compatibility check.

## Runtime lifecycle

XP/VX/VX Ace keep the existing mkxp-z selection protocol. LCF selections use a
versioned EasyRPG request in `ux0:/data/hardrpg/selection.json`. The launcher
checks that the chosen backend is packaged before preparing a game. The Vita
process then loads `app0:/easyrpg.self` with an explicit play flag. This
is an internal backend, with no additional app bubble, library or mode screen.
EasyRPG-enabled Vita packages retain upstream's `ATTRIBUTE2=12` memory flags
for the backend's 330 MiB newlib heap; payload verification rejects missing flags.

The EasyRPG entry point validates the bounded request, consumes it once, and
passes project, save and optional RTP paths to Player. Spaces, Unicode and `&`
in paths remain literal. Normal exit and fatal-error exit load HardRPG's
`eboot.bin` and return to the library. Fatal messages enter HardRPG's existing
error history. The EasyRPG splash logos and touch-overlay artwork are excluded
from this build; rendering, controls, audio and game execution use its Vita
backend.

## Games, saves and RTP

Game folders can live under `ux0:/data/hardrpg/games` or in an existing external
search folder. Wrapped ZIPs use the normal safe extraction cache. Collections
remain browsable, so different engines can coexist in the same collection.

ZIP preparation keeps the original archive and one reusable extracted copy
under `ux0:/data/hardrpg/cache/zip-<game-key>`. Game information shows that
location. Once unpacked, the ZIP can be deleted; the game stays listed.
Unchanged, complete copies are reused on subsequent launches. Progress names
the game ZIP, 2000 RTP ZIP and 2003 RTP ZIP separately; these are separate
preparation tasks, not automatic retries. Circle cancels preparation, including
the folder enumeration stage, and incomplete staging files are removed.

EasyRPG writes saves and its configuration under
`ux0:/data/hardrpg/saves/easyrpg/<game-key>`. The key uses the existing game
identity, including the inner project path for ZIPs. Existing `Save*.lsd` slots
are copied from the project when the corresponding HardRPG slot is absent.
Original files and newer HardRPG slots are retained. ZIP cache cleanup therefore
does not delete EasyRPG saves. Moving a game changes its identity; back up and
transfer its save directory when moving it.

RTP is optional for self-contained games. Settings offers 2000 and 2003 RTP
locations alongside XP/VX/Ace. Automatic locations are `hardrpg/rtp/2000` and
`hardrpg/rtp/2003`. An LCF RTP has `System` and `Music` directories. Folder RTPs
are passed directly; ZIP RTPs use a validated extraction cache. RTP is supplied
by the user and is never included in a HardRPG package.
RTP locations are checked in a background worker only while that submenu is
open. Ordinary game settings and renaming do not scan RTPs. Completed status
results are retained until the location changes or Triangle requests a refresh.

## Building and packaging

Prepare a separate Vita dependency prefix using the official
[EasyRPG Vita build scripts](https://github.com/EasyRPG/buildscripts/tree/master/vita).
The prefix needs ICU (including encoding data), inih, fmt, pixman, PNG/zlib,
FreeType/HarfBuzz, vita2d, and the desired audio codecs. Keep it separate from
the mkxp-z prefix. ICU is required for the supported non-Western encodings.
The build script compiles the pinned liblcf into this prefix.

```sh
VITASDK=/path/to/vitasdk scripts/build-easyrpg-vita.sh \
  --ws build/easyrpg --prefix /path/to/easyrpg-vita-prefix -j 4
VITASDK=/path/to/vitasdk scripts/build-vita.sh --native-launcher --ws build/real-vita
VITASDK=/path/to/vitasdk scripts/package-launcher.sh --ws build/real-vita \
  --easyrpg-ws build/easyrpg
```

The runtime manifest records upstream commits, integration inputs, dependency
archive hashes, executable hash and license payload hashes. Packaging verifies
that inputs still match. A package without the EasyRPG runtime can browse LCF
projects but reports the missing runtime when one is selected.

## Distribution terms

EasyRPG Player and the HardRPG EasyRPG entry point use GPL-3.0-or-later; liblcf
uses MIT. EasyRPG runs as a separate executable, so its license does not require
relicensing every independent component of the package. Preserve the existing
mkxp-z and dependency terms. GPL-2.0-or-later code may also be used under GPLv3
if code is combined in a future executable.

The backend includes bitmap font data: ttyp0 (ttyp0 license), Shinonome (public
domain), Baekmuk (Baekmuk license) and WenQuanYi (GPL-2.0-or-later with font
exception). FmMidi uses BSD-3-Clause; dr_wav uses public-domain/MIT-0 terms.
Compiled optional codecs and all dependency libraries retain their own terms.
In particular, linked LGPL libraries require their corresponding source and
the applicable relinking provisions. See the exact headers and dependency
source trees rather than inferring a license from a library name alone.

Binary distribution must provide matching Player/liblcf source, this entry
point, the dated upstream modification patch, build scripts, dependency source
and modifications, and required notices. A manifest or a dependency binary
download is not corresponding source. Audit the complete linked dependency
set before publishing a VPK. EasyRPG's CC-BY-SA logo assets are not incorporated
into the executable or HardRPG's artwork. No game data or proprietary RTP is
part of the source or binary distribution.

## Acceptance

Host tests verify the shared list, backend choice, folder/ZIP paths, save
identity/import, RTP routing, request validation and retained RGSS behavior.
Compilation and package verification do not establish game compatibility.
Each backend needs a Vita route covering launch, map movement/events, battle
where applicable, audio, controls, save/relaunch/load, exit-to-library and
subsequent launch of an RGSS game. Keep actual results and retrieved evidence
separate from these source-level checks.

On Vita, explicit RTP paths supplied by HardRPG are searched first. The loaded game determines
whether the RM2000 or RM2003 custom root is used. The backend then searches
`ux0:/data/hardrpg/rtp/<2000|2003>` and
`ux0:/data/easyrpg-player/rtp/<2000|2003>` in that order. Existing EasyRPG RTP
installations can stay in place. The `ux0:` mount prefix is preserved when
reading custom paths.
