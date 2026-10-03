# Epic Online Services integration

## Status

EOS integration is planned, not implemented. The current Battle.net button
opens the LAN browser. No EOS product, deployment, client policy or credentials
are committed to this repository. Account setup, SDK acquisition and live
multiplayer verification must precede enabling the Internet entry point.

## Ownership and entry points

`games/warcraft-3/menu/screens/main_menu.c:MainMenu_InitFrames` currently binds
both `BattleNetButton` and `LocalAreaNetworkButton` to `menu_multiplayer`.
`games/warcraft-3/menu/menu_main.c` routes that command to
`M_ShowLanBrowserMenu`. The Internet entry point must use a separate command
and browse EOS lobbies; the LAN command must continue to broadcast UDP queries.
An unavailable or unconfigured Internet service must report its actual status
in the menu, rather than silently searching LAN servers.

The game uses a Quake 2 server-authoritative simulation, not Doom lock-step.
Keep the existing `clc_*` / `svc_*` packets and versioned handshake.
EOS supplies lobby discovery and a transport beneath `NET_SendPacket` /
`NET_GetPacket`; it does not move the simulation into the client.

The common transport owns EOS initialization, ticking, Connect login,
P2P send/receive, notification handles and shutdown. The universal client
exposes generic discovery and connection operations to the menu module.
WC3 menu policy, retail frame selection, map selection and lobby presentation
remain in `games/warcraft-3/menu/`. Existing `server/sv_lobby.c` remains the
authority for slots, teams, settings and launch.

## Required lifecycle

1. Entering Internet games explicitly initializes the optional SDK with the
   configured product, sandbox, deployment and player-client credentials.
   Disable the Epic account overlay when using Device ID guests.
2. Create the desktop Device ID, then use EOS Connect to log in. Treat an
   existing Device ID as expected. On `EOS_InvalidUser`, call Connect
   CreateUser with its continuance token. Handle auth expiration and login
   status notifications; do not repeatedly create guest users.
3. Search public lobbies by engine protocol, game module, edition and build
   compatibility. Keep incompatible games out of the joinable list. Map names
   and lobby display names are untrusted data. Joining requires the same local
   map content; EOS discovery does not transfer maps or retail archives.
4. Create a public lobby after local map/slot validation succeeds. Publish
   compatibility, map identity, owner, available slots and joinability.
   Disable host migration: this engine has no authoritative host-state transfer.
   Close admission when launching a map.
5. Join asynchronously and connect to the lobby owner's Product User ID using
   the existing versioned engine handshake. Accept P2P requests and engine
   traffic only from current lobby members. Validate both socket identity and
   sender identity; LAN discovery replies must not populate the Internet list.
6. Maintain distinct EOS receive channels for the listen-server and local-client
   sides. Keep the host's own connection on loopback. Add typed EOS addresses
   and compare their complete Product User IDs in `SV_FindClientByAddr`;
   comparing four IP bytes would merge distinct EOS peers.
7. Fragment and reassemble engine messages within the installed SDK's P2P
   packet limit. Current remote signon pages are 1400 bytes and gameplay
   messages can be much larger. Do not assume one engine packet fits one EOS
   packet. Bound each peer's memory, message length, fragment count, queue
   lifetime and work per tick. Preserve packet boundaries and ordering.
8. Explicitly leave/destroy lobbies, close peer connections and release all
   search/details/notification handles on cancel, disconnect or server shutdown.
   Keep the lobby-to-map transition alive. Handle late callbacks after cancel
   using operation identity so they cannot resurrect a departed lobby.
9. If the owner leaves or crashes, clients disconnect through their ordinary
   timeout path and the room ceases to be joinable. Measure relay and service
   cleanup in a two-machine test; do not promise an unverified cleanup deadline.

## Credentials and distribution

The repository's top-level `LICENSE` is MIT. This does not grant rights to the
proprietary EOS SDK. Keep SDK headers, binaries and downloaded archives out of
the public source tree and follow Epic's redistribution terms and bundled
third-party notices. Official EOS-enabled distributions need the required
Epic Materials disclaimer in their end-user terms.

Use a custom, **user-required player client policy** granting only the features
used by this integration, including lobbies and P2P. Never ship credentials for
a trusted-server policy, the developer account, or administrative automation.
Guest Device ID login uses EOS Connect and does not require an Epic account
for each player. It is an install-local identity, not a portable account.

Product/sandbox/deployment IDs select a backend environment. A client ID and
client secret authenticate the SDK client under its policy. GitHub Actions
Secrets can supply release-build configuration, but once credentials are
embedded in a binary or bundled configuration they are extractable. The policy
must therefore remain safe when the player-client credential is known.

Release provenance is not an authentication boundary. A source build with the
same authorized configuration, SDK and compatible protocol can access the
same deployment. Epic's Services Agreement section 3.3.2 limits intentional
credential sharing to Licensed EOS Developers; do not publish credentials in
GitHub or assume an unrestricted public development key is permitted. A
contributor can build LAN/offline mode without EOS credentials, or configure
their own registered EOS product for integration development. Separate
development and production deployments; forks using their own deployment do
not automatically share the official lobby directory.

## Verification before enabling Internet play

- Build the ordinary SDK-free targets and run `make test`.
- Build EOS-enabled targets against the actual downloaded C SDK on every
  shipped platform; do not substitute guessed ABI declarations or mocks for
  this check. Verify packaging resolves the matching SDK shared library.
- Regress typed-address comparison, oversized and interleaved packet
  reassembly, malformed inputs, foreign peers, cancel/late-callback behavior,
  handshake protocol rejection and separate LAN/Internet menu commands.
- With two guest installations, test public search, create/join, full and
  incompatible rooms, slot changes, map launch, reconnect, graceful host leave,
  host process termination and relay-only connectivity.
- Record host-crash cleanup latency, connect errors and bounded queue behavior.
  Unit tests cannot establish that Epic's actual deployment policy or relay
  configuration permits these operations.

## References

Reviewed on 2026-10-03:

- [EOS Services Agreement](https://onlineservices.epicgames.com/services/terms/agreements),
  sections 3.1, 3.3 and 4.1, for SDK distribution and credential sharing.
- [Client policy guide](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/client-and-client-policy/client-policy-guide),
  for user-required policies and the trusted-server credential warning.
- [Connect reference](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/connect-interface/connect-reference),
  for Device ID and guest authentication.
- [C SDK setup](https://onlineservices.epicgames.com/sdk), for supported SDK
  downloads and product configuration.
- [Network architecture](network.md), for packet sizes, signon, transport and
  existing lobby lifecycle.
- [WC3 UI flow](../games/warcraft-3/architecture/ui-flow.md), for retail glue
  screen ownership and transitions.
