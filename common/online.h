#ifndef BZ_ONLINE_H
#define BZ_ONLINE_H

/* Service state is transport-owned; game menus choose when to enter it. */
typedef struct {
    char address[64], hostname[80], mapname[80];
    uint32_t players, maxPlayers, speed, slots;
} onlineGame_t;

#ifdef BZ_EOS
#ifdef BZ_TESTS
void Online_Acceptance_f(void);
#endif
bool Online_Begin(void);
void Online_Frame(uint32_t msec);
void Online_Shutdown(void);
cstring_t Online_Status(void);
bool Online_Ready(void);
bool Online_IsHost(void);
bool Online_HostReady(void);
bool Online_InLobby(void);
bool Online_ConnectionLost(NETSOURCE source, netadr_t const *address);
void Online_Refresh(void);
uint32_t Online_NumGames(void);
bool Online_Game(uint32_t index, onlineGame_t *out);
void Online_Join(uint32_t index);
bool Online_TakeConnection(netadr_t *address);
void Online_Leave(void);
void Online_Host(cstring_t map, cstring_t name, uint32_t players, uint32_t slots, uint32_t speed);
void Online_CloseAdmission(void);
void Online_Send(NETSOURCE source, int length, void const *data, netadr_t const *to);
int Online_Receive(NETSOURCE source, netadr_t *from, sizeBuf_t *message);
#else
static inline bool Online_Begin(void) { return false; }
static inline void Online_Frame(uint32_t msec) { (void)msec; }
static inline void Online_Shutdown(void) {}
static inline cstring_t Online_Status(void) { return "Internet play requires an EOS-enabled build."; }
static inline bool Online_Ready(void) { return false; }
static inline bool Online_IsHost(void) { return false; }
static inline bool Online_HostReady(void) { return false; }
static inline bool Online_InLobby(void) { return false; }
static inline bool Online_ConnectionLost(NETSOURCE source, netadr_t const *address) { (void)source; (void)address; return false; }
static inline void Online_Refresh(void) {}
static inline uint32_t Online_NumGames(void) { return 0; }
static inline bool Online_Game(uint32_t index, onlineGame_t *out) { (void)index; (void)out; return false; }
static inline void Online_Join(uint32_t index) { (void)index; }
static inline bool Online_TakeConnection(netadr_t *address) { (void)address; return false; }
static inline void Online_Leave(void) {}
static inline void Online_Host(cstring_t map, cstring_t name, uint32_t players, uint32_t slots, uint32_t speed) {
    (void)map; (void)name; (void)players; (void)slots; (void)speed;
}
static inline void Online_CloseAdmission(void) {}
static inline void Online_Send(NETSOURCE source, int length, void const *data, netadr_t const *to) {
    (void)source; (void)length; (void)data; (void)to;
    fprintf(stderr, "Online_Send: this build has no EOS transport\n");
}
static inline int Online_Receive(NETSOURCE source, netadr_t *from, sizeBuf_t *message) {
    (void)source; (void)from; (void)message; return 0;
}
#endif
#endif
