#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "fixtures/retail_clock275.h"

bool run_test_jass(cstring_t source);
void reset_entities(void);
void setup_test_world(void);
edict_t *alloc_test_unit(uint32_t type, float x, float y);

static uint32_t clock275_numerator, clock275_denominator, clock275_calls;
static void clock275_rate(uint32_t numerator, uint32_t denominator) {
    clock275_numerator = numerator;
    clock275_denominator = denominator;
    clock275_calls++;
}

/* Original 213350 -> 2300e0 narrows to a byte, then saturates at 2.
 * MAP_LOCK_SPEED prevents both the metadata write and scheduler update. */
TEST(wc3_clock, clock275_public_speed_canonicalization_and_lock) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "local integer i=0\nlocal integer expected=0\n"
        "loop\nexitwhen i==512\n"
        "set expected=i-(i/256)*256\n"
        "if expected>2 then\nset expected=2\nendif\n"
        "call SetGameSpeed(ConvertGameSpeed(i))\n"
        "call BJassAssert(GetGameSpeed()==ConvertGameSpeed(expected),\"retail byte speed\")\n"
        "set i=i+1\nendloop\n"
        "call SetGameSpeed(ConvertGameSpeed(1))\n"
        "call SetMapFlag(ConvertMapFlag(16384),true)\n"
        "call SetGameSpeed(ConvertGameSpeed(0))\n"
        "call BJassAssert(GetGameSpeed()==ConvertGameSpeed(1),\"locked speed\")\n"
        "call SetMapFlag(ConvertMapFlag(16384),false)\n"
        "call SetGameSpeed(ConvertGameSpeed(0))\n"
        "call BJassAssert(GetGameSpeed()==ConvertGameSpeed(0),\"unlocked speed\")\n"
        "endfunction\n"));
}

TEST(wc3_clock, clock275_original_setter_clock_words_and_saved_rate) {
    reset_entities(); setup_test_world();
    void (*old_rate)(uint32_t,uint32_t) = gi.SetSimulationRate;
    gi.SetSimulationRate = clock275_rate;
    FOR_LOOP(i, sizeof(clock275_speeds)/sizeof(*clock275_speeds)) {
        uint32_t const *row = clock275_speeds[i];
        level.setup.speed = 1;
        level.setup.map_flags = row[1];
        clock275_calls = 0;
        G_SetGameSpeed(row[0]);
        T_EQ(level.setup.speed, row[2]);
        T_EQ(clock275_calls, !(row[1]&0x4000));
        if (clock275_calls) {
            T_EQ(clock275_numerator, row[2]+3);
            T_EQ(clock275_denominator, 5);
        }
    }
    wc3Clock_t clocks[2] = {{.span=300},{.span=300}};
    FOR_LOOP(i, sizeof(clock275_timeline)/sizeof(*clock275_timeline)) {
        uint32_t const *row = clock275_timeline[i];
        wc3Clock_t *clock = clocks + row[0];
        T_EQ(wc3_float_bits(clock->time), row[4]);
        if (!row[2]) wc3_clock_advance(clock, wc3_float(row[1]), row[3]);
        T_EQ(wc3_float_bits(clock->time), row[5]);
        T_EQ(clock->epoch, row[6]);
        T_EQ(wc3_float_bits(clock->span), row[7]);
    }
    level.setup.map_flags = 0;
    G_SetGameSpeed(0);
    PATHSTR file;
    strlcpy(file, Test_TempPath("clock275-rate.bin"), sizeof(file));
    T_ASSERT(WriteGame(file));
    G_SetGameSpeed(2);
    clock275_calls = 0;
    T_ASSERT(ReadGame(file));
    T_EQ(level.setup.speed, 0);
    T_EQ(clock275_calls, 1);
    T_EQ(clock275_numerator, 3);
    T_EQ(clock275_denominator, 5);
    remove(file);
    gi.SetSimulationRate = old_rate;
    reset_entities(); setup_test_world();
}

/* All three wall rates run the same primary quanta and Move commits for an
 * equal number of simulation frames. A speed change never rescales velocity. */
TEST(wc3_clock, clock275_speed_preserves_move_and_saved_continuation) {
    uint32_t expected[20][6];
    PATHSTR file;
    strlcpy(file, Test_TempPath("clock275-moving.bin"), sizeof(file));
    FOR_LOOP(rate, 3) {
        reset_entities(); setup_test_world();
        level.time=level.pathing_msec=level.pathing_phase=0;
        level.pathing_counter=0x400;
        level.pathing_due=level.pathing_owner_clock_valid=false;
        level.pathing_clock=(wc3Clock_t){.span=300};
        level.timer_clock_valid=level.fow_clock_valid=false;
        wc3_random_seed(&level.pathing_random,0x69707365);
        T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
        edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 64);
        unit->collision = 0;
        unit->stand = unit_stand; unit->think = monster_think;
        unit->svflags |= SVF_MONSTER; unit->movetype = MOVETYPE_STEP;
        unit_stand(unit); S_SetUnitMoveSpeed(unit, 100);
        T_ASSERT(unit_issueorder(unit, "move", &(vec2_t){448, 256}));
        level.started = level.scriptsConfigured = level.scriptsStarted = true;
        G_SetGameSpeed(rate);
        uint32_t number = unit->s.number;
        FOR_LOOP(step, 20) {
            level.time += FRAMETIME; globals.RunFrame();
            uint32_t row[] = {wc3_float_bits(level.pathing_clock.time), level.pathing_counter,
                wc3_float_bits(unit->movement.fine_pose.x), wc3_float_bits(unit->movement.fine_pose.y),
                wc3_float_bits(unit->movement.velocity.x), wc3_float_bits(unit->movement.velocity.y)};
            FOR_LOOP(word, 6) { if (!rate) expected[step][word]=row[word]; else T_EQ(row[word],expected[step][word]); }
            if (step==9) {
                T_ASSERT(WriteGame(file));
                G_SetGameSpeed((rate+1)%3);
                T_ASSERT(ReadGame(file));
                unit=g_edicts+number;
                T_EQ(level.setup.speed, rate);
            }
        }
        T_ASSERT(unit->movement.fine_pose.x>1);
    }
    remove(file);
    reset_entities(); setup_test_world();
}
#endif
