#ifdef BZ_TESTS
#include "../server.h"
#include "shared/test.h"

static uint32_t clock275_frames;
static void clock275_frame(void) {
    clock275_frames++;
    T_EQ(sv.time, clock275_frames * FRAMETIME);
}

TEST(server_clock, clock275_fixed_steps_rate_pause_hitch_and_fraction) {
    struct server *saved = malloc(sizeof(*saved));
    *saved = sv;
    struct game_export *old_game = ge;
    struct game_export game = { .RunFrame = clock275_frame };
    uint32_t old_realtime = svs.realtime, old_clients = svs.num_clients;
    ge = &game;
    svs.num_clients = 0;
    for (uint32_t rate = 3; rate <= 5; rate++) {
        memset(&sv, 0, sizeof(sv));
        sv.state = ss_game; /* Exercise the active server fixed-step scheduler. */
        svs.realtime = clock275_frames = 0;
        SV_SetSimulationRate(rate, 5);
        for (unsigned ms = 0; ms < 10000; ms++) SV_Frame(1);
        T_EQ(clock275_frames, rate * 20 + 1);
        T_EQ(sv.next_frame_msec, (uint64_t)clock275_frames * FRAMETIME * 5 / rate);
        uint32_t frames = clock275_frames, game_time = sv.time;
        uint32_t residue = sv.frame_remainder;
        SV_SetSimulationRate(rate * 2, 10); /* Same ratio preserves its phase. */
        T_EQ(sv.frame_remainder, residue);
        SV_SetPaused(true);
        for (unsigned ms = 0; ms < 1000; ms++) SV_Frame(1);
        T_EQ(clock275_frames, frames);
        T_EQ(sv.time, game_time);
        SV_SetPaused(false);
        SV_Frame(0);
        T_EQ(clock275_frames, frames + 1);
        SV_Frame(5000); /* One fixed step after a hitch, no burst of catch-up. */
        T_EQ(clock275_frames, frames + 2);
        SV_Frame(0);
        T_EQ(clock275_frames, frames + 2);
        SV_SetSimulationRate(0, 1);
        SV_SetSimulationRate(1, 0);
        SV_SetSimulationRate(1, UINT32_MAX);
        /* A floor of INT32_MAX can still have an out-of-range carried interval. */
        SV_SetSimulationRate(199, (uint64_t)INT32_MAX * 199 / FRAMETIME + 1);
        T_EQ(sv.rate_numerator ? sv.rate_numerator : 5, rate);
    }
    sv = *saved;
    free(saved);
    svs.realtime = old_realtime;
    svs.num_clients = old_clients;
    ge = old_game;
    Cvar_Set("paused", sv.paused ? "1" : "0");
}
#endif
