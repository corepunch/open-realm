#ifndef net_h
#define net_h

#define MAX_MSGLEN 256 * 1024

#define BZ_XSTR(x) BZ_STR(x)
#define BZ_STR(x) #x

#ifdef WOW
#define PORT_SERVER 27911
#else
#define PORT_SERVER 27910
#endif
#define PORT_SERVER_STRING BZ_XSTR(PORT_SERVER)
#define BZ_PROTOCOL_VERSION 1

typedef void const *LPCVOID;
typedef struct sizeBuf_s *LPSIZEBUF;
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
    LPBYTE data;
    DWORD maxsize;
    DWORD cursize;
    DWORD readcount;
    BOOL overflowed;
} sizeBuf_t;

typedef struct {
    netadrtype_t type;
    BYTE ip[4];
    BYTE ipx[10];
    unsigned short port;        // stored in network byte order
} netadr_t;

struct netchan {
    netadr_t remote_address;    // where packets are sent/expected from
    sizeBuf_t message;
    BYTE message_buf[MAX_MSGLEN];
};

// Initialise loopback state. UDP sockets are opened lazily by NET_Config().
void NET_Init(void);
void NET_Config(BOOL multiplayer);
void NET_ConfigSource(NETSOURCE netsrc, BOOL open);
BOOL NET_IsConfigured(NETSOURCE netsrc);
void NET_Shutdown(void);

// Parse "host" or "host:port" into a netadr_t.  default_port is used
// when no port is present in the string.  Returns true on success.
bool NET_StringToAdr(LPCSTR s, unsigned short default_port, netadr_t *adr);
LPCSTR NET_AdrToString(const netadr_t *adr);

// Send a packet.  Routes to the loopback buffer (NA_LOOPBACK) or the
// UDP socket (NA_IP / NA_BROADCAST) based on to.type.
void NET_SendPacket(NETSOURCE netsrc, int length, const void *data, netadr_t to);

// Receive one packet.  Checks the loopback buffer first, then the UDP
// socket.  Fills *from with the sender's address.  Returns packet size
// (> 0) on success, 0 when no packet is available.
int NET_GetPacket(NETSOURCE netsrc, netadr_t *from, LPSIZEBUF msg);
int NET_GetLoopPacket(NETSOURCE netsrc, netadr_t *from, LPSIZEBUF msg);

void Netchan_Transmit(NETSOURCE netsrc, struct netchan *netchan);
void Netchan_OutOfBand(NETSOURCE netsrc, netadr_t adr, DWORD length, BYTE *data);
void Netchan_OutOfBandPrint(NETSOURCE netsrc, netadr_t adr, LPCSTR format, ...);

void MSG_Write(LPSIZEBUF buf, LPCVOID value, DWORD size);
void MSG_WriteByte(LPSIZEBUF buf, int value);
void MSG_WriteShort(LPSIZEBUF buf, int value);
void MSG_WriteLong(LPSIZEBUF buf, int value);
void MSG_WriteFloat(LPSIZEBUF buf, float value);
void MSG_WriteFloat2(LPSIZEBUF buf, float value);
void MSG_WriteString(LPSIZEBUF buf, LPCSTR value);
void MSG_WriteDeltaEntity(LPSIZEBUF buf, LPCENTITYSTATE from, LPCENTITYSTATE to, bool force);
void MSG_WriteDeltaUIFrame(LPSIZEBUF msg, LPCUIFRAME from, LPCUIFRAME to, bool force);
void MSG_WriteDeltaPlayerState(LPSIZEBUF msg, LPCPLAYER from, LPCPLAYER to);
void MSG_WriteEntityBits(LPSIZEBUF buf, DWORD bits, DWORD number);
void MSG_WritePlayerBits(LPSIZEBUF buf, DWORD bits, DWORD number);
void MSG_WritePos(LPSIZEBUF buf, LPCVECTOR3 pos);
void MSG_WriteDir(LPSIZEBUF buf, LPCVECTOR3 dir);
void MSG_WriteAngle(LPSIZEBUF buf, float f);

int MSG_Read(LPSIZEBUF buf, HANDLE value, DWORD size);
int MSG_ReadByte(LPSIZEBUF buf);
int MSG_ReadShort(LPSIZEBUF buf);
int MSG_ReadLong(LPSIZEBUF buf);
float MSG_ReadFloat(LPSIZEBUF buf);
void MSG_ReadString(LPSIZEBUF buf, LPSTR value);
void MSG_ReadStringN(LPSIZEBUF buf, LPSTR value, int maxlen);
void MSG_ReadPos(LPSIZEBUF buf, LPVECTOR3 pos);
void MSG_ReadDir(LPSIZEBUF buf, LPVECTOR3 dir);
float MSG_ReadAngle(LPSIZEBUF buf);
LPCSTR MSG_ReadString2(LPSIZEBUF buf);
void MSG_ReadDeltaEntity(LPSIZEBUF buf, LPENTITYSTATE edict, int number, int bits);
void MSG_ReadDeltaUIFrame(LPSIZEBUF msg, LPUIFRAME edict, int number, int bits);
void MSG_ReadDeltaPlayerState(LPSIZEBUF msg, LPPLAYER edict, int number, int bits);
int MSG_ReadEntityBits(LPSIZEBUF msg, DWORD *bits);
int MSG_ReadPlayerBits(LPSIZEBUF msg, DWORD *bits);

HANDLE SZ_GetSpace(LPSIZEBUF buf, DWORD length);
void SZ_Write(LPSIZEBUF buf, void const *data, DWORD length);
void SZ_Printf(LPSIZEBUF msg, LPCSTR fmt, ...);
void SZ_Init(LPSIZEBUF buf, BYTE *data, DWORD length);
void SZ_Clear(LPSIZEBUF buf);

#endif
