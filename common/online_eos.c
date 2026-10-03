/* Optional EOS implementation. The SDK is supplied separately by its licensee. */
#include "common.h"
#include "online.h"
#ifdef BZ_EOS
#include "online_packet.h"
#include <stdlib.h>
#include <zlib.h>
#include <eos_sdk.h>
#include <eos_lobby.h>
#include <eos_p2p.h>

#define ONLINE_MAX_GAMES 64
#define ONLINE_MAX_MEMBERS 32
#define ONLINE_ASSEMBLY_TIMEOUT 10000
#define ONLINE_RECEIVE_BUDGET 256
_Static_assert(ONLINE_FRAGMENT_SIZE <= EOS_P2P_MAX_PACKET_SIZE, "EOS packet limit changed");

typedef struct {
    onlineGame_t game;
    EOS_HLobbyDetails details;
    uint32_t crc;
} onlineResult_t;

typedef struct {
    EOS_ProductUserId user;
    EOS_ENetworkConnectionType network_type;
    bool closed;
    onlineAssembly_t assembly[2];
} onlinePeer_t;

static struct {
    EOS_HPlatform platform;
    EOS_HConnect connect;
    EOS_HLobby lobbies;
    EOS_HP2P p2p;
    EOS_HLobbySearch search;
    EOS_ProductUserId user, owner;
    EOS_NotificationId request_notify, established_notify, closed_notify;
    EOS_NotificationId member_notify, update_notify, expiration_notify, login_notify;
    EOS_P2P_SocketId socket;
    onlineResult_t games[ONLINE_MAX_GAMES];
    uint32_t num_games, epoch, message_id;
    onlinePeer_t peers[ONLINE_MAX_MEMBERS];
    uint32_t num_peers;
    char status[256], lobby[128], bucket[64];
    onlineGame_t hosted;
    uint32_t map_crc;
    bool initialized, authenticating, ready, enabled, hosting, search_pending, host_dirty;
    bool operation_pending, updating, admission_closed, connecting, published, departure_failed;
    uint32_t leave_pending, receive_budget[2];
    uint32_t update_retry;
} online;

static void Online_Login(void);
static void Online_UpdateHost(void);

static bool Online_Result(cstring_t operation, EOS_EResult result) {
    if (result == EOS_Success) return true;
    snprintf(online.status, sizeof(online.status), "%s: %s", operation, EOS_EResult_ToString(result));
    fprintf(stderr, "EOS %s\n", online.status);
    return false;
}

static bool Online_Current(void *context) { return (uintptr_t)context == online.epoch; }
static void *Online_Context(void) { return (void *)(uintptr_t)online.epoch; }

static void Online_ClearGames(void) {
    for (uint32_t i = 0; i < online.num_games; i++)
        EOS_LobbyDetails_Release(online.games[i].details);
    memset(online.games, 0, sizeof(online.games));
    online.num_games = 0;
}

static void Online_ClearPeers(void) {
    for (uint32_t i = 0; i < online.num_peers; i++)
        for (uint32_t j = 0; j < 2; j++) Online_ClearAssembly(&online.peers[i].assembly[j]);
    memset(online.peers, 0, sizeof(online.peers));
    online.num_peers = 0;
}

static EOS_HLobbyDetails Online_Details(void) {
    EOS_HLobbyDetails details = NULL;
    EOS_Lobby_CopyLobbyDetailsHandleOptions options = {
        .ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST,
        .LobbyId = online.lobby, .LocalUserId = online.user
    };
    if (!online.lobby[0] || !Online_Result("read lobby", EOS_Lobby_CopyLobbyDetailsHandle(online.lobbies, &options, &details)))
        return NULL;
    return details;
}

static void Online_UpdateMembers(void) {
    EOS_HLobbyDetails details = Online_Details();
    if (!details) return;
    EOS_LobbyDetails_GetMemberCountOptions count = { .ApiVersion = EOS_LOBBYDETAILS_GETMEMBERCOUNT_API_LATEST };
    uint32_t members = EOS_LobbyDetails_GetMemberCount(details, &count);
    EOS_ProductUserId users[ONLINE_MAX_MEMBERS];
    if (members > ONLINE_MAX_MEMBERS) {
        fprintf(stderr, "EOS lobby exceeds peer capacity: %u\n", members);
        EOS_LobbyDetails_Release(details); Online_Leave(); return;
    }
    for (uint32_t i = 0; i < members; i++) {
        EOS_LobbyDetails_GetMemberByIndexOptions item = {
            .ApiVersion = EOS_LOBBYDETAILS_GETMEMBERBYINDEX_API_LATEST, .MemberIndex = i
        };
        users[i] = EOS_LobbyDetails_GetMemberByIndex(details, &item);
    }
    EOS_LobbyDetails_Release(details);
    /* Preserve partial messages only for identities that remain in this lobby. */
    for (uint32_t i = 0; i < online.num_peers;) {
        bool found = false;
        for (uint32_t j = 0; j < members; j++) if (users[j] == online.peers[i].user) found = true;
        if (found) { i++; continue; }
        EOS_P2P_CloseConnectionOptions close = {
            .ApiVersion = EOS_P2P_CLOSECONNECTION_API_LATEST, .LocalUserId = online.user,
            .RemoteUserId = online.peers[i].user, .SocketId = &online.socket
        };
        Online_Result("close departed peer", EOS_P2P_CloseConnection(online.p2p, &close));
        for (int j = 0; j < 2; j++) Online_ClearAssembly(&online.peers[i].assembly[j]);
        online.peers[i] = online.peers[--online.num_peers];
        memset(&online.peers[online.num_peers], 0, sizeof(online.peers[0]));
    }
    for (uint32_t i = 0; i < members; i++) {
        if (!users[i] || users[i] == online.user) continue;
        bool found = false;
        for (uint32_t j = 0; j < online.num_peers; j++) if (users[i] == online.peers[j].user) found = true;
        if (!found) online.peers[online.num_peers++].user = users[i];
    }
}

static onlinePeer_t *Online_Peer(EOS_ProductUserId user, NETSOURCE source) {
    if (!online.lobby[0] || !user || user == online.user) return NULL;
    if (source == NS_SERVER && !online.hosting) return NULL;
    if (source == NS_CLIENT && (online.hosting || user != online.owner)) return NULL;
    for (uint32_t i = 0; i < online.num_peers; i++) if (online.peers[i].user == user) return &online.peers[i];
    return NULL;
}

/* A permanent host loss must clear the guest room even before engine sign-on. */
static void EOS_CALL Online_ConnectionClosed(EOS_P2P_OnRemoteConnectionClosedInfo const *info) {
    if (info->LocalUserId != online.user || !info->SocketId ||
        strcmp(info->SocketId->SocketName, online.socket.SocketName) ||
        info->Reason == EOS_CCR_ClosedByLocalUser) return;
    onlinePeer_t *peer = Online_Peer(info->RemoteUserId, online.hosting ? NS_SERVER : NS_CLIENT);
    if (!peer) return;
    fprintf(stderr, "EOS peer connection closed (reason %d)\n", info->Reason);
    for (int i = 0; i < 2; i++) Online_ClearAssembly(&peer->assembly[i]);
    peer->network_type = EOS_NCT_NoConnection;
    peer->closed = true;
    if (!online.hosting) {
        Online_Leave();
        snprintf(online.status, sizeof(online.status), "Connection to the Internet host has closed.");
    }
}

