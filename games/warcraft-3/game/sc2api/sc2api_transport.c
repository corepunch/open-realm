#ifdef WC3_SC2API

#include "sc2api_transport.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET net_socket_t;
typedef int net_socklen_t;
#define NET_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int net_socket_t;
typedef socklen_t net_socklen_t;
#define NET_INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

#define SC2API_HTTP_LIMIT 8192u
#define SC2API_MESSAGE_LIMIT (16u * 1024u * 1024u)
#define SC2API_RX_CAPACITY (SC2API_MESSAGE_LIMIT + SC2API_HTTP_LIMIT + 32u)
#define SC2API_TX_CAPACITY (32u * 1024u * 1024u)

static struct {
    net_socket_t listener;
    net_socket_t client;
    bool initialized;
    bool websocket;
    bool closing;
    uint8_t *rx;
    uint32_t rx_size;
    uint8_t *tx;
    uint32_t tx_size;
    uint8_t *message;
    uint32_t message_size;
    uint8_t fragment_opcode;
} transport = { .listener = NET_INVALID_SOCKET, .client = NET_INVALID_SOCKET };

static void transport_close_socket(net_socket_t *socket) {
    if (!socket || *socket == NET_INVALID_SOCKET) return;
#ifdef _WIN32
    closesocket(*socket);
#else
    close(*socket);
#endif
    * socket = NET_INVALID_SOCKET;
}

static bool transport_would_block(void) {
#ifdef _WIN32
    int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN;
#endif
}

static bool transport_set_nonblocking(net_socket_t socket) {
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe))) return false;
#endif
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static void transport_disconnect(void) {
    transport_close_socket(&transport.client);
    transport.websocket = false;
    transport.closing = false;
    transport.rx_size = 0;
    transport.tx_size = 0;
    transport.message_size = 0;
    transport.fragment_opcode = 0;
}

static bool transport_queue_raw(void const *data, uint32_t size) {
    if (!size) return true;
    if (!transport.tx || size > SC2API_TX_CAPACITY - transport.tx_size) return false;
    memcpy(transport.tx + transport.tx_size, data, size);
    transport.tx_size += size;
    return true;
}

static void transport_flush(void) {
    while (transport.client != NET_INVALID_SOCKET && transport.tx_size) {
#ifdef MSG_NOSIGNAL
        int const send_flags = MSG_NOSIGNAL;
#else
        int const send_flags = 0;
#endif
        int sent = send(transport.client, (cstring_t)transport.tx,
                        (int)MIN(transport.tx_size, (uint32_t)INT_MAX), send_flags);
        if (sent > 0) {
            transport.tx_size -= (uint32_t)sent;
            if (transport.tx_size) memmove(transport.tx, transport.tx + sent, transport.tx_size);
            continue;
        }
        if (sent < 0 && transport_would_block()) return;
        transport_disconnect();
        return;
    }
}

static bool transport_queue_frame(uint8_t opcode, uint8_t const *data, uint32_t size) {
    uint8_t header[10];
    uint32_t header_size = 0;

    header[header_size++] = 0x80 | (opcode & 0x0f);
    if (size < 126) {
        header[header_size++] = (uint8_t)size;
    } else if (size <= UINT16_MAX) {
        header[header_size++] = 126;
        header[header_size++] = (uint8_t)(size >> 8);
        header[header_size++] = (uint8_t)size;
    } else {
        uint64_t length = size;
        header[header_size++] = 127;
        FOR_LOOP(i, 8) header[header_size++] = (uint8_t)(length >> (56 - i * 8));
    }
    if (transport.tx_size > SC2API_TX_CAPACITY || header_size > SC2API_TX_CAPACITY - transport.tx_size ||
        size > SC2API_TX_CAPACITY - transport.tx_size - header_size) return false;
    return transport_queue_raw(header, header_size) && transport_queue_raw(data, size);
}

/* Minimal SHA-1 implementation used only for the RFC 6455 opening handshake. */
typedef struct {
    uint32_t h[5];
    uint64_t bits;
    uint8_t block[64];
    uint32_t used;
} sc2Sha1_t;

static uint32_t sha1_rotl(uint32_t value, uint32_t shift) {
    return (value << shift) | (value >> (32 - shift));
}

