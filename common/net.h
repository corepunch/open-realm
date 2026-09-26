#ifndef net_h
#define net_h

#include "common/rle.h"

#define MAX_MSGLEN 256 * 1024

#define BZ_LOADING_HEADER_SIZE 13 // bytes; opcode and three 32-bit lengths; bounds loading-screen chunks

#define BZ_XSTR(x) BZ_STR(x)
#define BZ_STR(x) #x

#ifdef WOW
#define PORT_SERVER 27911
#else
#define PORT_SERVER 27910
#endif
#define PORT_SERVER_STRING BZ_XSTR(PORT_SERVER)
#define BZ_PROTOCOL_VERSION 12 // version; svc_sound request identity for playback feedback


typedef struct entityState_s entityState_t;

typedef enum {
    NA_LOOPBACK,
    NA_BROADCAST,
    NA_IP,
    NA_IPX,
    NA_BROADCAST_IPX
} netadrtype_t;

typedef enum {
    NS_CLIENT, NS_SERVER
} NETSOURCE;

typedef struct sizeBuf_s {
    uint8_t *data;
    uint32_t maxsize;
    uint32_t cursize;
    uint32_t readcount;
    bool overflowed;
} sizeBuf_t;

typedef struct {
    netadrtype_t type;
    uint8_t ip[4];
    uint8_t ipx[10];
    unsigned short port;        // stored in network byte order
} netadr_t;

struct netchan {
    netadr_t remote_address;    // where packets are sent/expected from
    sizeBuf_t message;
    uint8_t message_buf[MAX_MSGLEN];
};

// Initialise loopback state. UDP sockets are opened lazily by NET_Config().
void NET_Init(void);
void NET_Config(bool multiplayer);
void NET_ConfigSource(NETSOURCE netsrc, bool open);
bool NET_IsConfigured(NETSOURCE netsrc);
void NET_Shutdown(void);

// Parse "host" or "host:port" into a netadr_t.  default_port is used
// when no port is present in the string.  Returns true on success.
bool NET_StringToAdr(cstring_t s, unsigned short default_port, netadr_t *adr);
cstring_t NET_AdrToString(netadr_t const *adr);

// Send a packet.  Routes to the loopback buffer (NA_LOOPBACK) or the
// UDP socket (NA_IP / NA_BROADCAST) based on to.type.
void NET_SendPacket(NETSOURCE netsrc, int length, void const *data, netadr_t to);

// Receive one packet.  Checks the loopback buffer first, then the UDP
// socket.  Fills *from with the sender's address.  Returns packet size
// (> 0) on success, 0 when no packet is available.
int NET_GetPacket(NETSOURCE netsrc, netadr_t *from, sizeBuf_t *msg);
int NET_GetLoopPacket(NETSOURCE netsrc, netadr_t *from, sizeBuf_t *msg);

void Netchan_Transmit(NETSOURCE netsrc, struct netchan *netchan);
void Netchan_OutOfBand(NETSOURCE netsrc, netadr_t adr, uint32_t length, uint8_t *data);
void Netchan_OutOfBandPrint(NETSOURCE netsrc, netadr_t adr, cstring_t format, ...);

void MSG_Write(sizeBuf_t *buf, void const *value, uint32_t size);
void MSG_WriteByte(sizeBuf_t *buf, int value);
void MSG_WriteShort(sizeBuf_t *buf, int value);
void MSG_WriteLong(sizeBuf_t *buf, int value);
void MSG_WriteFloat(sizeBuf_t *buf, float value);
void MSG_WriteFloat2(sizeBuf_t *buf, float value);
void MSG_WriteString(sizeBuf_t *buf, cstring_t value);
void MSG_WriteDeltaEntity(sizeBuf_t *buf, entityState_t const *from, entityState_t const *to, bool force);
void MSG_WriteDeltaUIFrame(sizeBuf_t *msg, uiFrame_t const *from, uiFrame_t const *to, bool force);
void MSG_WriteDeltaUIWindowFrame(sizeBuf_t *msg, uiFrame_t const *from, uiFrame_t const *to, bool force);
void MSG_WriteDeltaPlayerState(sizeBuf_t *msg, player_t const *from, player_t const *to);
void MSG_WriteEntityBits(sizeBuf_t *buf, uint32_t bits, uint32_t number);
void MSG_WritePlayerBits(sizeBuf_t *buf, uint32_t bits, uint32_t number);
void MSG_WriteInput(sizeBuf_t *buf, inputCmd_t const *cmd);
bool MSG_ReadInput(sizeBuf_t *buf, inputCmd_t *cmd);
void MSG_WritePos(sizeBuf_t *buf, vector3_t const *pos);
void MSG_WriteDir(sizeBuf_t *buf, vector3_t const *dir);
void MSG_WriteAngle(sizeBuf_t *buf, float f);

int MSG_Read(sizeBuf_t *buf, handle_t value, uint32_t size);
int MSG_ReadByte(sizeBuf_t *buf);
int MSG_ReadShort(sizeBuf_t *buf);
int MSG_ReadLong(sizeBuf_t *buf);
float MSG_ReadFloat(sizeBuf_t *buf);
void MSG_ReadString(sizeBuf_t *buf, string_t value);
void MSG_ReadStringN(sizeBuf_t *buf, string_t value, int maxlen);
void MSG_ReadPos(sizeBuf_t *buf, vector3_t *pos);
void MSG_ReadDir(sizeBuf_t *buf, vector3_t *dir);
float MSG_ReadAngle(sizeBuf_t *buf);
cstring_t MSG_ReadString2(sizeBuf_t *buf);
void MSG_ReadDeltaEntity(sizeBuf_t *buf, entityState_t *edict, int number, int bits);
void MSG_ReadDeltaUIFrame(sizeBuf_t *msg, uiFrame_t *edict, int number, int bits);
bool MSG_ReadDeltaUIWindowFrame(sizeBuf_t *msg, uiFrame_t *edict, int number, int bits);
void MSG_ReadDeltaPlayerState(sizeBuf_t *msg, player_t *edict, int number, int bits);
int MSG_ReadEntityBits(sizeBuf_t *msg, uint32_t *bits);
int MSG_ReadPlayerBits(sizeBuf_t *msg, uint32_t *bits);

handle_t SZ_GetSpace(sizeBuf_t *buf, uint32_t length);
void SZ_Write(sizeBuf_t *buf, void const *data, uint32_t length);
void SZ_Printf(sizeBuf_t *msg, cstring_t fmt, ...);
void SZ_Init(sizeBuf_t *buf, uint8_t *data, uint32_t length);
void SZ_Clear(sizeBuf_t *buf);

#endif
