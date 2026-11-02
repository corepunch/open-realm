#ifndef BZ_WC3_ENTITY_SET_H
#define BZ_WC3_ENTITY_SET_H

/* Derived membership in edict order. The summary skips empty words, while
 * live iteration observes newly admitted later slots exactly like an edict loop. */
typedef struct {
    uint64_t bits[(MAX_ENTITIES+63)/64], top[(MAX_ENTITIES+4095)/4096];
} entitySet_t;

static inline void entity_set_put(entitySet_t *set, uint32_t index, bool present) {
    assert(index<MAX_ENTITIES);
    uint32_t word = index / 64;
    uint64_t bit = UINT64_C(1) << (index % 64), old = set->bits[word];
    uint64_t changed = present ? old | bit : old & ~bit;
    if (changed == old) return;
    set->bits[word] = changed;
    /* The summary changes only when a word enters or leaves the empty state. */
    uint64_t group = UINT64_C(1) << (word % 64);
    if (!old) set->top[word / 64] |= group;
    else if (!changed) set->top[word / 64] &= ~group;
}

/* Preserve ascending identity even when callbacks add/remove members mid-loop. */
static inline uint32_t entity_set_next(entitySet_t const *set, uint32_t from) {
    if(from>=MAX_ENTITIES)return MAX_ENTITIES;
    uint32_t word=from/64;
    uint64_t bits=set->bits[word] & (UINT64_MAX<<(from%64));
    if(bits)return word*64+__builtin_ctzll(bits);
    word++;
    for(uint32_t top=word/64;top<sizeof(set->top)/sizeof(*set->top);top++,word=top*64) {
        uint64_t groups=set->top[top] & (UINT64_MAX<<(word%64));
        if(!groups)continue;
        word=top*64+__builtin_ctzll(groups);
        return word*64+__builtin_ctzll(set->bits[word]);
    }
    return MAX_ENTITIES;
}
#endif
