# Epic Online Services integration

## Status and scope

The optional EOS C SDK transport connects the retail Battle.net button to
`menu_online`. It reuses the existing custom-game browser, map selection and
server-authored lobby for player names, chat, slots, teams, races, colors,
game speed and map launch. The LAN button retains `menu_multiplayer` and UDP
broadcast discovery. SDK-free builds show an explicit Internet status and
keep creation disabled.

This change adds no wins leaderboard, achievement system, account profiles,
voice chat or social overlay. WC3's existing JASS leaderboard/multiboard HUDs
remain game state and are unrelated to EOS global leaderboards. Retail ladder
matchmaking and account/social screens have no existing engine implementation
for this adapter to connect.

Downloaded and built against C SDK **1.19.2.1-CL58105819** on macOS arm64.
The SDK lives under ignored `data/eos/SDK`; its headers, binaries and archive
are not committed to the game repository. The restricted release player client
and Live deployment are configured, with credentials stored in Actions secrets
and ignored private local configuration. A compact SDK archive is stored in a
separate private mirror. Two-installation gameplay, forced relay and host-crash
cleanup remain separate acceptance checks.

## Build and configuration

Download the C SDK through your Epic Developer Portal product settings. Unpack
its `SDK` directory outside tracked source, then build:

```sh
make EOS=1 EOS_SDK_ROOT=data/eos/SDK openwarcraft3
make EOS=1 EOS_SDK_ROOT=data/eos/SDK openwarcraft3-tests
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'online_service.*'
```

`EOS=0` is the default; `BZ_EOS` guards all SDK includes and implementation.
Normal `make build` and PR CI require no SDK headers, libraries or credentials.
Only the engine executable links EOS; game and menu
modules import the generic service API. The matching runtime library is copied
to `build/lib` on macOS/Linux and `build/bin` on Windows x64. Linux selects its
arm64 or x86_64 SDK library from the build host architecture. Cross compilation
and Windows arm64 need platform-specific packaging work.

The downloaded SDK's macOS arm64 library failed `codesign --verify` even though
its ZIP CRC matched. The build creates a copy without download metadata and
signs that derived library, preserving the original SDK. Local builds use an
ad hoc signature. Distribution builds can specify `EOS_CODESIGN_IDENTITY`,
and must sign/notarize the complete application through their normal pipeline.
Hosted Linux, Windows, macOS x64 and Flatpak release build/package recipes passed
in the validation run linked below; local development checks use macOS arm64.

The store slug is not an SDK configuration. Runtime needs five values from
Product Settings → SDK Download & Credentials / Clients:

```ini
OPENREALM_EOS_PRODUCT_ID=<product ID>
OPENREALM_EOS_SANDBOX_ID=<sandbox ID>
OPENREALM_EOS_DEPLOYMENT_ID=<deployment ID>
OPENREALM_EOS_CLIENT_ID=<restricted player client ID>
OPENREALM_EOS_CLIENT_SECRET=<restricted player client secret>
```

Save this as private `eos.cfg`, with `KEY=value` syntax and no quoting. The
engine reads the per-user game directory first, then bundled
`share/warcraft-3/eos.cfg`. `OPENREALM_EOS_CONFIG` selects an explicit file;
individual environment variables override file values. Credentials never
enter the cvar registry or normal diagnostic output. `eos.cfg` is ignored by
Git, including a bundled copy.

For an official package, supply a private configuration file explicitly:

```sh
make EOS=1 EOS_CONFIG_FILE=/private/path/eos.cfg openwarcraft3
```

Package the resulting executable, ordinary module libraries, EOS runtime and
`build/share` together. Merely storing a key in GitHub Secrets does not configure
a local build. Do not upload the private config as a public CI debug artifact.

## GitHub release builds

`.github/workflows/release.yml` downloads the C SDK for Linux x64, macOS x64,
Windows x64 and Flatpak releases from a private GitHub mirror. The repository
Actions configuration is:

| Name | Kind | Value |
| --- | --- | --- |
| `EOS_SDK_REPOSITORY` | Variable | `corepunch/open-realm-eos-sdk` (private) |
| `EOS_SDK_REF` | Variable | Pinned mirror commit `7306233848a55de428d0bb600c1effd4325f476c` |
| `EOS_SDK_DEPLOY_KEY` | Secret | SSH private key whose public key is a read-only deploy key on the mirror |
| `OPENREALM_EOS_PRODUCT_ID` | Secret | Product ID |
| `OPENREALM_EOS_SANDBOX_ID` | Secret | Sandbox ID |
| `OPENREALM_EOS_DEPLOYMENT_ID` | Secret | Official Live deployment ID |
| `OPENREALM_EOS_CLIENT_ID` | Secret | `OpenRealm release player` client ID |
| `OPENREALM_EOS_CLIENT_SECRET` | Secret | Restricted player client secret |

Epic's portal issues signed archive URLs containing `Expires`, `Key-Pair-Id` and
`Signature`. The recorded download URL had expired and returned HTTP 403 when
tested without browser authentication. It is unsuitable as a durable CI input.
The mirror instead contains a 37,438,691-byte SDK-only ZIP derived from the
checksum-verified official archive: C headers, the three desktop runtimes and
third-party notices. Its README records original/derived checksums and licensing.
Keep the mirror private. It contains no player credentials.

The deploy key grants read access only to that SDK repository. It cannot write
the mirror or access the rest of the GitHub account. `actions/checkout` pins the
mirror commit, and `persist-credentials: false` removes its authentication after
checkout. No Epic developer password or account-wide GitHub token is stored in
Actions. The five player credentials are separate secrets in `open-realm`.

`dist-scripts/eos/prepare_release.py` accepts only these pinned archive hashes:

```text
# Official C SDK 1.19.2.1-CL58105819
56a3bd805df426606946d74ba662f25223ffe24159e7884a625225ba80b582fb
# Compact archive EOS-SDK-1.19.2.1-CL58105819-runtime.zip in the private mirror
7fdb37c88c9dbb0dc321306cd1475e94f542d623259f7099dc85ebd6f29bb342
```

Tools and samples are excluded. Missing configuration, invalid ZIP contents,
unsafe paths, checkout failures or checksum mismatches fail the release job
explicitly. URLs and configuration values are omitted from downloader errors.
The helper creates ignored `data/eos/eos.cfg` with private file permissions and
the build passes `BUILD=release EOS=1 EOS_CONFIG_FILE=data/eos/eos.cfg`.
The workflow passes `--archive` for the checked-out compact ZIP. The helper also
supports an HTTPS download via `EOS_SDK_DOWNLOAD_URL` for manual preparation;
that variable is not required by release CI.

SDK upgrades require validating the official ZIP, creating the compact archive,
updating the helper's version/checksums, publishing a new private mirror commit
and updating `EOS_SDK_REF`. Changing the mirror variable alone cannot introduce
different SDK bytes. To rotate CI access, add a new read-only mirror deploy key,
replace `EOS_SDK_DEPLOY_KEY`, verify checkout, then remove the old public key.

Unix archives preserve `bin/`, `lib/` and `share/` so the executable's runtime
lookup paths work after extraction; run `bin/openwarcraft3`. macOS x64 uses an
Intel runner and explicit `ARCH=x86_64`. Windows packages the EOS DLL beside
`openwarcraft3.exe` and includes it in dependency validation. Portable archives
include EOS notices under `licenses/EOS/`.

The ordinary Flatpak manifest defaults to `EOS=0`. The release job creates an
adjacent ignored manifest setting `EOS=1` and `EOS_CONFIG_FILE`, installs the
runtime under `/app/lib` and notices under `/app/share/licenses/EOS`, then
publishes the final bundle only. Flatpak source/build caches are disabled so
SDK files and private configuration are not saved as cache artifacts.

Published-release events upload assets normally. Manual dispatch supports
`publish=false` to build/package all platforms without uploading release assets
or Flatpak artifacts. It does not require creating a release for the supplied
tag, because upload steps are skipped:

```sh
gh workflow run release.yml --ref feature/eos-online-multiplayer \
    -f tag=eos-setup-check -f publish=false
```

Validate release preparation without any SDK or service access using
`make test-eos-release` (also included in `make test`). To check a downloaded
official archive locally, supply the five configuration variables and a fresh
output directory:

```sh
python3 dist-scripts/eos/prepare_release.py --platform macOS \
    --archive /private/path/EOS-SDK-58105819-Release-v1.19.2.1.zip \
    --output /private/path/eos-release-inputs
```

The actual official archive passed checksum/extraction checks for all three
platforms locally. The compact mirror archive passed the same three-platform
checks, and the approved deploy key fetched the pinned mirror commit over SSH.
The SDK-free macOS build passed with a nonexistent SDK root
and its dependency list contains no EOS library. Nine preparation regressions
cover platform selection, checksum rejection, required files, traversal/symlink
rejection, private config, existing-file preservation and download error
redaction. A fresh optimized macOS arm64 EOS executable and test executable built
successfully; its SDK state checks passed 31 assertions in three tests, and the
full SDK-free `make test TEST_JOBS=4` passed with local UDP socket access.
These checks do not establish two-installation gameplay or relay behavior.

The hosted [release validation run](https://github.com/corepunch/open-realm/actions/runs/37110255488)
at commit `88657b56133536f757764894ef327ac6064c00ba` completed SDK mirror checkout,
checksum verification, compilation and packaging successfully on Linux x64,
macOS x64, Windows x64 and Flatpak. `publish=false` skipped every release-asset
upload, Flatpak artifact upload and the Publish Flatpak job. The live service
probe below was local and separate from this compile/package validation.

## Live configuration check

On 2026-10-03, a bounded native C SDK probe using the configured release player
client completed desktop Device ID login, created an isolated public lobby,
read its ownership/data, found the lobby through a bucket-filtered public search
and destroyed it. All service calls returned `EOS_Success`. The setup lobby used
`OpenRealmConfigurationProbe`, separate from the engine's game/protocol/edition
bucket, and was cleaned up after the check. Public indexing was asynchronous:
the first two searches returned zero matches, and the third found the lobby.
This verifies the configured Connect and three lobby permissions, not game
packet transport, another player's join, relay behavior or crash cleanup.

A headless macOS C SDK probe must pump the native run loop while ticking EOS.
`EOS_Platform_Tick` plus `usleep` alone timed out before the Device ID callback,
with native HTTP errors reported during teardown. Adding
`CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false)` in the bounded tick loop
completed the same operations. The normal SDL client already pumps native
events. Do not diagnose an unpumped headless probe as invalid credentials or
disable TLS validation to work around its HTTP errors.

## Ownership and lifecycle

The game uses a **Quake 2 server-authoritative simulation**. Existing versioned
`clc_*` / `svc_*` packets still carry commands, lobby state and world snapshots.
EOS provides discovery and transport beneath `NET_SendPacket` / `NET_GetPacket`.
Gameplay policy remains in the game module; WC3 menu policy stays in
`games/warcraft-3/menu/`. `server/sv_lobby.c` remains authoritative for slots,
chat and launch.

`common/online_eos.c` owns SDK platform/Connect/Lobbies/P2P handles. Initialization
is lazy when entering Internet games. It creates a desktop Device ID, accepts
an existing ID, logs in through Connect, and creates the guest Product User ID
on `EOS_InvalidUser`. Expiration reauthenticates; login loss closes the room.
Players do not need Epic accounts. Device ID is an install-local guest identity,
not a portable profile. The Epic overlay, lobby invites, host migration and RTC
are disabled.

A host first creates an unadvertised lobby, then publishes map path/CRC,
name, speed and human slot capacity. Search uses game module, engine protocol
and edition as the EOS bucket. Joining requires byte-identical installed map
content. There is no map or retail archive download. Hosting rejects map paths
that exceed the browser's 80-byte map field with a diagnostic. The Start button
waits for publication; launching makes the lobby private, and the engine rejects
new EOS clients after the map transition even while the service update is pending.

`NA_EOS` contains a complete Product User ID; address comparison does not alias
peers through their empty IP bytes. The host's own client remains on loopback.
P2P socket `OpenRealm` uses channel 0 for client→server and 1 for server→client.
Send requests and receives admit only current lobby members; guests accept
only the owner as a server. A client also checks the packet source against its
connected host. The initial engine handshake retries once per second and times
out after 10 seconds, including when the host disappears before replying. LAN replies never populate the Internet list.

Engine messages can reach 256 KiB, while EOS P2P accepts 1170-byte packets.
`common/online_packet.c` adds a 12-byte little-endian message-ID/total/offset
header and fragments into reliable ordered P2P packets. Reassembly retains one
message per peer/channel, checks exact offsets and lengths, bounds allocation
to `MAX_MSGLEN`, and expires incomplete messages after 10 seconds. The SDK
queues are 8 MiB per direction; receive work is capped at 256 fragments per
engine tick per channel. Malformed, foreign and failed packets are logged.

Cancel/disconnect closes peer connections and leaves or destroys the lobby.
Operation epochs discard old search/join/create/update callbacks; pending
operations and departures stay busy until their callbacks complete, preventing
a canceled operation from reviving a room. EOS lobby closure disconnects guests;
loss of the host's service session shuts down its local server. Engine timeouts
still handle network stalls and process crashes. Final process shutdown submits
best-effort departure before releasing SDK handles; service crash-cleanup latency
has not been measured.

## Credentials and open source

The repository's MIT license covers this adapter, not the proprietary EOS SDK.
Keep SDK downloads out of Git and follow Epic's redistribution terms and
third-party notices. EOS-enabled distributions need the required Epic Materials
disclaimer in their end-user terms.

Use a **Custom, user-required player client policy** granting only:

- `lobbies:connect`: create/join.
- `lobbies:readLobby`: read lobby data.
- `lobbies:findLobbies`: public searches.

Connect Device ID login and P2P do not require enabling the portal's unrelated
Connect account-link actions. Do not enable TrustedServer or administrative
credentials, achievements, stats, leaderboards or storage for this flow.

A shipped player client secret is extractable from the package. GitHub Secrets
protect it during CI; they do not make it secret on players' machines. The
policy must remain safe when this credential is known. A compatible source
build with authorized configuration can use the same deployment; release-only
provenance is not an authentication boundary. Contributors can build LAN/offline
without EOS credentials, or register a separate product for EOS development.
Forks with separate deployments do not share the official lobby directory.
Epic's agreement restricts intentional credential sharing to Licensed EOS
Developers; do not publish the credential in GitHub.

## Validation and remaining acceptance

Regression coverage exercises maximum/sign-on packet fragmentation, interleaved
peers/channels, malformed and out-of-order fragments, incomplete-message cleanup,
full typed-address comparison, server client identity, late callback cancellation,
foreign peer admission and separate retail Internet/LAN button commands. The
ordinary required suite remains `make test`; SDK-specific state tests use the
EOS-enabled executable command above and do not contact the service.

Before calling Internet play production-ready, use two separate guest
installations to verify login, public search/create/join, full and incompatible
rooms, slot/chat changes, map launch, graceful leave, reconnect, process crashes
and relay-only connectivity. Record cleanup latency and queue failures. Unit
tests cannot validate the configured Epic deployment or NAT/relay behavior.

## References

Reviewed on 2026-10-03:

- [EOS Services Agreement](https://onlineservices.epicgames.com/services/terms/agreements),
  sections 3.1, 3.3 and 4.1, for SDK distribution and credential sharing.
- [Client policy guide](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/client-and-client-policy/client-policy-guide).
- [Connect reference](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/connect-interface/connect-reference).
- [C SDK setup](https://onlineservices.epicgames.com/sdk).
- [Network architecture](network.md).
- [WC3 UI flow](../games/warcraft-3/architecture/ui-flow.md).