static void sha1_transform(sc2Sha1_t *ctx, uint8_t const block[64]) {
    uint32_t w[80], a, b, c, d, e;
    FOR_LOOP(i, 16) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               block[i * 4 + 3];
    }
    for (uint32_t i = 16; i < 80; i++) w[i] = sha1_rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = ctx->h[0]; b = ctx->h[1]; c = ctx->h[2]; d = ctx->h[3]; e = ctx->h[4];
    FOR_LOOP(i, 80) {
        uint32_t f, k, temp;
        if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5a827999u; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdcu; }
        else { f = b ^ c ^ d; k = 0xca62c1d6u; }
        temp = sha1_rotl(a, 5) + f + e + k + w[i];
        e = d; d = c; c = sha1_rotl(b, 30); b = a; a = temp;
    }
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d; ctx->h[4] += e;
}

static void sha1_init(sc2Sha1_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->h[0] = 0x67452301u; ctx->h[1] = 0xefcdab89u; ctx->h[2] = 0x98badcfeu;
    ctx->h[3] = 0x10325476u; ctx->h[4] = 0xc3d2e1f0u;
}

static void sha1_update(sc2Sha1_t *ctx, void const *data, uint32_t size) {
    uint8_t const *bytes = data;
    ctx->bits += (uint64_t)size * 8;
    while (size) {
        uint32_t copy = MIN(size, 64 - ctx->used);
        memcpy(ctx->block + ctx->used, bytes, copy);
        ctx->used += copy; bytes += copy; size -= copy;
        if (ctx->used == 64) { sha1_transform(ctx, ctx->block); ctx->used = 0; }
    }
}

