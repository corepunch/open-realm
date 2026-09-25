/* Local binary differential harness; includes the production admission code. */
#include "sound/s_sound.c"
Uint32 SDL_GetTicks(void) { return 10000; }
int main(void) {
    unsigned n, file, priority, flags, user, group, limit;
    static sfxcache_t cache[32];
    while (scanf("%u %u %u %u %u %u %u", &n, &file, &priority, &flags, &user, &group, &limit) == 7) {
        memset(&s, 0, sizeof(s));
        for (unsigned i = 0; i < n; i++) {
            unsigned f, pr, fl, us, gr, tick, serial;
            if (scanf("%u %u %u %u %u %u %u", &f, &pr, &fl, &us, &gr, &tick, &serial) != 7) return 2;
            s.channels[i].active = TRUE; s.channels[i].sc = &cache[f];
            s.channels[i].priority = pr; s.channels[i].serial = serial; s.channels[i].started = tick;
            s.channels[i].policy = (soundPolicy_t){.priority=pr,.flags=fl,.user=us,.group=gr,.max_total=24};
        }
        soundPolicy_t p = {.priority=priority,.flags=flags,.user=user,.group=group,.max_channel=limit,.max_total=24,.max_duplicates=4};
        int chosen = S_AdmitSound(&cache[file], &p);
        unsigned victims = 0;
        for (unsigned i = 0; i < n; i++) if (!s.channels[i].active || chosen == i) victims |= 1u << i;
        printf("%d %u\n", chosen >= 0, victims);
    }
    return 0;
}
