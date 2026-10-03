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
are not committed. Live login, two-installation multiplayer, forced relay and
host-crash cleanup require a configured client and remain separate acceptance
checks. A successful build does not establish live service access.

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
Windows and Linux build recipes have not been executed in this change.

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
Windows x64 and Flatpak releases. Set these **repository Actions secrets** before
publishing a release or dispatching the workflow:

| Secret | Value |
| --- | --- |
| `EOS_SDK_DOWNLOAD_URL` | Runner-accessible HTTPS URL for the licensed C SDK ZIP **1.19.2.1-CL58105819** |
| `OPENREALM_EOS_PRODUCT_ID` | Product ID |
| `OPENREALM_EOS_SANDBOX_ID` | Sandbox ID |
| `OPENREALM_EOS_DEPLOYMENT_ID` | Official deployment ID |
| `OPENREALM_EOS_CLIENT_ID` | Restricted player client ID |
| `OPENREALM_EOS_CLIENT_SECRET` | Restricted player client secret |

Use an authorized archive download endpoint or private artifact mirror that the
runner can access directly. A Developer Portal page or store slug is not an
archive URL. An expired signed URL must be replaced. The workflow does not log
in to Epic or store your developer account password. Secret configuration and
live deployment access have not been completed by this change.

`dist-scripts/eos/prepare_release.py` verifies this pinned SHA-256 before
extracting C headers, the requested platform runtime and third-party notices:

```text
56a3bd805df426606946d74ba662f25223ffe24159e7884a625225ba80b582fb
```

Tools and samples are excluded. Missing configuration, invalid ZIP contents,
unsafe paths, download failures or checksum mismatches fail the release job
explicitly. URLs and configuration values are omitted from downloader errors.
The helper creates ignored `data/eos/eos.cfg` with private file permissions and
the build passes `BUILD=release EOS=1 EOS_CONFIG_FILE=data/eos/eos.cfg`.
SDK upgrades require updating the version and checksum in this helper after
checking the official archive; a mutable download URL alone cannot upgrade it.

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
platforms locally. The SDK-free macOS build passed with a nonexistent SDK root
and its dependency list contains no EOS library. Eight preparation regressions
cover platform selection, checksum rejection, required files, traversal/symlink
rejection, private config, existing-file preservation and download error
redaction. A fresh optimized macOS arm64 EOS executable and test executable built
successfully; its SDK state checks passed 31 assertions in three tests, and the
full SDK-free `make test TEST_JOBS=4` passed with local UDP socket access.
Hosted release builds still require running the release
workflow with configured secrets; these checks do not establish live play.

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
