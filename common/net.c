/*
 * net.c — Unified network layer: loopback ring buffers + UDP sockets.
 *
 * The routing decision is made at runtime based on the destination address
 * type (netadr_t.type), matching the Quake 2 pattern:
 *
 *   NET_SendPacket()  — routes to loopback buffer (NA_LOOPBACK) or UDP
 *                       socket (NA_IP / NA_BROADCAST) based on to.type.
 *   NET_GetPacket()   — checks the loopback buffer first, then the UDP
 *                       socket.  Returns the sender address in *from.
 *
 * Loopback buffers (always present):
 *   Two 256 KiB ring buffers provide a zero-latency in-process path for
 *   local (same-process) server+client communication.  bufs[NS_CLIENT]
 *   carries client→server traffic; bufs[NS_SERVER] carries server→client.
 *
 * UDP sockets:
 *   Matching Quake 2's ip_sockets[NS_CLIENT/NS_SERVER], the client side
 *   uses an ephemeral port and the server side binds game_port.  Callers
 *   open only the side they need; NET_Config(true) remains a convenience
 *   for tests and tools that explicitly want both sockets.  UDP datagrams
 *   are sent raw, exactly like Quake 2; only the in-process loopback queue
 *   needs internal length framing.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "net_platform.h"
#include "common.h"

#define LOOPBACK_SIZE (1024 * 1024 * 8)

/* ---------------------------------------------------------------------------
 * Loopback ring buffers
 * -------------------------------------------------------------------------*/

struct loopback {
    char buffer[LOOPBACK_SIZE];
    int read;
    int write;
};

// bufs[NS_CLIENT] holds client→server packets; bufs[NS_SERVER] holds
// server→client packets.  Each sender writes into bufs[netsrc]; each
// receiver reads from bufs[!netsrc].
static struct loopback loopbufs[2];

static void NET_SendLoopPacket(NETSOURCE netsrc, int length, const void *data) {
    struct loopback *buf = &loopbufs[netsrc];
    int const packet_size = length + 4;

    if (length <= 0 || packet_size > LOOPBACK_SIZE) {
        fprintf(stderr, "NET_SendLoopPacket: bad packet length %d\n", length);
        return;
    }
    if (buf->write - buf->read + packet_size > LOOPBACK_SIZE) {
        fprintf(stderr, "NET_SendLoopPacket: loopback overflow, dropping queued packets\n");
        buf->read = buf->write;
    }

    DWORD len = (DWORD)length;
    FOR_LOOP(i, 4) {
        buf->buffer[(buf->write++) % LOOPBACK_SIZE] = ((char *)&len)[i];
    }
    FOR_LOOP(i, length) {
        buf->buffer[(buf->write++) % LOOPBACK_SIZE] = ((const char *)data)[i];
    }
}

int NET_GetLoopPacket(NETSOURCE netsrc, netadr_t *from, LPSIZEBUF msg) {
    struct loopback *buf = &loopbufs[!netsrc];
    if (buf->read == buf->write)
        return 0;

    DWORD size = 0;
    FOR_LOOP(i, 4) {
        ((char *)&size)[i] = buf->buffer[(buf->read++) % LOOPBACK_SIZE];
    }
    assert(size < MAX_MSGLEN);
    FOR_LOOP(i, size) {
        ((char *)msg->data)[i] = buf->buffer[(buf->read++) % LOOPBACK_SIZE];
    }
    msg->cursize = size;
    msg->readcount = 0;

    memset(from, 0, sizeof(*from));
    from->type = NA_LOOPBACK;
    return (int)size;
}

/* ---------------------------------------------------------------------------
 * UDP socket
 * -------------------------------------------------------------------------*/

static net_socket_t udp_sockets[2] = { NET_INVALID_SOCKET, NET_INVALID_SOCKET };

#ifdef _WIN32
static BOOL winsock_initialized;
#endif

