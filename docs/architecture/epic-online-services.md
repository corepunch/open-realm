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
separate private mirror. Trusted GitHub CI runs paired gameplay, forced relay
and host/guest process-crash acceptance in separate guest installations.

Internet play supports guest login, public discovery and server-authored gameplay. `online_force_relay=1` now selects EOS's forced-relay policy before the
platform is first created; connection notifications report the actual direct
or relay path. A permanent host P2P closure now leaves the guest room promptly,
without waiting for the engine's packet timeout.

## Build and configuration

Download the C SDK through your Epic Developer Portal product settings. Unpack
its `SDK` directory outside tracked source, then build:

```sh
make EOS=1 EOS_SDK_ROOT=data/eos/SDK openwarcraft3
make EOS=1 EOS_SDK_ROOT=data/eos/SDK openwarcraft3-tests
make EOS=1 EOS_SDK_ROOT=data/eos/SDK test-eos-service
```

`EOS=0` is the default; `BZ_EOS` guards all SDK includes and implementation.
Normal `make build` and the SDK-free PR CI jobs require no SDK headers, libraries
or credentials. Trusted PRs also run the EOS job described below.
`test-eos-service` requires `EOS=1` explicitly; an SDK-free invocation fails
instead of reporting success with no service tests. Release builds run these
offline SDK tests on Linux, macOS and Windows, using separate output directories
so test executables/modules are excluded from release archives.
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

The adapter registers established/closed notifications for the authenticated
user and `OpenRealm` socket. Established notifications log the actual network
type, including reconnections. Closed notifications validate local identity,
socket and active peer membership before changing state. Local close callbacks
are ignored so delayed teardown cannot invalidate a new room. A guest's
permanent host closure clears pending connection state and all reassembly
buffers; `CL_Frame` then takes its existing room-closed disconnect path. A host
losing one guest marks that connection permanently closed and clears its partial
messages. `SV_ReapZombieClients` observes `Online_ConnectionLost`, calls the
normal `SV_DropClient` lifecycle, releases the lobby slot to Open and reclaims
the client slot after the two-second zombie grace period. The host and other
clients keep running. A member waiting for its first P2P connection is not
considered lost; established/reestablished notifications clear the closed bit.
EOS handles temporary interruptions before a permanent-close notification.

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
still handle network stalls. Final process shutdown submits best-effort departure
before releasing SDK handles. The live CI driver separately measures host-crash
public-directory cleanup and guest-crash server-slot cleanup.

Departure callbacks retain any failure across the pending cleanup batch. An
`EOS_NotFound` departure is logged as an already-absent lobby, which can happen
when closure and local leave race; other failures remain errors. The live
acceptance command waits for departure callbacks and rejects failed cleanup.

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

## Validation and automated acceptance

Regression coverage exercises maximum/sign-on packet fragmentation, interleaved
peers/channels, malformed and out-of-order fragments, incomplete-message cleanup,
full typed-address comparison, server client identity, late callback cancellation,
foreign peer admission and separate retail Internet/LAN button commands. The
ordinary required suite remains `make test`; SDK-specific state tests use the
EOS-enabled executable command above and do not contact the service.

The host-connection regression first failed with seven assertions: the room,
owner, connection request and partial packet remained live after host closure.
The fix covers that cleanup, foreign/local close filtering, host survival when
a guest disappears, reconnection network type, and failed departure callbacks.
On 2026-10-03, local macOS arm64 validation passed the SDK-free full suite, the
EOS-enabled full suite and the isolated optimized release test build. The original six
offline EOS service tests passed all 66 assertions. These results establish
code/fixture behavior, not the live acceptance rows below.

### GitHub EOS checks

The `CI` workflow adds an EOS job on main/tag pushes and same-repository PRs.
It fetches the same pinned private SDK and restricted player secrets as release
builds, then runs `make EOS=1 TEST_JOBS=4 test` in the published Linux CI image.
Fork PRs retain the SDK-free jobs; they cannot access these secrets. This job
uses ordinary `pull_request`, never privileged execution of fork code.

`dist-scripts/eos/run_acceptance.py` then runs eight explicit live scenarios:
single-guest publication/reconnect, paired default-policy exchange/departure,
paired forced-relay exchange/departure, and forced-relay host-crash cleanup.
Four additional scenarios run real game clients: default-policy gameplay,
forced-relay gameplay, host crash during gameplay and guest crash during gameplay.
Host and guest run concurrently in separate disposable Docker containers with
private writable homes/native guest stores and a read-only source/build/config
mount. The adapter rejects identical Product User IDs. Rooms include the
Actions run ID, attempt and scenario to prevent concurrent-run collisions.
Both peers must exit successfully; an intentional crash-host exit alone cannot
pass without the guest verifying host loss and public-directory removal.
Paired hosts wait for publication readiness, then receive packets; the guest
proves public indexing by finding and joining the room. Hosts must not wait for
their own public search result: a guest may fill the two-member room before that
search completes, and full rooms are intentionally excluded by the browser.
The first GitHub live run on 2026-10-03 passed solo login/publication/reconnect
in 11.9 seconds but reproduced that acceptance-driver race after the guest
joined and both peers established direct connectivity.