/* Keep the actual path observable so forcing relays can be verified, not assumed. */
static void EOS_CALL Online_ConnectionEstablished(EOS_P2P_OnPeerConnectionEstablishedInfo const *info) {
    if (info->LocalUserId != online.user || !info->SocketId ||
        strcmp(info->SocketId->SocketName, online.socket.SocketName)) return;
    onlinePeer_t *peer = Online_Peer(info->RemoteUserId, online.hosting ? NS_SERVER : NS_CLIENT);
    if (!peer) return;
    peer->network_type = info->NetworkType;
    peer->closed = false;
    fprintf(stderr, "EOS peer connection %s: %s\n",
        info->ConnectionType == EOS_CET_Reconnection ? "reestablished" : "established",
        info->NetworkType == EOS_NCT_RelayedConnection ? "relay" :
        info->NetworkType == EOS_NCT_DirectConnection ? "direct" : "unknown network type");
}

static void EOS_CALL Online_Request(EOS_P2P_OnIncomingConnectionRequestInfo const *info) {
    if (strcmp(info->SocketId->SocketName, online.socket.SocketName)) return;
    Online_UpdateMembers();
    if (!Online_Peer(info->RemoteUserId, online.hosting ? NS_SERVER : NS_CLIENT)) {
        fprintf(stderr, "EOS rejected a P2P request from outside the active lobby\n"); return;
    }
    EOS_P2P_AcceptConnectionOptions options = {
        .ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST, .LocalUserId = online.user,
        .RemoteUserId = info->RemoteUserId, .SocketId = &online.socket
    };
    Online_Result("accept peer", EOS_P2P_AcceptConnection(online.p2p, &options));
}

/* Successful late departures must not erase an earlier cleanup failure. */
static void Online_Departed(cstring_t operation, EOS_EResult result) {
    if (online.leave_pending) online.leave_pending--;
    if (result == EOS_NotFound) {
        /* Closure notifications can race our leave request. Absence completes cleanup. */
        fprintf(stderr, "EOS %s: lobby already absent\n", operation);
    } else if (!Online_Result(operation, result)) online.departure_failed = true;
}

static void EOS_CALL Online_Left(EOS_Lobby_LeaveLobbyCallbackInfo const *info) {
    Online_Departed("leave lobby", info->ResultCode);
}

static void EOS_CALL Online_Destroyed(EOS_Lobby_DestroyLobbyCallbackInfo const *info) {
    Online_Departed("destroy lobby", info->ResultCode);
}

static void Online_Depart(cstring_t lobby, bool host, void *context) {
    if (!online.leave_pending) online.departure_failed = false;
    online.leave_pending++;
    if (host) {
        EOS_Lobby_DestroyLobbyOptions options = {
            .ApiVersion = EOS_LOBBY_DESTROYLOBBY_API_LATEST, .LocalUserId = online.user, .LobbyId = lobby
        };
        EOS_Lobby_DestroyLobby(online.lobbies, &options, context, Online_Destroyed);
    } else {
        EOS_Lobby_LeaveLobbyOptions options = {
            .ApiVersion = EOS_LOBBY_LEAVELOBBY_API_LATEST, .LocalUserId = online.user, .LobbyId = lobby
        };
        EOS_Lobby_LeaveLobby(online.lobbies, &options, context, Online_Left);
    }
}

void Online_Leave(void) {
    online.epoch++;
    if (online.platform && online.user) {
        EOS_P2P_CloseConnectionsOptions close = {
            .ApiVersion = EOS_P2P_CLOSECONNECTIONS_API_LATEST, .LocalUserId = online.user, .SocketId = &online.socket
        };
        Online_Result("close lobby connections", EOS_P2P_CloseConnections(online.p2p, &close));
        if (online.lobby[0]) {
            Online_Depart(online.lobby, online.hosting, Online_Context());
        }
    }
    online.lobby[0] = 0; online.owner = NULL;
    online.hosting = online.connecting = online.enabled = false;
    /* Outstanding callbacks retain their busy flags until cancellation cleanup completes. */
    online.search_pending = online.host_dirty = online.published = false;
    Online_ClearPeers(); Online_ClearGames();
    snprintf(online.status, sizeof(online.status), "Disconnected from Internet games.");
}

static void EOS_CALL Online_MemberChanged(EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo const *info) {
    if (!online.lobby[0] || strcmp(info->LobbyId, online.lobby)) return;
    if (info->CurrentStatus == EOS_LMS_CLOSED ||
        (info->TargetUserId == online.user && info->CurrentStatus != EOS_LMS_JOINED) ||
        (info->TargetUserId == online.owner && info->CurrentStatus != EOS_LMS_JOINED)) {
        Online_Leave();
        snprintf(online.status, sizeof(online.status), "The Internet game has closed.");
        return;
    }
    Online_UpdateMembers();
}

static void EOS_CALL Online_LobbyChanged(EOS_Lobby_LobbyUpdateReceivedCallbackInfo const *info) {
    if (online.lobby[0] && !strcmp(info->LobbyId, online.lobby)) Online_UpdateMembers();
}

static void EOS_CALL Online_Expired(EOS_Connect_AuthExpirationCallbackInfo const *info) { (void)info; Online_Login(); }
static void EOS_CALL Online_LoginStatus(EOS_Connect_LoginStatusChangedCallbackInfo const *info) {
    if (info->CurrentStatus == EOS_LS_LoggedIn) return;
    online.ready = false; Online_Leave();
    snprintf(online.status, sizeof(online.status), "EOS guest login expired. Reconnect to Internet games.");
}

static void Online_LoggedIn(EOS_ProductUserId user) {
    online.authenticating = false; online.ready = true; online.user = user;
    if (online.established_notify == EOS_INVALID_NOTIFICATIONID) {
        EOS_P2P_AddNotifyPeerConnectionEstablishedOptions established = {
            .ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST,
            .LocalUserId = user, .SocketId = &online.socket
        };
        online.established_notify = EOS_P2P_AddNotifyPeerConnectionEstablished(online.p2p, &established, NULL, Online_ConnectionEstablished);
    }
    if (online.closed_notify == EOS_INVALID_NOTIFICATIONID) {
        EOS_P2P_AddNotifyPeerConnectionClosedOptions closed = {
            .ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST,
            .LocalUserId = user, .SocketId = &online.socket
        };
        online.closed_notify = EOS_P2P_AddNotifyPeerConnectionClosed(online.p2p, &closed, NULL, Online_ConnectionClosed);
    }
    if (online.request_notify == EOS_INVALID_NOTIFICATIONID) {
        EOS_P2P_AddNotifyPeerConnectionRequestOptions request = {
            .ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST,
            .LocalUserId = user, .SocketId = &online.socket
        };
        online.request_notify = EOS_P2P_AddNotifyPeerConnectionRequest(online.p2p, &request, NULL, Online_Request);
    }
    if (!online.request_notify || !online.established_notify || !online.closed_notify) {
        online.ready = false;
        snprintf(online.status, sizeof(online.status), "EOS connection notification registration failed.");
        fprintf(stderr, "%s\n", online.status); return;
    }
    snprintf(online.status, sizeof(online.status), "Connected to Internet games.");
}

