# Release checks

The native launcher checks GitHub once on a fresh app boot. A published newer
version shows the installed version, available version and release changelog.
Up/Down scrolls the changelog; Cross or Circle dismisses it. An up-to-date app,
offline device, invalid response or failed request produces no startup notice.
A check also remains separate from the return-to-library path after playing.

Settings -> Release checks allows disabling automatic checks, checking manually
and viewing a detected release again. Manual checks report whether the request
succeeded or failed; startup remains silent when no newer release is available.
This preference is stored in `ux0:/data/hardrpg/update-settings.json`.
Updates are downloaded and installed
manually. The checker never installs packages or writes game files.

The launcher reads the public
[GitHub releases API](https://docs.github.com/en/rest/releases/releases#list-releases)
for Asukate/mkxp-z-vita, without a token. Alpha builds consider published
prereleases as well as stable releases; stable builds ignore prereleases. Drafts,
malformed version tags and release URLs outside this repository are rejected.
Semantic version ordering prevents announcing an older or equal release. Only
the ten most recent release records are requested, with a 256 KiB response limit.

HTTPS uses certificate and hostname verification with the packaged Mozilla CA
bundle. The launcher-only libcurl 8.17.0 build uses VitaSDK Mbed TLS 3.6.5. Its
source archive hash, recipe, licenses and built archive hashes are recorded.
The worker has a three-second connection limit and six-second transfer limit.
The Vita build uses the synchronous resolver, so a blocked platform DNS lookup
can exceed that transfer limit; UI input and game launch never wait for the
worker. The worker is cancelled when the launcher exits, and game execution
starts in a separate process. Hardware HTTPS/Wi-Fi behavior remains a required
release gate.
