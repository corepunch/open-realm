#ifdef WC3_SC2API

#include "sc2api_transport.h"
#include "../../../../common/net_platform.h"

#define SC2API_HTTP_LIMIT 8192u
#define SC2API_MESSAGE_LIMIT (16u * 1024u * 1024u)
#define SC2API_RX_CAPACITY (SC2API_MESSAGE_LIMIT + SC2API_HTTP_LIMIT + 32u)
#define SC2API_TX_CAPACITY (32u * 1024u * 1024u)

static struct {
    net_socket_t listener;
    net_socket_t client;
    BOOL initialized;
    BOOL websocket;
    LPBYTE rx;
    DWORD rx_size;
    LPBYTE tx;
    DWORD tx_size;
    LPBYTE message;
    DWORD message_size;
    BYTE fragment_opcode;
} transport;

static void transport_close_socket(net_socket_t *socket) {
    if (!socket || *socket == NET_INVALID_SOCKET) return;
#ifdef _WIN32
    closesocket(*socket);
#else
    close(*socket);
#endif
    *socket = NET_INVALID_SOCKET;
}

static BOOL transport_would_block(void) {
#ifdef _WIN32
    int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN;
#endif
}

static BOOL transport_set_nonblocking(net_socket_t socket) {
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
    transport.rx_size = 0;
    transport.tx_size = 0;
    transport.message_size = 0;
    transport.fragment_opcode = 0;
}

static BOOL transport_queue_raw(void const *data, DWORD size) {
    if (!size) return true;
    if (!transport.tx || size > SC2API_TX_CAPACITY - transport.tx_size) return false;
    memcpy(transport.tx + transport.tx_size, data, size);
    transport.tx_size += size;
    return true;
}

static void transport_flush(void) {
    while (transport.client != NET_INVALID_SOCKET && transport.tx_size) {
        int sent = send(transport.client, (char const *)transport.tx,
                        (int)MIN(transport.tx_size, (DWORD)INT_MAX), 0);
        if (sent > 0) {
            transport.tx_size -= (DWORD)sent;
            if (transport.tx_size) memmove(transport.tx, transport.tx + sent, transport.tx_size);
            continue;
        }
        if (sent < 0 && transport_would_block()) return;
        transport_disconnect();
        return;
    }
}

static BOOL transport_queue_frame(BYTE opcode, BYTE const *data, DWORD size) {
    BYTE header[10];
    DWORD header_size = 0;

    header[header_size++] = 0x80 | (opcode & 0x0f);
    if (size < 126) {
        header[header_size++] = (BYTE)size;
    } else if (size <= UINT16_MAX) {
        header[header_size++] = 126;
        header[header_size++] = (BYTE)(size >> 8);
        header[header_size++] = (BYTE)size;
    } else {
        uint64_t length = size;
        header[header_size++] = 127;
        FOR_LOOP(i, 8) header[header_size++] = (BYTE)(length >> (56 - i * 8));
    }
    return transport_queue_raw(header, header_size) && transport_queue_raw(data, size);
}

/* Minimal SHA-1 implementation used only for the RFC 6455 opening handshake. */
typedef struct {
    uint32_t h[5];
    uint64_t bits;
    BYTE block[64];
    DWORD used;
} sc2Sha1_t;

static uint32_t sha1_rotl(uint32_t value, DWORD shift) {
    return (value << shift) | (value >> (32 - shift));
}

static void sha1_transform(sc2Sha1_t *ctx, BYTE const block[64]) {
    uint32_t w[80], a, b, c, d, e;
    FOR_LOOP(i, 16) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               block[i * 4 + 3];
    }
    for (DWORD i = 16; i < 80; i++) w[i] = sha1_rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
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

static void sha1_update(sc2Sha1_t *ctx, void const *data, DWORD size) {
    BYTE const *bytes = data;
    ctx->bits += (uint64_t)size * 8;
    while (size) {
        DWORD copy = MIN(size, 64 - ctx->used);
        memcpy(ctx->block + ctx->used, bytes, copy);
        ctx->used += copy; bytes += copy; size -= copy;
        if (ctx->used == 64) { sha1_transform(ctx, ctx->block); ctx->used = 0; }
    }
}

static void sha1_final(sc2Sha1_t *ctx, BYTE digest[20]) {
    uint64_t bits = ctx->bits;
    BYTE pad = 0x80, zero = 0;
    sha1_update(ctx, &pad, 1);
    while (ctx->used != 56) sha1_update(ctx, &zero, 1);
    BYTE length[8];
    FOR_LOOP(i, 8) length[i] = (BYTE)(bits >> (56 - i * 8));
    sha1_update(ctx, length, 8);
    FOR_LOOP(i, 5) {
        digest[i * 4] = (BYTE)(ctx->h[i] >> 24);
        digest[i * 4 + 1] = (BYTE)(ctx->h[i] >> 16);
        digest[i * 4 + 2] = (BYTE)(ctx->h[i] >> 8);
        digest[i * 4 + 3] = (BYTE)ctx->h[i];
    }
}