static void EOS_CALL Online_UserCreated(EOS_Connect_CreateUserCallbackInfo const *info) {
    if (!Online_Result("create guest", info->ResultCode)) { online.authenticating = false; return; }
    Online_LoggedIn(info->LocalUserId);
}
static void EOS_CALL Online_LoginComplete(EOS_Connect_LoginCallbackInfo const *info) {
    if (info->ResultCode == EOS_InvalidUser) {
        EOS_Connect_CreateUserOptions options = {
            .ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST, .ContinuanceToken = info->ContinuanceToken
        };
        EOS_Connect_CreateUser(online.connect, &options, NULL, Online_UserCreated); return;
    }
    if (!Online_Result("guest login", info->ResultCode)) { online.authenticating = false; return; }
    Online_LoggedIn(info->LocalUserId);
}
static void Online_Login(void) {
    if (online.authenticating) return;
    online.authenticating = true;
    EOS_Connect_Credentials credentials = {
        .ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST, .Type = EOS_ECT_DEVICEID_ACCESS_TOKEN
    };
    char name[EOS_CONNECT_USERLOGININFO_DISPLAYNAME_MAX_LENGTH + 1];
    snprintf(name, sizeof(name), "%s", Cvar_String("name", "Player"));
    if (!name[0]) snprintf(name, sizeof(name), "Player");
    EOS_Connect_UserLoginInfo user = {
        .ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST, .DisplayName = name
    };
    EOS_Connect_LoginOptions options = {
        .ApiVersion = EOS_CONNECT_LOGIN_API_LATEST, .Credentials = &credentials, .UserLoginInfo = &user
    };
    EOS_Connect_Login(online.connect, &options, NULL, Online_LoginComplete);
}
static void EOS_CALL Online_DeviceCreated(EOS_Connect_CreateDeviceIdCallbackInfo const *info) {
    online.authenticating = false;
    if (info->ResultCode != EOS_DuplicateNotAllowed && !Online_Result("create Device ID", info->ResultCode)) return;
    Online_Login();
}

/* Config never enters the cvar registry, command-line diagnostics or Git. */
static bool Online_ReadConfig(char values[5][129]) {
    static char const *keys[] = { "OPENREALM_EOS_PRODUCT_ID", "OPENREALM_EOS_SANDBOX_ID",
        "OPENREALM_EOS_DEPLOYMENT_ID", "OPENREALM_EOS_CLIENT_ID", "OPENREALM_EOS_CLIENT_SECRET" };
    PATHSTR path; char line[512];
    FS_UserPath("eos.cfg", path, sizeof(path));
    cstring_t override = getenv("OPENREALM_EOS_CONFIG");
    FILE *file = fopen(override && *override ? override : path, "r");
    if (!file && (!override || !*override)) {
        snprintf(path, sizeof(path), "%s/%s/eos.cfg", FS_BasePath(), BZ_GAME);
        file = fopen(path, "r");
    }
    if (file) {
        while (fgets(line, sizeof(line), file)) {
            char *equals = strchr(line, '='); if (!equals || line[0] == '#') continue;
            *equals++ = 0; equals[strcspn(equals, "\r\n")] = 0;
            for (int i = 0; i < 5; i++) if (!strcmp(line, keys[i])) {
                if (strlen(equals) >= 129) {
                    snprintf(online.status, sizeof(online.status), "EOS configuration value for %s is too long.", keys[i]);
                    fprintf(stderr, "%s\n", online.status); fclose(file); return false;
                }
                snprintf(values[i], 129, "%s", equals);
            }
        }
        fclose(file);
    }
    for (int i = 0; i < 5; i++) {
        cstring_t env = getenv(keys[i]);
        if (env && *env) {
            if (strlen(env) >= 129) {
                snprintf(online.status, sizeof(online.status), "EOS environment value for %s is too long.", keys[i]);
                fprintf(stderr, "%s\n", online.status); return false;
            }
            snprintf(values[i], 129, "%s", env);
        }
        if (!values[i][0]) {
            snprintf(online.status, sizeof(online.status), "Internet service configuration is incomplete.");
            fprintf(stderr, "EOS setup is missing %s\n", keys[i]); return false;
        }
    }
    return true;
}

bool Online_Begin(void) {
    online.enabled = true;
    if (online.platform) {
        if (!online.ready && !online.authenticating) Online_Login();
        return true;
    }
    char config[5][129] = {{0}};
    if (!Online_ReadConfig(config)) return false;
    EOS_InitializeOptions init = { .ApiVersion = EOS_INITIALIZE_API_LATEST,
        .ProductName = "OpenRealm", .ProductVersion = BZ_XSTR(BZ_PROTOCOL_VERSION) };
    if (!Online_Result("initialize", EOS_Initialize(&init))) return false;
    online.initialized = true;
    EOS_Platform_Options platform = { .ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST,
        .ProductId = config[0], .SandboxId = config[1], .DeploymentId = config[2],
        .ClientCredentials = { .ClientId = config[3], .ClientSecret = config[4] },
        .Flags = EOS_PF_DISABLE_OVERLAY, .TickBudgetInMilliseconds = 2 };
    online.platform = EOS_Platform_Create(&platform);
    memset(config, 0, sizeof(config));
    if (!online.platform) {
        snprintf(online.status, sizeof(online.status), "EOS platform creation failed.");
        fprintf(stderr, "%s\n", online.status); EOS_Shutdown(); online.initialized = false; return false;
    }
    online.connect = EOS_Platform_GetConnectInterface(online.platform);
    online.lobbies = EOS_Platform_GetLobbyInterface(online.platform);
    online.p2p = EOS_Platform_GetP2PInterface(online.platform);
    EOS_P2P_SetRelayControlOptions relay = {
        .ApiVersion = EOS_P2P_SETRELAYCONTROL_API_LATEST,
        .RelayControl = Cvar_Integer("online_force_relay", 0) ? EOS_RC_ForceRelays : EOS_RC_AllowRelays
    };
    if (!Online_Result("configure relay policy", EOS_P2P_SetRelayControl(online.p2p, &relay))) {
        Online_Shutdown();
        snprintf(online.status, sizeof(online.status), "Internet relay policy could not be configured.");
        return false;
    }
    online.socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    snprintf(online.socket.SocketName, sizeof(online.socket.SocketName), "OpenRealm");
    EOS_P2P_SetPacketQueueSizeOptions queues = {
        .ApiVersion = EOS_P2P_SETPACKETQUEUESIZE_API_LATEST,
        .IncomingPacketQueueMaxSizeBytes = 8 * 1024 * 1024, .OutgoingPacketQueueMaxSizeBytes = 8 * 1024 * 1024
    };
    if (!Online_Result("configure packet queues", EOS_P2P_SetPacketQueueSize(online.p2p, &queues))) {
        Online_Shutdown();
        snprintf(online.status, sizeof(online.status), "Internet packet queues could not be configured.");
        return false;
    }
    EOS_Lobby_AddNotifyLobbyMemberStatusReceivedOptions members = { .ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYMEMBERSTATUSRECEIVED_API_LATEST };
    EOS_Lobby_AddNotifyLobbyUpdateReceivedOptions updates = { .ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYUPDATERECEIVED_API_LATEST };
    EOS_Connect_AddNotifyAuthExpirationOptions expiration = { .ApiVersion = EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST };
    EOS_Connect_AddNotifyLoginStatusChangedOptions login = { .ApiVersion = EOS_CONNECT_ADDNOTIFYLOGINSTATUSCHANGED_API_LATEST };
    online.member_notify = EOS_Lobby_AddNotifyLobbyMemberStatusReceived(online.lobbies, &members, NULL, Online_MemberChanged);
    online.update_notify = EOS_Lobby_AddNotifyLobbyUpdateReceived(online.lobbies, &updates, NULL, Online_LobbyChanged);
    online.expiration_notify = EOS_Connect_AddNotifyAuthExpiration(online.connect, &expiration, NULL, Online_Expired);
    online.login_notify = EOS_Connect_AddNotifyLoginStatusChanged(online.connect, &login, NULL, Online_LoginStatus);
    if (!online.member_notify || !online.update_notify || !online.expiration_notify || !online.login_notify) {
        fprintf(stderr, "EOS notification registration failed\n"); Online_Shutdown();
        snprintf(online.status, sizeof(online.status), "Internet service notifications could not be initialized.");
        return false;
    }
    snprintf(online.bucket, sizeof(online.bucket), "%s-%d-%d", BZ_GAME, BZ_PROTOCOL_VERSION, Cvar_Integer("fs_expansion", 0));
    snprintf(online.status, sizeof(online.status), "Signing in to Internet games...");
    EOS_Connect_CreateDeviceIdOptions device = { .ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST, .DeviceModel = "OpenRealm Desktop" };
    online.authenticating = true;
    EOS_Connect_CreateDeviceId(online.connect, &device, NULL, Online_DeviceCreated);
    return true;
}

