#ifndef BZ_ONLINE_PACKET_H
#define BZ_ONLINE_PACKET_H
#define ONLINE_FRAGMENT_HEADER 12
#define ONLINE_FRAGMENT_SIZE 1170
#define ONLINE_FRAGMENT_DATA (ONLINE_FRAGMENT_SIZE - ONLINE_FRAGMENT_HEADER)

/* One ordered message per peer/channel, allocated only while assembling it. */
typedef struct {
    uint8_t *data;
    uint32_t id, size, used, age;
} onlineAssembly_t;

uint32_t Online_WriteFragment(uint8_t *out, uint32_t id, uint32_t size, uint32_t offset, void const *data);
/* Returns message bytes on completion, 0 while pending, -1 for invalid input. */
int Online_ReadFragment(onlineAssembly_t *assembly, void const *data, uint32_t size, sizeBuf_t *message);
void Online_ClearAssembly(onlineAssembly_t *assembly);
#endif