static void transport_base64(BYTE const *data, DWORD size, LPSTR out, DWORD out_size) {
    static char const alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    DWORD pos = 0;
    for (DWORD i = 0; i < size && pos + 4 < out_size; i += 3) {
        DWORD remain = size - i;
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

static LPCSTR transport_find_header(LPCSTR request, LPCSTR name) {
    size_t name_len = strlen(name);
    LPCSTR line = request;
    while (line && *line) {
        LPCSTR next = strstr(line, "\r\n");
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

static BOOL transport_handshake(void) {
    LPCSTR end, key;
    char key_value[256], source[320], accept[64], response[512];
    BYTE digest[20];
    sc2Sha1_t sha;
    DWORD key_len;

    if (transport.rx_size >= SC2API_HTTP_LIMIT) return false;
    transport.rx[transport.rx_size] = '\0';
    end = strstr((LPCSTR)transport.rx, "\r\n\r\n");
    if (!end) return true;
    if (strncmp((LPCSTR)transport.rx, "GET /sc2api ", 12)) return false;
    key = transport_find_header((LPCSTR)transport.rx, "Sec-WebSocket-Key");
    if (!key) return false;
    key_len = 0;
    while (key[key_len] && key[key_len] != '\r' && key[key_len] != '\n' && key_len + 1 < sizeof(key_value)) key_len++;
    memcpy(key_value, key, key_len); key_value[key_len] = '\0';
    snprintf(source, sizeof(source), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key_value);
    sha1_init(&sha); sha1_update(&sha, source, (DWORD)strlen(source)); sha1_final(&sha, digest);
    transport_base64(digest, sizeof(digest), accept, sizeof(accept));
    snprintf(response, sizeof(response),
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
    if (!transport_queue_raw(response, (DWORD)strlen(response))) return false;

    DWORD consumed = (DWORD)((end + 4) - (LPCSTR)transport.rx);
    transport.rx_size -= consumed;
    if (transport.rx_size) memmove(transport.rx, transport.rx + consumed, transport.rx_size);
    transport.websocket = true;
    return true;
}

static BOOL transport_consume_frames(wc3Sc2TransportMessageFn on_message) {
    while (transport.rx_size >= 2) {
        BYTE first = transport.rx[0], second = transport.rx[1];
        BOOL fin = !!(first & 0x80), masked = !!(second & 0x80);
        BYTE opcode = first & 0x0f;
        uint64_t payload_size = second & 0x7f;
        DWORD header_size = 2;
        BYTE mask[4];

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
        if (!masked || payload_size > SC2API_MESSAGE_LIMIT) return false;
        if (transport.rx_size < header_size + 4 || payload_size > UINT32_MAX) return true;
        memcpy(mask, transport.rx + header_size, 4); header_size += 4;
        if (transport.rx_size < header_size + (DWORD)payload_size) return true;
        LPBYTE payload = transport.rx + header_size;
        FOR_LOOP(i, (DWORD)payload_size) payload[i] ^= mask[i & 3];

        if (opcode == 8) return false;
        if (opcode == 9) {
            if (!transport_queue_frame(10, payload, (DWORD)payload_size)) return false;
        } else if (opcode == 2 || opcode == 0) {
            if (opcode == 2) {
                if (transport.fragment_opcode || transport.message_size) return false;
                transport.fragment_opcode = 2;
            } else if (!transport.fragment_opcode) {
                return false;
            }
            if (payload_size > SC2API_MESSAGE_LIMIT - transport.message_size) return false;
            memcpy(transport.message + transport.message_size, payload, (DWORD)payload_size);
            transport.message_size += (DWORD)payload_size;
            if (fin) {
                if (on_message) on_message(transport.message, transport.message_size);
                transport.message_size = 0;
                transport.fragment_opcode = 0;
            }
        } else if (opcode != 10) {
            return false;
        }

        DWORD consumed = header_size + (DWORD)payload_size;
        transport.rx_size -= consumed;
        if (transport.rx_size) memmove(transport.rx, transport.rx + consumed, transport.rx_size);
    }
    return true;
}

static BOOL transport_init(void) {
    struct sockaddr_in address;
    LPCSTR listen_ip = gi.CvarString("sc2_api_listen", "127.0.0.1");
    LONG port = atoi(gi.CvarString("sc2_api_port", "5000"));
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
    setsockopt(transport.listener, SOL_SOCKET, SO_REUSEADDR, (char const *)&reuse, sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((USHORT)port);
    if (inet_pton(AF_INET, listen_ip, &address.sin_addr) != 1 ||
        bind(transport.listener, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR ||
        listen(transport.listener, 1) == SOCKET_ERROR ||
        !transport_set_nonblocking(transport.listener)) {
        WC3_SC2API_TransportShutdown();
        return false;
    }
    return true;
}

BOOL WC3_SC2API_TransportPoll(wc3Sc2TransportMessageFn on_message) {
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
        int received = recv(transport.client, (char *)transport.rx + transport.rx_size,
                            (int)MIN(SC2API_RX_CAPACITY - 1 - transport.rx_size, (DWORD)INT_MAX), 0);
        if (received > 0) { transport.rx_size += (DWORD)received; continue; }
        if (received < 0 && transport_would_block()) break;
        transport_disconnect();
        return true;
    }
    if (!transport.websocket) {
        if (!transport_handshake()) transport_disconnect();
    }
    if (transport.websocket && !transport_consume_frames(on_message)) transport_disconnect();
    transport_flush();
    return true;
}

BOOL WC3_SC2API_TransportSend(BYTE const *data, DWORD size) {
    if (!transport.websocket || transport.client == NET_INVALID_SOCKET || size > SC2API_MESSAGE_LIMIT) return false;
    if (!transport_queue_frame(2, data, size)) return false;
    transport_flush();
    return transport.client != NET_INVALID_SOCKET;
}

BOOL WC3_SC2API_TransportConnected(void) {
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