static EOS_Lobby_Attribute *Online_Attribute(EOS_HLobbyDetails details, cstring_t name) {
    EOS_Lobby_Attribute *attribute = NULL;
    EOS_LobbyDetails_CopyAttributeByKeyOptions options = {
        .ApiVersion = EOS_LOBBYDETAILS_COPYATTRIBUTEBYKEY_API_LATEST, .AttrKey = name
    };
    EOS_EResult result = EOS_LobbyDetails_CopyAttributeByKey(details, &options, &attribute);
    if (result != EOS_Success) { fprintf(stderr, "EOS lobby missing attribute '%s': %s\n", name, EOS_EResult_ToString(result)); return NULL; }
    return attribute;
}
static bool Online_CopyString(EOS_HLobbyDetails details, cstring_t name, char *out, size_t size) {
    EOS_Lobby_Attribute *attribute = Online_Attribute(details, name);
    if (!attribute) return false;
    bool valid = attribute->Data && attribute->Data->ValueType == EOS_AT_STRING &&
        attribute->Data->Value.AsUtf8 && strlen(attribute->Data->Value.AsUtf8) < size;
    if (valid) snprintf(out, size, "%s", attribute->Data->Value.AsUtf8);
    else fprintf(stderr, "EOS invalid string attribute '%s'\n", name);
    EOS_Lobby_Attribute_Release(attribute); return valid;
}
static bool Online_CopyInt(EOS_HLobbyDetails details, cstring_t name, uint32_t *out) {
    EOS_Lobby_Attribute *attribute = Online_Attribute(details, name);
    if (!attribute) return false;
    bool valid = attribute->Data && attribute->Data->ValueType == EOS_AT_INT64 &&
        attribute->Data->Value.AsInt64 >= 0 && attribute->Data->Value.AsInt64 <= UINT32_MAX;
    if (valid) *out = (uint32_t)attribute->Data->Value.AsInt64;
    else fprintf(stderr, "EOS invalid integer attribute '%s'\n", name);
    EOS_Lobby_Attribute_Release(attribute); return valid;
}

static void EOS_CALL Online_Found(EOS_LobbySearch_FindCallbackInfo const *info) {
    online.operation_pending = false;
    if (!Online_Current(info->ClientData)) return;
    if (!Online_Result("search games", info->ResultCode)) return;
    Online_ClearGames();
    EOS_LobbySearch_GetSearchResultCountOptions count = { .ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST };
    uint32_t num = MIN(EOS_LobbySearch_GetSearchResultCount(online.search, &count), ONLINE_MAX_GAMES);
    for (uint32_t i = 0; i < num; i++) {
        EOS_HLobbyDetails details = NULL;
        EOS_LobbySearch_CopySearchResultByIndexOptions item = {
            .ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST, .LobbyIndex = i
        };
        if (!Online_Result("read search result", EOS_LobbySearch_CopySearchResultByIndex(online.search, &item, &details))) continue;
        EOS_LobbyDetails_Info *lobby = NULL;
        EOS_LobbyDetails_CopyInfoOptions options = { .ApiVersion = EOS_LOBBYDETAILS_COPYINFO_API_LATEST };
        if (!Online_Result("read game", EOS_LobbyDetails_CopyInfo(details, &options, &lobby))) { EOS_LobbyDetails_Release(details); continue; }
        onlineResult_t result = { .details = details };
        int32_t len = sizeof(result.game.address) - 4;
        snprintf(result.game.address, sizeof(result.game.address), "eos:");
        bool valid = lobby->BucketId && !strcmp(lobby->BucketId, online.bucket) && lobby->AvailableSlots &&
            lobby->PermissionLevel == EOS_LPL_PUBLICADVERTISED &&
            EOS_ProductUserId_ToString(lobby->LobbyOwnerUserId, result.game.address + 4, &len) == EOS_Success &&
            Online_CopyString(details, "name", result.game.hostname, sizeof(result.game.hostname)) &&
            Online_CopyString(details, "map", result.game.mapname, sizeof(result.game.mapname)) &&
            Online_CopyInt(details, "crc", &result.crc) && Online_CopyInt(details, "speed", &result.game.speed) &&
            Online_CopyInt(details, "slots", &result.game.slots) && Online_CopyInt(details, "players", &result.game.players);
        result.game.maxPlayers = lobby->MaxMembers;
        if (valid) online.games[online.num_games++] = result;
        else { fprintf(stderr, "EOS ignored incompatible or malformed lobby\n"); EOS_LobbyDetails_Release(details); }
        EOS_LobbyDetails_Info_Release(lobby);
    }
    snprintf(online.status, sizeof(online.status), "%u Internet games found.", online.num_games);
}

void Online_Refresh(void) { if (online.enabled) online.search_pending = true; }
static void Online_Search(void) {
    if (online.search) EOS_LobbySearch_Release(online.search);
    online.search = NULL;
    EOS_Lobby_CreateLobbySearchOptions create = { .ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST, .MaxResults = ONLINE_MAX_GAMES };
    if (!Online_Result("create search", EOS_Lobby_CreateLobbySearch(online.lobbies, &create, &online.search))) return;
    EOS_Lobby_AttributeData data = { .ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST,
        .Key = EOS_LOBBY_SEARCH_BUCKET_ID, .ValueType = EOS_AT_STRING, .Value.AsUtf8 = online.bucket };
    EOS_LobbySearch_SetParameterOptions parameter = {
        .ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST, .Parameter = &data, .ComparisonOp = EOS_CO_EQUAL
    };
    if (!Online_Result("filter search", EOS_LobbySearch_SetParameter(online.search, &parameter))) return;
    EOS_LobbySearch_FindOptions find = { .ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST, .LocalUserId = online.user };
    online.operation_pending = true;
    snprintf(online.status, sizeof(online.status), "Searching Internet games...");
    EOS_LobbySearch_Find(online.search, &find, Online_Context(), Online_Found);
}

static bool Online_MapCRC(cstring_t map, uint32_t *crc) {
    void *bytes = NULL; int size = FS_ReadFileQ3(map, &bytes);
    if (size <= 0 || !bytes) {
        if (bytes) FS_FreeFile(bytes);
        snprintf(online.status, sizeof(online.status), "The game map is not installed.");
        fprintf(stderr, "EOS cannot read map '%s'\n", map); return false;
    }
    *crc = (uint32_t)crc32(0, bytes, (uInt)size); FS_FreeFile(bytes); return true;
}

