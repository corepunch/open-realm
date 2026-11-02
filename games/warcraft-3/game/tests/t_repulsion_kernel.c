#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "retail_repulsion_policy.h"

TEST(wc3_repulsion_kernel, integer_policy_setters_keep_only_their_owned_bits) {
    /* Same complete selector/rank byte domains as the unchanged original
     * setter oracle, with independent categories and retained low words. */
    for (unsigned selector=0; selector<256; selector++) for (unsigned rank=0; rank<256; rank++) {
        uint32_t prior = selector*0x01010101u ^ rank*0x10101010u;
        uint32_t category = selector*257u + rank;
        uint32_t word = wc3_repulse_policy(prior, selector, category, rank);
        T_EQ(word&65535u, prior&65535u);
        T_EQ((word>>16)&15u, selector&15u);
        T_EQ((word>>20)&255u, category&255u);
        T_EQ(word>>28, rank&15u);
    }
}

TEST(wc3_repulsion_kernel, inert_rows_keep_overlap_draws_and_zero_without_cooldown) {
    for (unsigned selector=5; selector<16; selector++) {
        wc3RepulseConfig_t config = wc3_repulse_config(selector<<16);
        FOR_LOOP(row, sizeof(retail_inert_pairs)/sizeof(*retail_inert_pairs)) {
            uint32_t const *expected = retail_inert_pairs[row];
            wc3Random_t random = {expected[6],expected[7]};
            wc3Repulse_t state = {.vector={wc3_float(expected[4]),wc3_float(expected[5])}};
            wc3RepulsePair_t pair = {.source={wc3_float(expected[0]),wc3_float(expected[1])},
                .other={wc3_float(expected[2]),wc3_float(expected[3])},.config=config,.random=&random};
            wc3_repulse_pair(&state, &pair);
            T_EQ(random.sum, expected[8]); T_EQ(random.index, expected[9]);
            FOR_LOOP(i,2) T_EQ(wc3_float_bits(state.vector[i]), expected[10+i]);
        }
        FOR_LOOP(row, sizeof(retail_inert_tails)/sizeof(*retail_inert_tails)) {
            uint32_t const *expected = retail_inert_tails[row];
            wc3Repulse_t state = {.vector={wc3_float(expected[0]),wc3_float(expected[1])},.packed=expected[2]};
            wc3_repulse_tail(&state, &config);
            FOR_LOOP(i,2) T_EQ(wc3_float_bits(state.vector[i]), expected[3+i]);
            T_EQ(state.packed, expected[5]);
        }
    }
}
#endif