static int NET_SocketError(void) {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static BOOL NET_ErrorWouldBlock(int error) {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

static void NET_CloseSocket(net_socket_t socket_handle) {
#ifdef _WIN32
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
}

static LPCSTR NET_SourceName(NETSOURCE netsrc) {
    return netsrc == NS_CLIENT ? "client" : "server";
}

static net_socket_t NET_UDPSocket(unsigned short port) {
    net_socket_t newsocket;
    int flag = 1;
    struct sockaddr_in addr;

    newsocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (newsocket == NET_INVALID_SOCKET) {
        fprintf(stderr, "NET_UDPSocket: socket creation failed: %d\n", NET_SocketError());
        return NET_INVALID_SOCKET;
    }

    setsockopt(newsocket, SOL_SOCKET, SO_BROADCAST, (const char *)&flag, sizeof(flag));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(newsocket, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "NET_UDPSocket: bind failed on port %u: %d\n", port, NET_SocketError());
        NET_CloseSocket(newsocket);
        return NET_INVALID_SOCKET;
    }

#ifdef _WIN32
    u_long nonblocking = 1;
    if (ioctlsocket(newsocket, FIONBIO, &nonblocking) == SOCKET_ERROR) {
        fprintf(stderr, "NET_UDPSocket: nonblocking mode failed: %d\n", NET_SocketError());
        NET_CloseSocket(newsocket);
        return NET_INVALID_SOCKET;
    }
#else
    flag = fcntl(newsocket, F_GETFL, 0);
    if (flag >= 0) {
        fcntl(newsocket, F_SETFL, flag | O_NONBLOCK);
    }
#endif

    if (port) {
        fprintf(stderr, "NET_UDPSocket: bound UDP 0.0.0.0:%u\n", (unsigned)port);
    } else {
        fprintf(stderr, "NET_UDPSocket: bound ephemeral client UDP port\n");
    }
    return newsocket;
}

static unsigned short NET_GamePort(void) {
    int port = Cvar_Integer("game_port", PORT_SERVER);

    if (port <= 0 || port > 65535) {
        fprintf(stderr,
                "Invalid game_port %d, using default %u\n",
                port,
                (unsigned)PORT_SERVER);
        port = PORT_SERVER;
    }
    return (unsigned short)port;
}

static void NET_OpenIP(NETSOURCE netsrc) {
    if (netsrc == NS_SERVER && udp_sockets[NS_SERVER] == NET_INVALID_SOCKET) {
        unsigned short port = NET_GamePort();
        fprintf(stderr, "NET_OpenIP: opening server socket on UDP port %u\n", (unsigned)port);
        udp_sockets[NS_SERVER] = NET_UDPSocket(port);
    }
    if (netsrc == NS_CLIENT && udp_sockets[NS_CLIENT] == NET_INVALID_SOCKET) {
        fprintf(stderr, "NET_OpenIP: opening client socket on ephemeral UDP port\n");
        udp_sockets[NS_CLIENT] = NET_UDPSocket(0);
    }
}

static void NET_SendUDPPacket(NETSOURCE netsrc, int length, const void *data, netadr_t to) {
    net_socket_t sock = udp_sockets[netsrc];

    if (sock == NET_INVALID_SOCKET) {
        fprintf(stderr,
                "NET_SendUDPPacket: %s socket is closed, dropping packet to %s\n",
                NET_SourceName(netsrc),
                NET_AdrToString(&to));
        return;
    }
    if (length <= 0 || length > MAX_MSGLEN) {
        fprintf(stderr, "NET_SendUDPPacket: bad packet length %d\n", length);
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    if (to.type == NA_BROADCAST) {
        addr.sin_addr.s_addr = INADDR_BROADCAST;
    } else {
        memcpy(&addr.sin_addr, to.ip, 4);
    }
    addr.sin_port = to.port;    // already in network byte order

    if (sendto(sock, (const char *)data, length, 0,
               (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr,
                "NET_SendUDPPacket: sendto %s failed: %d\n",
                NET_AdrToString(&to),
                NET_SocketError());
    }
}

static int NET_GetUDPPacket(NETSOURCE netsrc, netadr_t *from, LPSIZEBUF msg) {
    net_socket_t sock = udp_sockets[netsrc];

    if (sock == NET_INVALID_SOCKET)
        return 0;

    struct sockaddr_in srcaddr;
    net_socklen_t addrlen = sizeof(srcaddr);

    int bytes = recvfrom(sock, (char *)msg->data, msg->maxsize, 0,
                         (struct sockaddr *)&srcaddr, &addrlen);
    if (bytes == SOCKET_ERROR) {
        int const error = NET_SocketError();
        if (!NET_ErrorWouldBlock(error))
            fprintf(stderr, "NET_GetUDPPacket: recvfrom failed: %d\n", error);
        return 0;
    }
    if (bytes == msg->maxsize) {
        fprintf(stderr, "NET_GetUDPPacket: oversize packet from UDP socket\n");
        return 0;
    }

    msg->cursize = (DWORD)bytes;
    msg->readcount = 0;

    memset(from, 0, sizeof(*from));
    from->type = NA_IP;
    memcpy(from->ip, &srcaddr.sin_addr, 4);
    from->port = srcaddr.sin_port;  // keep in network byte order
    return bytes;
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

void NET_Init(void) {
#ifdef _WIN32
    WSADATA winsock_data;
    if (!winsock_initialized) {
        int const result = WSAStartup(MAKEWORD(2, 2), &winsock_data);
        if (result != 0) {
            fprintf(stderr, "NET_Init: WSAStartup failed: %d\n", result);
            return;
        }
        winsock_initialized = true;
    }
#endif
    memset(loopbufs, 0, sizeof(loopbufs));
}

void NET_Config(BOOL multiplayer) {
    if (!multiplayer) {
        FOR_LOOP(i, 2) {
            NET_ConfigSource((NETSOURCE)i, false);
        }
        return;
    }
    NET_ConfigSource(NS_SERVER, true);
    NET_ConfigSource(NS_CLIENT, true);
}

void NET_ConfigSource(NETSOURCE netsrc, BOOL open) {
    if (netsrc > NS_SERVER) {
        return;
    }
    if (!open) {
        if (udp_sockets[netsrc] != NET_INVALID_SOCKET) {
            fprintf(stderr, "NET_Config: closing %s UDP socket\n", NET_SourceName(netsrc));
            NET_CloseSocket(udp_sockets[netsrc]);
            udp_sockets[netsrc] = NET_INVALID_SOCKET;
        }
        return;
    }
    NET_OpenIP(netsrc);
}

BOOL NET_IsConfigured(NETSOURCE netsrc) {
    return netsrc <= NS_SERVER && udp_sockets[netsrc] != NET_INVALID_SOCKET;
}

void NET_Shutdown(void) {
    NET_Config(false);
#ifdef _WIN32
    if (winsock_initialized) {
        WSACleanup();
        winsock_initialized = false;
    }
#endif
}

bool NET_StringToAdr(LPCSTR s, unsigned short default_port, netadr_t *adr) {
    char host[256];
    unsigned short port = default_port;

    strncpy(host, s, sizeof(host) - 1);
    host[sizeof(host) - 1] = '\0';

    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
        port = (unsigned short)atoi(colon + 1);
    }

    memset(adr, 0, sizeof(*adr));
    if (!strcmp(s, "localhost")) {
        adr->type = NA_LOOPBACK;
        return true;
    }
    adr->type = NA_IP;
    adr->port = htons(port);

    if (inet_pton(AF_INET, host, adr->ip) > 0)
        return true;

    // Fall back to hostname resolution
    struct hostent *he = gethostbyname(host);
    if (!he || he->h_addrtype != AF_INET) {
        fprintf(stderr, "NET_StringToAdr: failed to resolve \"%s\"\n", host);
        return false;
    }
    memcpy(adr->ip, he->h_addr_list[0], 4);
    return true;
}

LPCSTR NET_AdrToString(const netadr_t *adr) {
    static char buffers[4][64];
    static DWORD index;
    char host[INET_ADDRSTRLEN] = "0.0.0.0";
    char *out = buffers[index++ & 3];

    if (!adr) {
        snprintf(out, sizeof(buffers[0]), "0.0.0.0:0");
        return out;
    }
    if (adr->type == NA_LOOPBACK) {
        snprintf(out, sizeof(buffers[0]), "loopback");
        return out;
    }
    inet_ntop(AF_INET, adr->ip, host, sizeof(host));
    snprintf(out, sizeof(buffers[0]), "%s:%u", host, ntohs(adr->port));
    return out;
}

// Route a packet to the loopback buffer or the UDP socket depending on
// the destination address type — the core of the Quake 2 network model.
void NET_SendPacket(NETSOURCE netsrc, int length, const void *data, netadr_t to) {
    switch (to.type) {
    case NA_LOOPBACK:
        NET_SendLoopPacket(netsrc, length, data);
        break;
    case NA_IP:
    case NA_BROADCAST:
        NET_SendUDPPacket(netsrc, length, data, to);
        break;
    default:
        break;
    }
}

// Check the loopback buffer first (zero latency for local clients), then
// fall through to the UDP socket for remote clients.
int NET_GetPacket(NETSOURCE netsrc, netadr_t *from, LPSIZEBUF msg) {
    int r = NET_GetLoopPacket(netsrc, from, msg);
    if (r)
        return r;
    return NET_GetUDPPacket(netsrc, from, msg);
}

void Netchan_Transmit(NETSOURCE netsrc, struct netchan *netchan) {
    if (netchan->message.cursize == 0)
        return;
    NET_SendPacket(netsrc, (int)netchan->message.cursize,
                   netchan->message_buf, netchan->remote_address);
    netchan->message.cursize = 0;
}

void SZ_Init(LPSIZEBUF buf, BYTE *data, DWORD length) {
    memset(buf, 0, sizeof(*buf));
    buf->data = data;
    buf->maxsize = length;
}

void SZ_Clear(LPSIZEBUF buf) {
    buf->cursize = 0;
    buf->overflowed = false;
}

HANDLE SZ_GetSpace(LPSIZEBUF buf, DWORD length) {
    if (buf->cursize + length > buf->maxsize) {
//        if (length > buf->maxsize)
//            Com_Error (ERR_FATAL, "SZ_GetSpace: %i is > full buffer size", length);
        fprintf(stderr,
                "SZ_GetSpace: overflow length=%u cursize=%u maxsize=%u\n",
                (unsigned)length,
                (unsigned)buf->cursize,
                (unsigned)buf->maxsize);
        buf->overflowed = true;
        SZ_Clear(buf);
        buf->overflowed = true;
    }
    HANDLE data = buf->data + buf->cursize;
    buf->cursize += length;
    return data;
}

void SZ_Write(LPSIZEBUF buf, void const *data, DWORD length) {
    memcpy(SZ_GetSpace(buf, length), data, length);
}

void Netchan_OutOfBand(NETSOURCE netsrc, netadr_t adr, DWORD length, BYTE *data) {
    BYTE send_buf[MAX_MSGLEN];
    sizeBuf_t send;

    SZ_Init(&send, send_buf, sizeof(send_buf));
    MSG_WriteLong(&send, -1);       // -1 sequence marks an out-of-band packet
    SZ_Write(&send, data, length);

    NET_SendPacket(netsrc, (int)send.cursize, send.data, adr);
}

void Netchan_OutOfBandPrint(NETSOURCE netsrc, netadr_t adr, LPCSTR format, ...) {
    va_list argptr;
    static char string[MAX_MSGLEN - 4];
    va_start(argptr, format);
    vsnprintf(string, sizeof(string), format, argptr);
    va_end(argptr);
    string[sizeof(string) - 1] = '\0';
    Netchan_OutOfBand(netsrc, adr, (DWORD)strlen(string), (BYTE *)string);
}