static void EOS_CALL Online_Joined(EOS_Lobby_JoinLobbyCallbackInfo const *info) {
    online.operation_pending = false;
    if (!Online_Current(info->ClientData)) {
        if (info->ResultCode == EOS_Success) Online_Depart(info->LobbyId, false, NULL);
        return;
    }
    online.operation_pending = false;
    if (!Online_Result("join game", info->ResultCode)) return;
    snprintf(online.lobby, sizeof(online.lobby), "%s", info->LobbyId);
    Online_UpdateMembers(); online.connecting = true;
    snprintf(online.status, sizeof(online.status), "Connecting to game host...");
}

void Online_Join(uint32_t index) {
    if (!online.ready || online.operation_pending || online.leave_pending || online.lobby[0] || index >= online.num_games) return;
    onlineResult_t *game = &online.games[index]; uint32_t crc;
    if (!Online_MapCRC(game->game.mapname, &crc)) return;
    if (crc != game->crc) {
        snprintf(online.status, sizeof(online.status), "The installed map differs from the host's map.");
        fprintf(stderr, "%s\n", online.status); return;
    }
    EOS_LobbyDetails_Info *info = NULL;
    EOS_LobbyDetails_CopyInfoOptions copy = { .ApiVersion = EOS_LOBBYDETAILS_COPYINFO_API_LATEST };
    if (!Online_Result("read host", EOS_LobbyDetails_CopyInfo(game->details, &copy, &info))) return;
    online.owner = info->LobbyOwnerUserId; EOS_LobbyDetails_Info_Release(info);
    EOS_Lobby_JoinLobbyOptions options = { .ApiVersion = EOS_LOBBY_JOINLOBBY_API_LATEST,
        .LocalUserId = online.user, .LobbyDetailsHandle = game->details };
    online.operation_pending = true;
    EOS_Lobby_JoinLobby(online.lobbies, &options, Online_Context(), Online_Joined);
}

static void EOS_CALL Online_HostUpdated(EOS_Lobby_UpdateLobbyCallbackInfo const *info) {
    online.updating = false;
    if (!Online_Current(info->ClientData)) return;
    if (!Online_Result("publish game", info->ResultCode)) { online.host_dirty = true; online.update_retry = 5000; return; }
    online.published = true;
    snprintf(online.status, sizeof(online.status), online.admission_closed ? "Internet game in progress." : "Hosting an Internet game.");
}

static bool Online_AddAttribute(EOS_HLobbyModification modification, cstring_t key, cstring_t text, uint32_t value) {
    EOS_Lobby_AttributeData data = { .ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST, .Key = key,
        .ValueType = text ? EOS_AT_STRING : EOS_AT_INT64 };
    if (text) data.Value.AsUtf8 = text; else data.Value.AsInt64 = value;
    EOS_LobbyModification_AddAttributeOptions options = { .ApiVersion = EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST,
        .Attribute = &data, .Visibility = EOS_LAT_PUBLIC };
    return Online_Result("set game attribute", EOS_LobbyModification_AddAttribute(modification, &options));
}

static void Online_UpdateHost(void) {
    EOS_HLobbyModification modification = NULL;
    EOS_Lobby_UpdateLobbyModificationOptions create = {
        .ApiVersion = EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST, .LobbyId = online.lobby, .LocalUserId = online.user
    };
    if (!Online_Result("edit game", EOS_Lobby_UpdateLobbyModification(online.lobbies, &create, &modification))) return;
    EOS_LobbyModification_SetPermissionLevelOptions permission = {
        .ApiVersion = EOS_LOBBYMODIFICATION_SETPERMISSIONLEVEL_API_LATEST,
        .PermissionLevel = online.admission_closed ? EOS_LPL_INVITEONLY : EOS_LPL_PUBLICADVERTISED
    };
    EOS_LobbyModification_SetMaxMembersOptions members = { .ApiVersion = EOS_LOBBYMODIFICATION_SETMAXMEMBERS_API_LATEST,
        .MaxMembers = online.hosted.maxPlayers };
    bool valid = Online_Result("set admission", EOS_LobbyModification_SetPermissionLevel(modification, &permission)) &&
        Online_Result("set member limit", EOS_LobbyModification_SetMaxMembers(modification, &members)) &&
        Online_AddAttribute(modification, "map", online.hosted.mapname, 0) &&
        Online_AddAttribute(modification, "name", online.hosted.hostname, 0) &&
        Online_AddAttribute(modification, "crc", NULL, online.map_crc) &&
        Online_AddAttribute(modification, "speed", NULL, online.hosted.speed) &&
        Online_AddAttribute(modification, "slots", NULL, online.hosted.slots) &&
        Online_AddAttribute(modification, "players", NULL, online.hosted.players);
    if (valid) {
        EOS_Lobby_UpdateLobbyOptions update = { .ApiVersion = EOS_LOBBY_UPDATELOBBY_API_LATEST, .LobbyModificationHandle = modification };
        online.updating = true; online.host_dirty = false;
        EOS_Lobby_UpdateLobby(online.lobbies, &update, Online_Context(), Online_HostUpdated);
    } else online.update_retry = 5000;
    EOS_LobbyModification_Release(modification);
}

static void EOS_CALL Online_Created(EOS_Lobby_CreateLobbyCallbackInfo const *info) {
    online.operation_pending = false;
    if (!Online_Current(info->ClientData)) {
        if (info->ResultCode == EOS_Success) Online_Depart(info->LobbyId, true, NULL);
        return;
    }
    online.operation_pending = false;
    if (!Online_Result("create game", info->ResultCode)) {
        online.hosting = false; online.update_retry = 5000; return;
    }
    snprintf(online.lobby, sizeof(online.lobby), "%s", info->LobbyId);
    online.owner = online.user; Online_UpdateMembers(); online.host_dirty = true;
}

void Online_Host(cstring_t map, cstring_t name, uint32_t players, uint32_t slots, uint32_t speed) {
    if (!online.enabled || !online.ready ||
        (!online.hosting && (online.operation_pending || online.updating || online.update_retry)) || online.leave_pending) return;
    if (!map || strlen(map) >= sizeof(online.hosted.mapname) || !slots || slots > ONLINE_MAX_MEMBERS) {
        snprintf(online.status, sizeof(online.status), "This map's room settings cannot be published.");
        fprintf(stderr, "EOS invalid game metadata\n"); online.update_retry = 5000; return;
    }
    if (!online.hosting) {
        if (online.lobby[0]) return;
        if (!Online_MapCRC(map, &online.map_crc)) { online.update_retry = 5000; return; }
        online.hosting = true; online.admission_closed = false;
    } else if (strcmp(online.hosted.mapname, map)) {
        snprintf(online.status, sizeof(online.status), "Leave the Internet lobby before changing maps.");
        fprintf(stderr, "%s\n", online.status); return;
    }
    onlineGame_t next = { .players = players, .slots = slots, .maxPlayers = slots, .speed = speed };
    snprintf(next.hostname, sizeof(next.hostname), "%s", name ? name : "Internet Game");
    snprintf(next.mapname, sizeof(next.mapname), "%s", map);
    if (memcmp(&next, &online.hosted, sizeof(next))) { online.hosted = next; online.host_dirty = true; }
    if (!online.lobby[0] && !online.operation_pending) {
        EOS_Lobby_CreateLobbyOptions create = { .ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST,
            .LocalUserId = online.user, .MaxLobbyMembers = slots, .PermissionLevel = EOS_LPL_INVITEONLY,
            .BucketId = online.bucket, .bDisableHostMigration = EOS_TRUE, .bAllowInvites = EOS_FALSE };
        online.operation_pending = true;
        snprintf(online.status, sizeof(online.status), "Creating Internet game...");
        EOS_Lobby_CreateLobby(online.lobbies, &create, Online_Context(), Online_Created);
    }
}

