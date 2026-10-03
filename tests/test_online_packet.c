#include "common/common.h"
#include "common/online_packet.h"
#include "shared/test.h"
#include <stdlib.h>

TEST(online_packet, reassembles_signon_and_maximum_engine_message) {
    uint32_t sizes[] = { 1, ONLINE_FRAGMENT_DATA, ONLINE_FRAGMENT_DATA + 1, 1400, MAX_MSGLEN };
    uint8_t *input = malloc(MAX_MSGLEN), *output = malloc(MAX_MSGLEN);
    T_NOT_NULL(input); T_NOT_NULL(output);
    if (!input || !output) { free(input); free(output); return; }
    for (uint32_t i = 0; i < MAX_MSGLEN; i++) input[i] = (uint8_t)(i * 73);
    FOR_LOOP(n, sizeof(sizes) / sizeof(sizes[0])) {
        onlineAssembly_t assembly = {0};
        sizeBuf_t message = { .data = output, .maxsize = MAX_MSGLEN };
        uint32_t offset = 0;
        while (offset < sizes[n]) {
            uint8_t fragment[ONLINE_FRAGMENT_SIZE];
            uint32_t size = Online_WriteFragment(fragment, n + 1, sizes[n], offset, input);
            T_ASSERT(size > ONLINE_FRAGMENT_HEADER && size <= ONLINE_FRAGMENT_SIZE);
            offset += size - ONLINE_FRAGMENT_HEADER;
            int result = Online_ReadFragment(&assembly, fragment, size, &message);
            T_EQ(result, offset == sizes[n] ? (int)sizes[n] : 0);
        }
        T_EQ(message.cursize, sizes[n]);
        T_EQ(message.readcount, 0);
        T_ASSERT(!memcmp(input, output, sizes[n])); T_NULL(assembly.data);
    }
    free(input); free(output);
}

TEST(online_packet, interleaved_peers_and_channels_keep_independent_packets) {
    uint8_t input[3][2400], output[3][2400], fragment[ONLINE_FRAGMENT_SIZE];
    onlineAssembly_t peers[3] = {{0}};
    sizeBuf_t messages[3] = {{0}};
    FOR_LOOP(i, 3) {
        memset(input[i], i + 1, sizeof(input[i]));
        messages[i].data = output[i]; messages[i].maxsize = sizeof(output[i]);
    }
    for (uint32_t offset = 0; offset < sizeof(input[0]); offset += ONLINE_FRAGMENT_DATA) FOR_LOOP(i, 3) {
        uint32_t size = Online_WriteFragment(fragment, 7, sizeof(input[i]), offset, input[i]);
        int result = Online_ReadFragment(&peers[i], fragment, size, &messages[i]);
        T_EQ(result, offset + ONLINE_FRAGMENT_DATA >= sizeof(input[i]) ? (int)sizeof(input[i]) : 0);
    }
    FOR_LOOP(i, 3) T_ASSERT(!memcmp(input[i], output[i], sizeof(input[i])));
}

TEST(online_packet, rejects_truncation_overflow_wrong_identity_and_out_of_order_data) {
    uint8_t input[3000] = {0}, output[3000], fragment[ONLINE_FRAGMENT_SIZE];
    onlineAssembly_t assembly = {0};
    sizeBuf_t message = { .data = output, .maxsize = sizeof(output) };
    uint32_t size = Online_WriteFragment(fragment, 1, sizeof(input), 0, input);
    T_EQ(Online_ReadFragment(&assembly, fragment, size - 1, &message), -1);
    T_NULL(assembly.data);
    T_EQ(Online_ReadFragment(&assembly, fragment, ONLINE_FRAGMENT_HEADER, &message), -1);
    size = Online_WriteFragment(fragment, 1, sizeof(input), ONLINE_FRAGMENT_DATA, input);
    T_EQ(Online_ReadFragment(&assembly, fragment, size, &message), -1);
    size = Online_WriteFragment(fragment, 1, sizeof(input), 0, input);
    T_EQ(Online_ReadFragment(&assembly, fragment, size, &message), 0);
    size = Online_WriteFragment(fragment, 2, sizeof(input), ONLINE_FRAGMENT_DATA, input);
    T_EQ(Online_ReadFragment(&assembly, fragment, size, &message), -1); T_NULL(assembly.data);
    size = Online_WriteFragment(fragment, 3, sizeof(input), 0, input);
    fragment[4] = fragment[5] = fragment[6] = fragment[7] = 255; /* untrusted total = UINT32_MAX */
    T_EQ(Online_ReadFragment(&assembly, fragment, size, &message), -1); T_NULL(assembly.data);
    size = Online_WriteFragment(fragment, 4, sizeof(input), 0, input);
    message.maxsize = 100;
    T_EQ(Online_ReadFragment(&assembly, fragment, size, &message), -1);
    T_EQ(message.cursize, 0);
}

TEST(online_packet, new_message_and_explicit_cleanup_discard_incomplete_message) {
    uint8_t input[1400] = {42}, output[1400], fragment[ONLINE_FRAGMENT_SIZE];
    onlineAssembly_t assembly = {0};
    sizeBuf_t message = { .data = output, .maxsize = sizeof(output) };
    uint32_t size = Online_WriteFragment(fragment, 8, sizeof(input), 0, input);
    T_EQ(Online_ReadFragment(&assembly, fragment, size, &message), 0);
    size = Online_WriteFragment(fragment, 9, 30, 0, input);
    T_EQ(Online_ReadFragment(&assembly, fragment, size, &message), 30);
    T_EQ(output[0], 42); T_NULL(assembly.data);
    size = Online_WriteFragment(fragment, 10, sizeof(input), 0, input);
    T_EQ(Online_ReadFragment(&assembly, fragment, size, &message), 0);
    Online_ClearAssembly(&assembly);
    T_NULL(assembly.data); T_EQ(assembly.used, 0);
    size = Online_WriteFragment(fragment, 10, sizeof(input), ONLINE_FRAGMENT_DATA, input);
    T_EQ(Online_ReadFragment(&assembly, fragment, size, &message), -1);
}
