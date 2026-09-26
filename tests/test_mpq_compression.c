#include "common/mpq.h"
#include "shared/test.h"

/* Mark Adler's blast.c reference stream, wrapped in the MPQ method byte. */
static uint8_t const pkware_sector[] = { 0x08, 0x00, 0x04, 0x82, 0x24, 0x25, 0x8f, 0x80, 0x7f };

TEST(mpq_compression, pkware_sector) {
    uint8_t out[14] = {0};
    uint32_t size = 0;
    T_ASSERT(Mpq_TestDecompressSector(pkware_sector, sizeof(pkware_sector), out, 13, &size));
    T_EQ(size, 13);
    T_STREQ((char *)out, "AIAIAIAIAIAIA");
}

TEST(mpq_compression, pkware_rejects_short_input_and_output_overflow) {
    uint8_t out[14] = {0};
    uint32_t size = 0;
    out[12] = 0x55;
    T_ASSERT(!Mpq_TestDecompressSector(pkware_sector, sizeof(pkware_sector), out, 12, &size));
    T_EQ(out[12], 0x55);
    T_ASSERT(!Mpq_TestDecompressSector(pkware_sector, 5, out, sizeof(out), &size));
}