void Online_CloseAdmission(void) {
    if (online.hosting) { online.admission_closed = true; online.host_dirty = true; }
}

void Online_Send(NETSOURCE source, int length, void const *data, netadr_t const *to) {
    EOS_ProductUserId user = EOS_ProductUserId_FromString(to->peer);
    onlinePeer_t *peer = Online_Peer(user, source);
    if (!online.ready || !peer || peer->closed || length <= 0 || length > MAX_MSGLEN) {
        fprintf(stderr, "EOS rejected packet for inactive peer or invalid size\n"); return;
    }
    EOS_P2P_AcceptConnectionOptions accept = {
        .ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST, .LocalUserId = online.user,
        .RemoteUserId = user, .SocketId = &online.socket
    };
    if (!Online_Result("request peer", EOS_P2P_AcceptConnection(online.p2p, &accept))) return;
    uint32_t id = ++online.message_id;
    uint8_t fragment[ONLINE_FRAGMENT_SIZE];
    for (uint32_t offset = 0; offset < (uint32_t)length;) {
        uint32_t size = Online_WriteFragment(fragment, id, length, offset, data);
        EOS_P2P_SendPacketOptions options = { .ApiVersion = EOS_P2P_SENDPACKET_API_LATEST,
            .LocalUserId = online.user, .RemoteUserId = user, .SocketId = &online.socket,
            .Channel = source == NS_CLIENT ? 0 : 1, .Data = fragment, .DataLengthBytes = size,
            .bAllowDelayedDelivery = EOS_TRUE, .Reliability = EOS_PR_ReliableOrdered,
            .bDisableAutoAcceptConnection = EOS_TRUE };
        if (!Online_Result("send packet", EOS_P2P_SendPacket(online.p2p, &options))) return;
        offset += size - ONLINE_FRAGMENT_HEADER;
    }
}

int Online_Receive(NETSOURCE source, netadr_t *from, sizeBuf_t *message) {
    if (!online.ready || !online.lobby[0]) return 0;
    uint8_t requested = source == NS_SERVER ? 0 : 1, channel, bytes[ONLINE_FRAGMENT_SIZE];
    EOS_P2P_ReceivePacketOptions options = { .ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST,
        .LocalUserId = online.user, .MaxDataSizeBytes = sizeof(bytes), .RequestedChannel = &requested };
    while (online.receive_budget[source]) {
        online.receive_budget[source]--;
        EOS_ProductUserId user; EOS_P2P_SocketId socket; uint32_t length;
        EOS_EResult result = EOS_P2P_ReceivePacket(online.p2p, &options, &user, &socket, &channel, bytes, &length);
        if (result == EOS_NotFound) return 0;
        if (!Online_Result("receive packet", result)) return 0;
        onlinePeer_t *peer = Online_Peer(user, source);
        if (!peer || peer->closed || strcmp(socket.SocketName, online.socket.SocketName) || channel != requested) {
            fprintf(stderr, "EOS rejected packet from inactive peer/socket/channel\n"); continue;
        }
        int size = Online_ReadFragment(&peer->assembly[source], bytes, length, message);
        if (size < 0) { fprintf(stderr, "EOS rejected malformed packet fragment\n"); continue; }
        if (size) {
            memset(from, 0, sizeof(*from)); from->type = NA_EOS;
            int32_t capacity = sizeof(from->peer);
            if (!Online_Result("serialize peer", EOS_ProductUserId_ToString(user, from->peer, &capacity))) return 0;
            return size;
        }
    }
    return 0;
}

bool Online_TakeConnection(netadr_t *address) {
    if (!online.connecting || !online.lobby[0]) return false;
    online.connecting = false;
    memset(address, 0, sizeof(*address)); address->type = NA_EOS;
    int32_t size = sizeof(address->peer);
    return Online_Result("read host identity", EOS_ProductUserId_ToString(online.owner, address->peer, &size));
}

void Online_Frame(uint32_t msec) {
    if (!online.platform) return;
    online.receive_budget[NS_CLIENT] = online.receive_budget[NS_SERVER] = ONLINE_RECEIVE_BUDGET;
    EOS_Platform_Tick(online.platform);
    for (uint32_t i = 0; i < online.num_peers; i++) for (int j = 0; j < 2; j++) {
        onlineAssembly_t *assembly = &online.peers[i].assembly[j];
        if (!assembly->data) continue;
        assembly->age += MIN(msec, ONLINE_ASSEMBLY_TIMEOUT);
        if (assembly->age >= ONLINE_ASSEMBLY_TIMEOUT) {
            fprintf(stderr, "EOS expired an incomplete engine packet\n"); Online_ClearAssembly(assembly);
        }
    }
    online.update_retry = online.update_retry > msec ? online.update_retry - msec : 0;
    if (!online.ready || !online.enabled || online.leave_pending) return;
    if (online.search_pending && !online.operation_pending && !online.lobby[0]) {
        online.search_pending = false; Online_Search();
    }
    if (online.host_dirty && online.lobby[0] && !online.updating && !online.update_retry) Online_UpdateHost();
}

cstring_t Online_Status(void) { return online.status[0] ? online.status : "Connect to Internet games."; }
bool Online_Ready(void) { return online.enabled && online.ready; }
bool Online_IsHost(void) { return online.hosting; }
bool Online_HostReady(void) {
    return Online_Ready() && online.hosting && online.published && !online.host_dirty &&
        !online.updating && !online.admission_closed;
}
bool Online_InLobby(void) { return online.lobby[0] || online.operation_pending; }
bool Online_ConnectionLost(NETSOURCE source, netadr_t const *address) {
    if (!address || address->type != NA_EOS) return false;
    onlinePeer_t *peer = Online_Peer(EOS_ProductUserId_FromString(address->peer), source);
    return !peer || peer->closed;
}
uint32_t Online_NumGames(void) { return online.num_games; }
bool Online_Game(uint32_t index, onlineGame_t *out) {
    if (!out || index >= online.num_games) return false;
    *out = online.games[index].game; return true;
}

void Online_Shutdown(void) {
    if (!online.platform) return;
    Online_Leave();
    if (online.request_notify) EOS_P2P_RemoveNotifyPeerConnectionRequest(online.p2p, online.request_notify);
    if (online.established_notify) EOS_P2P_RemoveNotifyPeerConnectionEstablished(online.p2p, online.established_notify);
    if (online.closed_notify) EOS_P2P_RemoveNotifyPeerConnectionClosed(online.p2p, online.closed_notify);
    if (online.member_notify) EOS_Lobby_RemoveNotifyLobbyMemberStatusReceived(online.lobbies, online.member_notify);
    if (online.update_notify) EOS_Lobby_RemoveNotifyLobbyUpdateReceived(online.lobbies, online.update_notify);
    if (online.expiration_notify) EOS_Connect_RemoveNotifyAuthExpiration(online.connect, online.expiration_notify);
    if (online.login_notify) EOS_Connect_RemoveNotifyLoginStatusChanged(online.connect, online.login_notify);
    if (online.search) EOS_LobbySearch_Release(online.search);
    EOS_Platform_Release(online.platform);
    if (online.initialized) Online_Result("shutdown", EOS_Shutdown());
    memset(&online, 0, sizeof(online));
}