After fixing that race, [CI run 37130087222](https://github.com/corepunch/open-realm/actions/runs/37130087222)
passed all jobs on 2026-10-03 at commit `521e49d1d`. The EOS-enabled full suite
passed 38,202 assertions in 2,286 engine tests per edition. Linux x64 containers
used distinct native Device ID guests, the same fixture map and Live deployment:

| Live adapter scenario | Verified result |
| --- | --- |
| Solo | Guest login, public publication/search, private admission, destroy and stable-identity reconnect passed |
| Default policy | Both peers established direct connections and verified 262,144 bytes in each direction; guest detected graceful host loss after 369 ms and public absence after 502 ms |
| Forced relay | Both peers established relay connections and verified 262,144 bytes in each direction; graceful host loss after 399 ms and public absence after 511 ms |
| Forced-relay host crash | Host exited without SDK teardown after the packet acknowledgement; guest detected loss after 13,288 ms and public absence after 18,587 ms, then passed cleanup |

Latency starts when the guest sends its packet acknowledgement, immediately
before host departure/crash, and includes reconnect/search for public absence.
These timings are observations from one hosted run, not guaranteed service SLAs.

Each native command has its 180-second watchdog; the runner adds a 220-second
scenario deadline and removes surviving containers on failure. Service failures
fail CI. Only console diagnostics are retained: no SDK, guest stores, private
config or credential-bearing build artifacts are uploaded by the EOS job.
For a Linux Docker host with prepared SDK/config and EOS test build:

```sh
python3 dist-scripts/eos/run_acceptance.py
# Limit a diagnostic rerun to one scenario:
python3 dist-scripts/eos/run_acceptance.py --scenario crash
```

Runner failure/timeout/peer-isolation checks are included in `make test` through
`test-eos-release`; they use no Docker or service credentials. Passing the live
adapter scenarios prove service/packet/lifecycle behavior. The additional gameplay
scenarios run `SV_StartLobby`, `SV_Map`, `SV_Frame` and `CL_Frame` with the real
WC3 module, client parser, menu callbacks and software-GL renderer.

### Bounded live adapter checks

`+online_acceptance` is an opt-in command in the EOS-enabled **test executable**,
not a registered unit test. It drives the real Connect/Lobbies/P2P adapter,
filesystem map CRC and `NET_SendPacket` / `NET_GetPacket`. It never runs through
`make test` or release CI; the separate GitHub EOS job invokes it explicitly.
Every process has a 180-second watchdog, including
synchronous native SDK calls; timeout is a failure. Use a unique room name per
run, matching game protocol/edition and the same credentials/deployment on both
installations. The default map is the generated fixture in `build/tests`.

Build fixtures and run the single-installation publication/reconnect check:

```sh
make EOS=1 test-eos-service
OPENREALM_EOS_CONFIG=/private/path/eos.cfg \
  build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +online_force_relay 1 +online_acceptance solo acceptance-unique-name
```

`solo` verifies guest login, configured relay policy, public indexing, private
admission after Start, asynchronous destruction, stable guest identity on
re-entry, a second publication and eventual public-list cleanup. Selecting the
forced-relay policy alone does **not** prove that a relay connection works.

On installation A, then promptly on installation B, run:

```sh
# A
OPENREALM_EOS_CONFIG=/private/path/eos.cfg \
  build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +online_force_relay 1 +online_acceptance host acceptance-unique-name
# B
OPENREALM_EOS_CONFIG=/private/path/eos.cfg \
  build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +online_force_relay 1 +online_acceptance guest acceptance-unique-name
```

The guest rejects the host's own Product User ID, joins the public room and
sends a deterministic `MAX_MSGLEN` (256 KiB) packet. The host checks every byte
and echoes it; the guest verifies the echo and acknowledges receipt. With
`online_force_relay=1`, both processes require the SDK's established connection
type to be relayed. The host closes admission and departs; the guest reports
host-loss detection and public-list disappearance latency, then reconnects to
search. Repeat with `online_force_relay=0` for the default direct/relay policy.

For crash cleanup, replace A's role with `crash-host` and use a fresh room name.
After receiving the acknowledgement, A deliberately uses `_Exit(0)` without
any EOS teardown. B must detect host loss and observe that the room disappears
from a successful public search before its watchdog expires. A prints the crash
action and exits without a final PASS line; B is the cleanup verifier. Capture
both outputs, latencies and any packet-queue errors.

These are adapter checks. They do not run lobby UI, game sign-on, simulation,
snapshots or input. Those still require the gameplay acceptance matrix below.
To use an installed map for the CRC check, pass
`+set online_acceptance_map 'Maps/(2)OgreMound.w3m'` before the command and use
`-data` pointing to retail data on both installations.

On macOS, EOS Device ID creation can synchronously wait for Keychain permission
when accessing an existing guest item from a newly rebuilt executable. Handle
the native dialog yourself; do not change the stored identity, Keychain access
controls or TLS settings to bypass it. A local run on 2026-10-03 was stopped
while `EOS_Connect_CreateDeviceId` was waiting inside `SecItemCopyMatching`;
it produced no live service acceptance result. The command now has a separate
timer watchdog for this native blocking case. The headless command pumps the
CoreFoundation run loop for EOS HTTP; the normal graphical client uses SDL.

### Paired gameplay acceptance

`games/warcraft-3/tests/online_gameplay.h` adds explicit `game-host`, `game-guest`,
`game-crash-host`, `game-survivor` and `game-crash-guest` roles to the same bounded
command. It uses the generated `Maps/Transport.w3m` nested MPQ, a 32x32-tile
ROC-format map with two human starts and two fixture Footmen created by JASS.
The driver completes `CL_Init`'s queued `menu_main` command with `online_mode=0`
before enabling the Internet lobby. Main-menu entry intentionally leaves EOS;
running that deferred command during a live search cancels its operation epoch.
`tools/wc3fixturegen.py` writes real W3I/W3E/pathing/placement/script files;
`make test-assets` packs them. The earlier Human02 fixture is only placeholder
bytes for CRC tests and cannot load a game. No retail archives are required.

| Scenario | Required observation |
| --- | --- |
| Default gameplay | Distinct guest identities discover/join; both clients receive authoritative slots, names, races, teams, colors and chat ownership; real map sign-on reaches active gameplay |
| Admission | An incompatible discovery bucket excludes the room; a changed cached host CRC is rejected by the production join check before restoring real metadata; a full room disappears from successful public search |
| Shared simulation | Each player selects its own Footman and issues SmartPoint/Move over the engine channel; both clients observe both moved units, their own camera-command result, matching map checksum and 100 fresh monotonic snapshots |
| Forced relay | Both actual remote engine connections report relay; the same sign-on and shared-simulation assertions pass |
| Graceful host leave | Guest takes the normal client disconnect path, pumps queued menu restoration, signs in again and observes public-room removal |
| Host process crash | Host exits without engine/SDK teardown after guest gameplay acknowledgement; guest returns to menu, reconnects and measures directory cleanup |
| Guest process crash | Guest exits after shared-simulation verification; host stays active with advancing snapshots, releases the lobby slot to Open and reclaims the server client slot |

The gameplay container sets `SDL_VIDEODRIVER=offscreen`,
`SDL_AUDIODRIVER=dummy` and `LIBGL_ALWAYS_SOFTWARE=1`. Mesa software rendering
is a dependency of the shared CI Dockerfile. The EOS job builds that image so
PR dependency changes are tested before the refreshed image is published.
Rendering still uses the real renderer; fixture art is minimal and missing
retail presentation resources are logged. This validates the runtime lifecycle
and menu callback flow, not retail artwork, visual fidelity, long matches or
cross-platform hardware/NAT combinations.

A local loopback diagnostic runs the same map, real game/client/render path,
Move/camera commands and snapshot assertions without authenticating to EOS:

```sh
make EOS=1 openwarcraft3-tests test-assets
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +online_acceptance local local-check
# Linux Docker with prepared SDK/player config:
python3 dist-scripts/eos/run_acceptance.py --scenario game-relay
python3 dist-scripts/eos/run_acceptance.py --scenario game-crash
python3 dist-scripts/eos/run_acceptance.py --scenario game-guest-crash
```

Local macOS arm64 loopback validation passed real map loading, Move/camera
command round-trips and fresh snapshots. The new lost-guest regression first
reproduced four failures: the server client remained connected, the lobby slot
remained occupied, and the slot/high-water count were never reclaimed. After
fixing transport-close observation in the normal server lifecycle, the seven
offline EOS service tests passed 81 assertions, including reassignment of the
released slot and isolation of the surviving host. Offline tests share one
native SDK initialization lifetime; reinitializing it between tests fails on
macOS even after shutdown.

Growing the fixture archive exposed `mpqtool pack` silently truncating its
argument list at 128 files. Its argument list now scales with the actual CLI
input; the fixture archive verification checks files beyond the former cutoff.

The paired lobby check also exposed `SV_LobbySayClient_f` generating generic
"Player 1/2" chat labels instead of the sanitized name already held by the
server client and shown in its slot. It now broadcasts that authoritative name;
the server command regression checks sender text and ownership on both clients.

The gameplay pump follows the normal engine loop's `SV_IsActive()` guard.
Continuing to call `SV_Frame` while waiting for asynchronous EOS departure after
`SV_Shutdown` runs the game callback against freed state. A local diagnostic
reproduced that crash and now checks menu recovery and idle frames after shutdown
without advancing the stopped server.

Keep deployment failures as failing CI results. Acceptance is bounded and
explicit; ordinary unit tests never authenticate or contact the live service.

## References

Reviewed on 2026-10-03:

- [EOS Services Agreement](https://onlineservices.epicgames.com/services/terms/agreements),
  sections 3.1, 3.3 and 4.1, for SDK distribution and credential sharing.
- [Client policy guide](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/client-and-client-policy/client-policy-guide).
- [Connect reference](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/connect-interface/connect-reference).
- [C SDK setup](https://onlineservices.epicgames.com/sdk).
- [Network architecture](network.md).
- [WC3 UI flow](../games/warcraft-3/architecture/ui-flow.md).