static void sha1_final(sc2Sha1_t *ctx, uint8_t digest[20]) {
    uint64_t bits = ctx->bits;
    uint8_t pad = 0x80, zero = 0;
    sha1_update(ctx, &pad, 1);
    while (ctx->used != 56) sha1_update(ctx, &zero, 1);
    uint8_t length[8];
    FOR_LOOP(i, 8) length[i] = (uint8_t)(bits >> (56 - i * 8));
    sha1_update(ctx, length, 8);
    FOR_LOOP(i, 5) {
        digest[i * 4] = (uint8_t)(ctx->h[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->h[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->h[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)ctx->h[i];
    }
}

static void transport_base64(uint8_t const *data, uint32_t size, string_t out, uint32_t out_size) {
    static char const alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t pos = 0;
    for (uint32_t i = 0; i < size && pos + 4 < out_size; i += 3) {
        uint32_t remain = size - i;
        uint32_t value = (uint32_t)data[i] << 16;
        if (remain > 1) value |= (uint32_t)data[i + 1] << 8;
        if (remain > 2) value |= data[i + 2];
        out[pos++] = alphabet[(value >> 18) & 63];
        out[pos++] = alphabet[(value >> 12) & 63];
        out[pos++] = remain > 1 ? alphabet[(value >> 6) & 63] : '=';
        out[pos++] = remain > 2 ? alphabet[value & 63] : '=';
    }
    if (out_size) out[MIN(pos, out_size - 1)] = '\0';
}

static cstring_t transport_find_header(cstring_t request, cstring_t name) {
    size_t name_len = strlen(name);
    cstring_t line = request;
    while (line && *line) {
        cstring_t next = strstr(line, "\r\n");
        if (!strncasecmp(line, name, name_len) && line[name_len] == ':') {
            line += name_len + 1;
            while (*line == ' ' || *line == '\t') line++;
            return line;
        }
        if (!next || next == line) break;
        line = next + 2;
    }
    return NULL;
}

static bool transport_handshake(void) {
    cstring_t end, key;
    char key_value[256], source[320], accept[64], response[512];
    uint8_t digest[20];
    sc2Sha1_t sha;
    uint32_t key_len;

    if (transport.rx_size >= SC2API_HTTP_LIMIT) return false;
    transport.rx[transport.rx_size] = '\0';
    end = strstr((cstring_t)transport.rx, "\r\n\r\n");
    if (!end) return true;
    if (strncmp((cstring_t)transport.rx, "GET /sc2api ", 12)) return false;
    key = transport_find_header((cstring_t)transport.rx, "Sec-WebSocket-Key");
    if (!key) return false;
    cstring_t version = transport_find_header((cstring_t)transport.rx, "Sec-WebSocket-Version");
    cstring_t upgrade = transport_find_header((cstring_t)transport.rx, "Upgrade");
    cstring_t connection = transport_find_header((cstring_t)transport.rx, "Connection");
    if (!version || strncmp(version, "13\r\n", 4) || !upgrade ||
        strncasecmp(upgrade, "websocket\r\n", 11) || !connection) return false;
    cstring_t connection_end = strstr(connection, "\r\n");
    bool has_upgrade = false;
    for (cstring_t token = connection; connection_end && token + 7 <= connection_end; token++)
        if (!strncasecmp(token, "upgrade", 7)) { has_upgrade = true; break; }
    if (!has_upgrade) return false;
    key_len = 0;
    while (key[key_len] && key[key_len] != '\r' && key[key_len] != '\n' && key_len + 1 < sizeof(key_value)) key_len++;
    if (key_len != 24 || key[22] != '=' || key[23] != '=') return false;
    memcpy(key_value, key, key_len); key_value[key_len] = '\0';
    snprintf(source, sizeof(source), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key_value);
    sha1_init(&sha); sha1_update(&sha, source, (uint32_t)strlen(source)); sha1_final(&sha, digest);
    transport_base64(digest, sizeof(digest), accept, sizeof(accept));
    snprintf(response, sizeof(response),
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
    if (!transport_queue_raw(response, (uint32_t)strlen(response))) return false;

    uint32_t consumed = (uint32_t)((end + 4) - (cstring_t)transport.rx);
    transport.rx_size -= consumed;
    if (transport.rx_size) memmove(transport.rx, transport.rx + consumed, transport.rx_size);
    transport.websocket = true;
    return true;
}

static bool transport_consume_frames(wc3Sc2TransportMessageFn on_message) {
    while (!transport.closing && transport.rx_size >= 2) {
        uint8_t first = transport.rx[0], second = transport.rx[1];
        bool fin = !!(first & 0x80), masked = !!(second & 0x80);
        uint8_t opcode = first & 0x0f;
        uint64_t payload_size = second & 0x7f;
        uint32_t header_size = 2;
        uint8_t mask[4];

        if (payload_size == 126) {
            if (transport.rx_size < 4) return true;
            payload_size = ((uint64_t)transport.rx[2] << 8) | transport.rx[3];
            header_size = 4;
        } else if (payload_size == 127) {
            if (transport.rx_size < 10) return true;
            payload_size = 0;
            FOR_LOOP(i, 8) payload_size = (payload_size << 8) | transport.rx[2 + i];
            header_size = 10;
        }
        if ((first & 0x70) || !masked || payload_size > SC2API_MESSAGE_LIMIT) return false;
        if ((opcode & 8) && (!fin || payload_size > 125)) return false;
        if (transport.rx_size < header_size + 4 || payload_size > UINT32_MAX) return true;
        memcpy(mask, transport.rx + header_size, 4); header_size += 4;
        if (transport.rx_size < header_size + (uint32_t)payload_size) return true;
        uint8_t *payload = transport.rx + header_size;
        FOR_LOOP(i, (uint32_t)payload_size) payload[i] ^= mask[i & 3];

        if (opcode == 8) {
            if (payload_size == 1 || !transport_queue_frame(8, payload, (uint32_t)payload_size)) return false;
            transport.closing = true;
        }
        else if (opcode == 9) {
            if (!transport_queue_frame(10, payload, (uint32_t)payload_size)) return false;
        } else if (opcode == 2 || opcode == 0) {
            if (opcode == 2) {
                if (transport.fragment_opcode || transport.message_size) return false;
                transport.fragment_opcode = 2;
            } else if (!transport.fragment_opcode) {
                return false;
            }
            if (payload_size > SC2API_MESSAGE_LIMIT - transport.message_size) return false;
            memcpy(transport.message + transport.message_size, payload, (uint32_t)payload_size);
            transport.message_size += (uint32_t)payload_size;
            if (fin) {
                if (on_message) on_message(transport.message, transport.message_size);
                transport.message_size = 0;
                transport.fragment_opcode = 0;
            }
        } else if (opcode != 10) {
            return false;
        }

        uint32_t consumed = header_size + (uint32_t)payload_size;
        transport.rx_size -= consumed;
        if (transport.rx_size) memmove(transport.rx, transport.rx + consumed, transport.rx_size);
    }
    return true;
}

static bool transport_init(void) {
    struct sockaddr_in address;
    cstring_t listen_ip = gi.CvarString("sc2_api_listen", "127.0.0.1");
    int32_t port = atoi(gi.CvarString("sc2_api_port", "5000"));
    int reuse = 1;

    if (transport.initialized) return transport.listener != NET_INVALID_SOCKET;
    memset(&transport, 0, sizeof(transport));
    transport.listener = transport.client = NET_INVALID_SOCKET;
    transport.initialized = true;
    if (port <= 0 || port > 65535) { WC3_SC2API_TransportShutdown(); return false; }
    transport.rx = gi.MemAlloc(SC2API_RX_CAPACITY);
    transport.tx = gi.MemAlloc(SC2API_TX_CAPACITY);
    transport.message = gi.MemAlloc(SC2API_MESSAGE_LIMIT);
    if (!transport.rx || !transport.tx || !transport.message) { WC3_SC2API_TransportShutdown(); return false; }
    transport.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (transport.listener == NET_INVALID_SOCKET) { WC3_SC2API_TransportShutdown(); return false; }
    setsockopt(transport.listener, SOL_SOCKET, SO_REUSEADDR, (cstring_t)&reuse, sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, listen_ip, &address.sin_addr) != 1 ||
        bind(transport.listener, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR ||
        listen(transport.listener, 1) == SOCKET_ERROR ||
        !transport_set_nonblocking(transport.listener)) {
        WC3_SC2API_TransportShutdown();
        return false;
    }
    return true;
}

bool WC3_SC2API_TransportPoll(wc3Sc2TransportMessageFn on_message) {
    if (!transport_init()) return false;
    if (transport.client == NET_INVALID_SOCKET) {
        struct sockaddr_in address;
        net_socklen_t length = sizeof(address);
        net_socket_t client = accept(transport.listener, (struct sockaddr *)&address, &length);
        if (client != NET_INVALID_SOCKET) {
            if (!transport_set_nonblocking(client)) transport_close_socket(&client);
            else { transport.client = client; transport.websocket = false; transport.rx_size = transport.tx_size = 0; }
        }
    }
    if (transport.client == NET_INVALID_SOCKET) return true;
    transport_flush();
    if (transport.client == NET_INVALID_SOCKET) return true;

    while (transport.rx_size < SC2API_RX_CAPACITY - 1) {
        int received = recv(transport.client, (string_t)transport.rx + transport.rx_size,
                            (int)MIN(SC2API_RX_CAPACITY - 1 - transport.rx_size, (uint32_t)INT_MAX), 0);
        if (received > 0) { transport.rx_size += (uint32_t)received; continue; }
        if (received < 0 && transport_would_block()) break;
        transport_disconnect();
        return true;
    }
    if (!transport.websocket) {
        if (!transport_handshake()) transport_disconnect();
    }
    if (transport.websocket && !transport_consume_frames(on_message)) transport_disconnect();
    transport_flush();
    if (transport.closing && !transport.tx_size) transport_disconnect();
    return true;
}

#ifdef BZ_TESTS
static bool (*sc2_transport_test_send)(uint8_t const *, uint32_t);
#endif

bool WC3_SC2API_TransportSend(uint8_t const *data, uint32_t size) {
#ifdef BZ_TESTS
    if (sc2_transport_test_send) return sc2_transport_test_send(data, size);
#endif
    if (!transport.websocket || transport.client == NET_INVALID_SOCKET || size > SC2API_MESSAGE_LIMIT) return false;
    if (!transport_queue_frame(2, data, size)) { transport_disconnect(); return false; }
    transport_flush();
    return transport.client != NET_INVALID_SOCKET;
}

bool WC3_SC2API_TransportConnected(void) {
    return transport.websocket && transport.client != NET_INVALID_SOCKET;
}

void WC3_SC2API_TransportShutdown(void) {
    transport_close_socket(&transport.client);
    transport_close_socket(&transport.listener);
    SAFE_DELETE(transport.rx, gi.MemFree);
    SAFE_DELETE(transport.tx, gi.MemFree);
    SAFE_DELETE(transport.message, gi.MemFree);
    memset(&transport, 0, sizeof(transport));
    transport.listener = transport.client = NET_INVALID_SOCKET;
}

#endif