#ifdef BZ_TESTS
#include "shared/test.h"
#include "server/server.h"
static bool online_test_sdk;
static void Online_TestSDKShutdown(void) {
    if (online_test_sdk && !online.initialized) EOS_Shutdown();
}
static bool Online_TestSDK(void) {
    if (online.initialized || online_test_sdk) return true;
    EOS_InitializeOptions init = { .ApiVersion = EOS_INITIALIZE_API_LATEST,
        .ProductName = "OpenRealm Tests", .ProductVersion = "1" };
    EOS_EResult result = EOS_Initialize(&init);
    T_EQ(result, EOS_Success);
    if (result != EOS_Success) return false;
    /* The native SDK owns one lifetime per process, shared by offline tests. */
    online_test_sdk = true; atexit(Online_TestSDKShutdown);
    return true;
}
TEST(online_service, lost_guest_releases_server_slot_without_dropping_host) {
    void *saved = malloc(sizeof(online));
    client_t *clients = malloc(2 * sizeof(*clients));
    T_NOT_NULL(saved); T_NOT_NULL(clients);
    if (!saved || !clients) { free(saved); free(clients); return; }
    if (!Online_TestSDK()) { free(saved); free(clients); return; }
    memcpy(saved, &online, sizeof(online)); memset(&online, 0, sizeof(online));
    memcpy(clients, svs.clients, 2 * sizeof(*clients));
    lobbyState_t lobby = svs.lobby;
    uint32_t count = svs.num_clients, realtime = svs.realtime;
    serverState_t state = sv.state;
    sv.state = ss_dead; svs.num_clients = 2; svs.realtime = 100;
    memset(svs.clients, 0, 2 * sizeof(*clients)); memset(&svs.lobby, 0, sizeof(svs.lobby));
    svs.lobby.active = true; svs.lobby.slot_count = 2;
    svs.lobby.slots[1] = (lobbySlot_t){ .visible = true, .occupied = true, .client = 1, .type = LOBBY_SLOT_HUMAN };
    svs.clients[0].state = svs.clients[1].state = cs_connected;
    svs.clients[0].netchan.remote_address.type = NA_LOOPBACK;
    svs.clients[1].netchan.remote_address.type = NA_EOS;
    strlcpy(svs.clients[1].netchan.remote_address.peer, "0123456789abcdef0123456789abcde2", sizeof(svs.clients[1].netchan.remote_address.peer));
    SZ_Init(&svs.clients[1].netchan.message, svs.clients[1].netchan.message_buf, MAX_MSGLEN);
    online.user = EOS_ProductUserId_FromString("0123456789abcdef0123456789abcde1");
    online.hosting = true; online.num_peers = 1;
    online.peers[0].user = EOS_ProductUserId_FromString(svs.clients[1].netchan.remote_address.peer);
    strlcpy(online.lobby, "test-room", sizeof(online.lobby)); strlcpy(online.socket.SocketName, "OpenRealm", sizeof(online.socket.SocketName));
    /* A member waiting for its first connection is not a lost connection. */
    T_ASSERT(!Online_ConnectionLost(NS_SERVER, &svs.clients[1].netchan.remote_address));
    SV_ReapZombieClients(); T_EQ(svs.clients[1].state, cs_connected);
    EOS_P2P_OnRemoteConnectionClosedInfo closed = { .LocalUserId = online.user,
        .RemoteUserId = online.peers[0].user, .SocketId = &online.socket, .Reason = EOS_CCR_ConnectionClosed };
    Online_ConnectionClosed(&closed);
    T_ASSERT(Online_ConnectionLost(NS_SERVER, &svs.clients[1].netchan.remote_address));
    SV_ReapZombieClients();
    T_EQ(svs.clients[0].state, cs_connected); T_EQ(svs.clients[1].state, cs_zombie);
    T_ASSERT(!svs.lobby.slots[1].occupied); T_EQ(svs.lobby.slots[1].type, LOBBY_SLOT_OPEN);
    svs.realtime += BZ_CLIENT_ZOMBIE_MSEC;
    SV_ReapZombieClients(); T_EQ(svs.clients[1].state, cs_free); T_EQ(svs.num_clients, 1);
    svs.num_clients = 2; svs.clients[1].state = cs_connected;
    T_ASSERT(SV_LobbyAssignClient(1, false)); T_ASSERT(svs.lobby.slots[1].occupied);
    memcpy(&online, saved, sizeof(online)); free(saved);
    memcpy(svs.clients, clients, 2 * sizeof(*clients)); free(clients);
    svs.lobby = lobby; svs.num_clients = count; svs.realtime = realtime; sv.state = state;
}
TEST(online_service, lost_host_connection_clears_guest_room_and_partial_packets) {
    void *saved = malloc(sizeof(online));
    T_NOT_NULL(saved); if (!saved) return;
    memcpy(saved, &online, sizeof(online)); memset(&online, 0, sizeof(online));
    /* Opaque identities are compared only; this callback test needs no SDK login. */
    EOS_ProductUserId local = (EOS_ProductUserId)(uintptr_t)1;
    EOS_ProductUserId owner = (EOS_ProductUserId)(uintptr_t)2;
    EOS_ProductUserId other = (EOS_ProductUserId)(uintptr_t)3;
    EOS_P2P_SocketId socket = { .ApiVersion = EOS_P2P_SOCKETID_API_LATEST };
    snprintf(socket.SocketName, sizeof(socket.SocketName), "OpenRealm");
    online.socket = socket; online.user = local; online.owner = owner;
    online.enabled = online.ready = online.connecting = true;
    snprintf(online.lobby, sizeof(online.lobby), "test-room");
    online.peers[0].user = owner; online.num_peers = 1;
    online.peers[0].assembly[NS_CLIENT].data = malloc(32);
    T_NOT_NULL(online.peers[0].assembly[NS_CLIENT].data);
    EOS_P2P_OnRemoteConnectionClosedInfo closed = { .LocalUserId = local,
        .RemoteUserId = other, .SocketId = &socket, .Reason = EOS_CCR_ConnectionClosed };
    Online_ConnectionClosed(&closed); T_ASSERT(Online_InLobby());
    closed.RemoteUserId = owner; closed.LocalUserId = other;
    Online_ConnectionClosed(&closed); T_ASSERT(Online_InLobby());
    closed.LocalUserId = local; snprintf(socket.SocketName, sizeof(socket.SocketName), "Foreign");
    Online_ConnectionClosed(&closed); T_ASSERT(Online_InLobby());
    socket = online.socket; closed.Reason = EOS_CCR_ClosedByLocalUser;
    Online_ConnectionClosed(&closed); T_ASSERT(Online_InLobby());
    closed.Reason = EOS_CCR_ConnectionClosed;
    Online_ConnectionClosed(&closed);
    T_ASSERT(!Online_InLobby()); T_ASSERT(!Online_Ready());
    T_ASSERT(!online.connecting); T_NULL(online.owner); T_EQ(online.num_peers, 0);
    T_NULL(online.peers[0].assembly[NS_CLIENT].data);
    T_STREQ(Online_Status(), "Connection to the Internet host has closed.");
    Online_ClearPeers();
    memcpy(&online, saved, sizeof(online)); free(saved);
}

TEST(online_service, departing_guest_does_not_close_host_or_other_peer_assembly) {
    void *saved = malloc(sizeof(online));
    T_NOT_NULL(saved); if (!saved) return;
    memcpy(saved, &online, sizeof(online)); memset(&online, 0, sizeof(online));
    online.user = online.owner = (EOS_ProductUserId)(uintptr_t)1;
    online.hosting = online.enabled = online.ready = online.published = true;
    snprintf(online.lobby, sizeof(online.lobby), "test-room");
    snprintf(online.socket.SocketName, sizeof(online.socket.SocketName), "OpenRealm");
    online.peers[0].user = (EOS_ProductUserId)(uintptr_t)2;
    online.peers[1].user = (EOS_ProductUserId)(uintptr_t)3;
    online.num_peers = 2;
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) {
        online.peers[i].assembly[j].data = malloc(32);
        T_NOT_NULL(online.peers[i].assembly[j].data);
    }
    EOS_P2P_OnPeerConnectionEstablishedInfo established = { .LocalUserId = online.user,
        .RemoteUserId = online.peers[0].user, .SocketId = &online.socket,
        .NetworkType = EOS_NCT_RelayedConnection };
    Online_ConnectionEstablished(&established);
    T_EQ(online.peers[0].network_type, EOS_NCT_RelayedConnection);
    EOS_P2P_OnRemoteConnectionClosedInfo closed = { .LocalUserId = online.user,
        .RemoteUserId = online.peers[0].user, .SocketId = &online.socket, .Reason = EOS_CCR_ConnectionClosed };
    Online_ConnectionClosed(&closed);
    T_ASSERT(Online_InLobby()); T_ASSERT(Online_HostReady());
    T_EQ(online.peers[0].network_type, EOS_NCT_NoConnection);
    T_ASSERT(online.peers[0].closed); T_ASSERT(!online.peers[1].closed);
    for (int i = 0; i < 2; i++) {
        T_NULL(online.peers[0].assembly[i].data);
        T_NOT_NULL(online.peers[1].assembly[i].data);
    }
    established.ConnectionType = EOS_CET_Reconnection;
    established.NetworkType = EOS_NCT_DirectConnection;
    Online_ConnectionEstablished(&established);
    T_EQ(online.peers[0].network_type, EOS_NCT_DirectConnection);
    T_ASSERT(!online.peers[0].closed);
    Online_ClearPeers();
    memcpy(&online, saved, sizeof(online)); free(saved);
}

TEST(online_service, departure_failures_remain_visible_until_the_batch_finishes) {
    void *saved = malloc(sizeof(online));
    T_NOT_NULL(saved); if (!saved) return;
    memcpy(saved, &online, sizeof(online)); memset(&online, 0, sizeof(online));
    online.leave_pending = 2;
    EOS_Lobby_DestroyLobbyCallbackInfo destroyed = { .ResultCode = EOS_UnexpectedError };
    Online_Destroyed(&destroyed);
    T_EQ(online.leave_pending, 1); T_ASSERT(online.departure_failed);
    EOS_Lobby_LeaveLobbyCallbackInfo left = { .ResultCode = EOS_Success };
    Online_Left(&left);
    T_EQ(online.leave_pending, 0); T_ASSERT(online.departure_failed);
    T_STREQ(Online_Status(), "destroy lobby: EOS_UnexpectedError");
    online.departure_failed = false; online.leave_pending = 1;
    left.ResultCode = EOS_NotFound;
    Online_Left(&left);
    T_EQ(online.leave_pending, 0); T_ASSERT(!online.departure_failed);
    memcpy(&online, saved, sizeof(online)); free(saved);
}

TEST(online_service, cancel_waits_for_late_create_and_update_callbacks) {
    void *saved = malloc(sizeof(online));
    T_NOT_NULL(saved); if (!saved) return;
    memcpy(saved, &online, sizeof(online)); memset(&online, 0, sizeof(online));
    online.epoch = 40; online.enabled = online.hosting = online.ready = online.published = true;
    T_ASSERT(Online_HostReady());
    online.host_dirty = true; T_ASSERT(!Online_HostReady()); online.host_dirty = false;
    online.operation_pending = online.updating = true;
    T_ASSERT(!Online_HostReady());
    void *old_context = Online_Context();
    Online_Leave();
    T_ASSERT(!online.enabled && !online.hosting && !online.connecting);
    T_ASSERT(online.operation_pending && online.updating); T_ASSERT(!Online_HostReady());
    /* Another cancellation must not permit a new operation while the old one is live. */
    Online_Leave(); T_ASSERT(online.operation_pending && online.updating);
    EOS_Lobby_CreateLobbyCallbackInfo created = { .ClientData = old_context, .ResultCode = EOS_UnexpectedError };
    Online_Created(&created);
    T_ASSERT(!online.operation_pending); T_ASSERT(!online.lobby[0]);
    EOS_Lobby_UpdateLobbyCallbackInfo updated = { .ClientData = old_context, .ResultCode = EOS_Success };
    Online_HostUpdated(&updated);
    T_ASSERT(!online.updating && !online.published);
    memcpy(&online, saved, sizeof(online)); free(saved);
}

TEST(online_service, canceled_search_cannot_replace_results_or_status) {
    void *saved = malloc(sizeof(online));
    T_NOT_NULL(saved); if (!saved) return;
    memcpy(saved, &online, sizeof(online)); memset(&online, 0, sizeof(online));
    online.epoch = 11; online.operation_pending = true;
    void *old_context = Online_Context();
    Online_Leave();
    char status[sizeof(online.status)]; snprintf(status, sizeof(status), "%s", online.status);
    EOS_LobbySearch_FindCallbackInfo found = { .ClientData = old_context, .ResultCode = EOS_Success };
    Online_Found(&found);
    T_ASSERT(!online.operation_pending); T_EQ(online.num_games, 0); T_STREQ(online.status, status);
    memcpy(&online, saved, sizeof(online)); free(saved);
}

TEST(online_service, server_and_client_admit_only_their_lobby_peers) {
    void *saved = malloc(sizeof(online));
    T_NOT_NULL(saved); if (!saved) return;
    if (!Online_TestSDK()) { free(saved); return; }
    memcpy(saved, &online, sizeof(online)); memset(&online, 0, sizeof(online));
    EOS_ProductUserId local = EOS_ProductUserId_FromString("0123456789abcdef0123456789abcde1");
    EOS_ProductUserId owner = EOS_ProductUserId_FromString("0123456789abcdef0123456789abcde2");
    EOS_ProductUserId other = EOS_ProductUserId_FromString("0123456789abcdef0123456789abcde3");
    T_NOT_NULL(local); T_NOT_NULL(owner); T_NOT_NULL(other);
    online.user = local; online.owner = owner; snprintf(online.lobby, sizeof(online.lobby), "test-room");
    online.peers[0].user = owner; online.peers[1].user = other; online.num_peers = 2;
    T_NOT_NULL(Online_Peer(owner, NS_CLIENT));
    T_NULL(Online_Peer(other, NS_CLIENT)); T_NULL(Online_Peer(local, NS_CLIENT));
    T_NULL(Online_Peer(owner, NS_SERVER));
    online.hosting = true; online.owner = local;
    T_NOT_NULL(Online_Peer(owner, NS_SERVER)); T_NOT_NULL(Online_Peer(other, NS_SERVER));
    T_NULL(Online_Peer(local, NS_SERVER)); T_NULL(Online_Peer(owner, NS_CLIENT));
    online.num_peers = 1;
    T_NULL(Online_Peer(other, NS_SERVER)); /* departed member */
    online.lobby[0] = 0; T_NULL(Online_Peer(owner, NS_SERVER));
    memcpy(&online, saved, sizeof(online)); free(saved);
}
#include "../tests/online_acceptance.h"
#endif
#endif
