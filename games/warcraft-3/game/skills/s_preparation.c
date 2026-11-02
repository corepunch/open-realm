#include "s_skills.h"

/* 608ed0 brackets synchronous item dispatch with caster+60 bit8. These frames
 * are call-stack owners, never entity/save state. A map reset invalidates their
 * identities while leaving the old frames linked until their callers unwind. */
typedef struct spellPreparation_s {
    struct spellPreparation_s *previous;
    edict_t *caster, *item;
    uint32_t caster_spawn, item_spawn, ability_slot, ability_generation;
    uint64_t epoch;
} spellPreparation_t;

static spellPreparation_t *preparation;
static uint64_t preparation_epoch = 1;
static void preparation_registry_clear(void);

bool S_SpellPreparationPending(void) { return preparation != NULL; }

uint64_t S_SpellPreparationEpoch(void) { return preparation_epoch; }

void S_ResetSpellPreparation(void) {
    if (++preparation_epoch == 0) { gi.error("Spell preparation epoch exhausted"); abort(); }
    preparation_registry_clear();
}

static bool preparation_live(spellPreparation_t const *scope) {
    return scope->epoch == preparation_epoch && scope->caster->inuse &&
        scope->caster->spawn_time == scope->caster_spawn &&
        !G_IsDeferredFree(scope->caster) && !M_IsDead(scope->caster);
}

bool S_SpellPreparationBypassesTarget(edict_t const *caster) {
    for (spellPreparation_t const *scope = preparation; scope; scope = scope->previous)
        if (scope->caster == caster && preparation_live(scope)) return true;
    return false;
}

/* 609210's exact class allowlist and ordering. Aliases retain their own data. */
static int preparation_family(uint32_t code) {
    switch (G_AbilityCode(code)) {
    case MAKEFOURCC('A','I','m','a'): return 0;
    case MAKEFOURCC('A','I','m','r'): return 1;
    case MAKEFOURCC('A','I','r','e'): return 2;
    case MAKEFOURCC('A','I','r','a'): return 3;
    default: return -1;
    }
}

/* Ordinary profiled item execution shares attachment identity, but never
 * enters the four-class preparatory mana collector. */
static bool item_attachment_registered(uint32_t code) {
    return preparation_family(code) >= 0 || G_AbilityCode(code) == MAKEFOURCC('A','I','v','i');
}

/* Item abilities have retained canonical identities independent of inventory
 * slots. This registry uses native 1cbf40/1cbff0 LIFO slot reuse and 1c53f0's
 * domain generation counter. Its numeric domain is local, not a reproduction
 * of unrelated native agents' opaque global identity numbers. */
typedef struct {
    edict_t *item, *carrier;
    uint32_t item_spawn, carrier_spawn, code, ordinal, generation;
    uint32_t next_item, next_free;
    bool detaching; /* Callback guard only; never serialized. */
} preparationItem_t;
static preparationItem_t *preparation_items;
static uint32_t preparation_item_count, preparation_item_capacity;
static uint32_t preparation_free, preparation_generation = 1;
static uint32_t preparation_heads[MAX_ENTITIES];

static void preparation_registry_clear(void) {
    free(preparation_items); preparation_items = NULL;
    preparation_item_count = preparation_item_capacity = preparation_free = 0;
    preparation_generation = 1;
    memset(preparation_heads, 0, sizeof(preparation_heads));
}

bool S_DetachPreparatoryItems(edict_t *item) {
    if (!item || !g_edicts) return false;
    uint32_t number = (uint32_t)(item - g_edicts);
    if (number >= MAX_ENTITIES) return false;
    typedef struct { uint32_t slot, generation; } attachmentToken_t;
    size_t count = 0;
    for (uint32_t slot=preparation_heads[number]; slot; slot=preparation_items[slot].next_item) count++;
    if (!count) return true;
    attachmentToken_t *tokens=malloc(count*sizeof(*tokens));
    if (!tokens) { gi.error("Cannot snapshot item attachment removal"); abort(); }
    size_t index=0;
    for (uint32_t slot=preparation_heads[number]; slot; slot=preparation_items[slot].next_item)
        tokens[index++]=(attachmentToken_t){slot,preparation_items[slot].generation};
    uint64_t epoch=preparation_epoch;
    uint32_t birth=item->spawn_time;
    bool deferred=G_IsDeferredFree(item);
    for (index=0; index<count; ++index) {
        uint32_t slot=tokens[index].slot, generation=tokens[index].generation;
        preparationItem_t *row=&preparation_items[slot];
        if (row->generation!=generation || row->item!=item || row->item_spawn!=birth) continue;
        /* Public ENDCAST still resolves the old attachment. A recursive
         * removal may release it, but must not notify this token twice. */
        if (!row->detaching && row->carrier && row->carrier->inuse &&
            row->carrier->spawn_time==row->carrier_spawn) {
            row->detaching=true;
            S_CancelItemAbilityAttachment(row->carrier,item,birth,slot,generation);
            /* A callback may reset the world or grow/reuse the registry. */
            if (epoch!=preparation_epoch) break;
            if (item->spawn_time!=birth) break;
            row=&preparation_items[slot];
            if (row->generation!=generation || row->item!=item || row->item_spawn!=birth) continue;
        }
        /* Find the old token again: never overwrite a callback's new head
         * or follow an old next pointer through a released/reused slot. */
        uint32_t *link=&preparation_heads[number];
        while (*link && *link!=slot) link=&preparation_items[*link].next_item;
        if (*link!=slot) continue;
        S_DetachItemAbilityAvailability(row->carrier,slot,generation);
        *link=row->next_item;
        row->item=row->carrier=NULL; row->next_item=0; row->detaching=false;
        row->next_free=preparation_free; preparation_free=slot;
    }
    free(tokens);
    if (epoch != preparation_epoch) return false;
    return item->inuse && item->spawn_time == birth && G_IsDeferredFree(item) == deferred &&
           preparation_heads[number] == 0;
}

static uint32_t preparation_identity_alloc(void) {
    uint32_t slot;
    if (preparation_free) {
        slot = preparation_free;
        preparation_free = preparation_items[slot].next_free;
    } else {
        if (preparation_item_count == UINT32_MAX - 1) {
            gi.error("Preparation item identity slots exhausted"); abort();
        }
        slot = ++preparation_item_count;
        if (slot >= preparation_item_capacity) {
            size_t capacity = preparation_item_capacity ? (size_t)preparation_item_capacity * 2 : 32;
            if (capacity <= slot) capacity = (size_t)slot + 1;
            if (capacity > UINT32_MAX || capacity > SIZE_MAX / sizeof(*preparation_items)) {
                gi.error("Preparation item identity capacity exhausted"); abort();
            }
            preparationItem_t *rows = realloc(preparation_items, capacity * sizeof(*rows));
            if (!rows) { gi.error("Cannot allocate preparation item identities"); abort(); }
            memset(rows + preparation_item_capacity, 0,
                   (capacity - preparation_item_capacity) * sizeof(*rows));
            preparation_items = rows; preparation_item_capacity = (uint32_t)capacity;
        }
    }
    if (!preparation_generation || preparation_generation == UINT32_MAX) {
        gi.error("Preparation item identity generation exhausted"); abort();
    }
    preparation_items[slot] = (preparationItem_t){.generation=preparation_generation++};
    return slot;
}

bool S_AttachPreparatoryItems(edict_t *unit, edict_t *item) {
    if (!unit || !item) return false;
    uint64_t epoch = preparation_epoch;
    uint32_t unit_birth = unit->spawn_time, item_birth = item->spawn_time;
    bool unit_deferred=G_IsDeferredFree(unit),item_deferred=G_IsDeferredFree(item);
    if (!S_DetachPreparatoryItems(item)) return false;
    /* ENDCAST may reset the world, remove either actor, or repick this item.
     * The outer attachment must not replace a callback's new binding. */
    if (epoch != preparation_epoch) return false;
    if (!unit->inuse || !item->inuse || unit->spawn_time != unit_birth ||
        item->spawn_time != item_birth || G_IsDeferredFree(unit)!=unit_deferred ||
        G_IsDeferredFree(item)!=item_deferred) return false;
    uint32_t item_number = (uint32_t)(item - g_edicts);
    if (item_number >= MAX_ENTITIES || preparation_heads[item_number]) return false;
    cstring_t abilities = G_ItemAbilityList(item);
    if (!abilities) return true;
    uint32_t ordinal = 0, tail = 0, number = (uint32_t)(item - g_edicts);
    if (number >= MAX_ENTITIES) return false;
    PARSE_LIST(abilities, name, parse_segment) {
        uint32_t code = FS_SLKKey(name), index = ordinal++;
        if (!item_attachment_registered(code)) continue;
        uint32_t slot = preparation_identity_alloc();
        preparationItem_t *row = &preparation_items[slot];
        row->item = item; row->carrier = unit;
        row->item_spawn = item->spawn_time; row->carrier_spawn = unit->spawn_time;
        row->code = code; row->ordinal = index;
        if (tail) preparation_items[tail].next_item = slot;
        else preparation_heads[number] = slot;
        tail = slot;
        uint32_t generation=row->generation;
        S_AttachItemAbilityAvailability(unit,code,slot,generation);
        if(epoch!=preparation_epoch || !unit->inuse || !item->inuse ||
           unit->spawn_time!=unit_birth || item->spawn_time!=item_birth ||
           preparation_items[slot].generation!=generation || preparation_items[slot].item!=item)return false;
    }
    return true;
}

typedef struct {
    uint32_t item, carrier, item_spawn, carrier_spawn, code, ordinal, generation, next_free;
} preparationItemSave_t;

static bool preparation_identity_live(preparationItem_t const *row) {
    if (!row->item || !row->carrier || !row->item->inuse || !row->carrier->inuse ||
        row->item->spawn_time != row->item_spawn || row->carrier->spawn_time != row->carrier_spawn ||
        G_IsDeferredFree(row->item) || G_IsDeferredFree(row->carrier)) return false;
    item_t const *state = G_ItemState(row->item);
    if (!state || state->carrier != row->carrier || state->inventory_slot < 0 ||
        G_InventoryItem(row->carrier, (uint32_t)state->inventory_slot) != row->item) return false;
    cstring_t abilities = G_ItemAbilityList(row->item);
    if (!abilities) return false;
    uint32_t ordinal = 0;
    PARSE_LIST(abilities, name, parse_segment) {
        if (ordinal++ == row->ordinal) return FS_SLKKey(name) == row->code;
    }
    return false;
}

/* A token is weak: detaching releases its slot, and a later attachment uses
 * a new generation even when the same item edict and alias are reused. */
bool S_ItemAbilityAttachmentLive(edict_t const *carrier, edict_t const *item,
                                 uint32_t item_spawn, uint32_t code,
                                 uint32_t slot, uint32_t generation) {
    if (!carrier || !item || !slot || slot > preparation_item_count || !generation) return false;
    preparationItem_t const *row = &preparation_items[slot];
    return row->generation == generation && row->carrier == carrier && row->item == item &&
        row->item_spawn == item_spawn && row->code == code && preparation_identity_live(row);
}

bool S_ResolveItemAbilityAttachment(edict_t const *carrier,uint32_t slot,uint32_t generation,
                                    edict_t **item,uint32_t *birth,uint32_t *code) {
    if(!carrier || !slot || slot>preparation_item_count)return false;
    preparationItem_t const *row=preparation_items+slot;
    if(row->carrier!=carrier || row->generation!=generation || !preparation_identity_live(row))return false;
    if(item)*item=row->item;
    if(birth)*birth=row->item_spawn;
    if(code)*code=row->code;
    return true;
}

bool S_CaptureItemAbilityAttachment(edict_t const *carrier, edict_t const *item,
                                    uint32_t item_spawn, uint32_t code,
                                    uint32_t *slot, uint32_t *generation) {
    if (!slot || !generation) return false;
    *slot = *generation = 0;
    if (!carrier || !item || !g_edicts) return false;
    uint32_t number = (uint32_t)(item - g_edicts);
    if (number >= MAX_ENTITIES) return false;
    /* Item ability-list order selects the first matching alias; ordinal is
     * retained in the row so a sibling alias cannot satisfy this token. */
    for (uint32_t candidate = preparation_heads[number]; candidate;
         candidate = preparation_items[candidate].next_item) {
        preparationItem_t const *row = &preparation_items[candidate];
        if (S_ItemAbilityAttachmentLive(carrier,item,item_spawn,code,candidate,row->generation)) {
            *slot = candidate; *generation = row->generation; return true;
        }
    }
    return false;
}

/* Save/load validates the roster against actual usable inventory attachments,
 * not only against the rows supplied by the serialized registry. Otherwise a
 * syntactically valid empty section silently removes live potion potential. */
static bool preparation_registry_complete(preparationItem_t const *rows, uint32_t const *heads) {
    FOR_LOOP(number,globals.num_edicts) {
        edict_t *item=g_edicts+number;
        item_t const *state=G_ItemState(item);
        if(!state || !state->carrier || state->in_world || state->pending_use_removal ||
           state->inventory_slot<0 || !state->carrier->inuse || G_IsDeferredFree(item) ||
           G_InventoryItem(state->carrier,(uint32_t)state->inventory_slot)!=item ||
           !G_InventoryCanUseItems(state->carrier))continue;
        cstring_t abilities=G_ItemAbilityList(item);
        if(!abilities)continue;
        uint32_t ordinal=0;
        PARSE_LIST(abilities,name,parse_segment) {
            uint32_t code=FS_SLKKey(name),index=ordinal++;
            if(!item_attachment_registered(code))continue;
            bool found=false;
            for(uint32_t slot=heads[number];slot;slot=rows[slot].next_item)
                if(rows[slot].item==item && rows[slot].item_spawn==item->spawn_time &&
                   rows[slot].carrier==state->carrier && rows[slot].ordinal==index &&
                   rows[slot].code==code) {found=true;break;}
            if(!found)return false;
        }
    }
    return true;
}

static bool preparation_availability_complete(preparationItem_t const *rows,uint32_t count) {
    FOR_LOOP(n,globals.num_edicts)for(unitAbilityDisable_t const *node=g_edicts[n].ability_disables;node;node=node->next) {
        if(!node->attachment_slot && !node->attachment_generation) {
            if(node->class_code==MAKEFOURCC('A','I','v','i') || node->cast_end_pending || node->item_use_pending)return false;
            continue;
        }
        if(!node->linked || !node->attachment_slot || node->attachment_slot>count || !node->attachment_generation ||
           node->class_code!=MAKEFOURCC('A','I','v','i') || node->exclusive_owned ||
           node->exclusive_inventory || node->ethereal_initialized || node->ethereal_owned ||
           node->morph_owned || node->burrow_initialized || node->burrow_owned ||
           *(uint8_t const *)&node->pause_owned>1 || *(uint8_t const *)&node->retire_owned>1 ||
           *(uint8_t const *)&node->cast_end_pending>1 || *(uint8_t const *)&node->item_use_pending>1)return false;
        preparationItem_t const *row=rows+node->attachment_slot;
        if(row->generation!=node->attachment_generation || row->carrier!=g_edicts+n ||
           row->code!=node->code || G_AbilityCode(row->code)!=node->class_code ||
           !preparation_identity_live(row))return false;
    }
    for(uint32_t slot=1;slot<=count;slot++) {
        preparationItem_t const *row=rows+slot;
        if(!row->item || G_AbilityCode(row->code)!=MAKEFOURCC('A','I','v','i'))continue;
        unsigned matches=0;
        for(unitAbilityDisable_t const *node=row->carrier->ability_disables;node;node=node->next)
            if(node->attachment_slot==slot && node->attachment_generation==row->generation)matches++;
        if(matches!=1)return false;
    }
    return true;
}
bool S_ValidateItemAbilityAvailability(void) {
    return preparation_availability_complete(preparation_items,preparation_item_count);
}

bool S_WritePreparationItems(FILE *file) {
    if (S_SpellPreparationPending() || !preparation_registry_complete(preparation_items,preparation_heads) ||
        !S_ValidateItemAbilityAvailability()) return false;
    uint32_t header[3] = {preparation_item_count, preparation_free, preparation_generation};
    if (fwrite(header, sizeof(header), 1, file) != 1) return false;
    for (uint32_t slot = 1; slot <= preparation_item_count; ++slot) {
        preparationItem_t const *row = &preparation_items[slot];
        if (row->detaching || (row->item && !preparation_identity_live(row))) return false;
        preparationItemSave_t saved = {.item=row->item ? (uint32_t)(row->item-g_edicts)+1 : 0,
            .carrier=row->carrier ? (uint32_t)(row->carrier-g_edicts)+1 : 0,
            .item_spawn=row->item_spawn,.carrier_spawn=row->carrier_spawn,.code=row->code,
            .ordinal=row->ordinal,.generation=row->generation,.next_free=row->next_free};
        if (fwrite(&saved,sizeof(saved),1,file)!=1) return false;
    }
    return true;
}

bool S_ReadPreparationItems(FILE *file) {
    uint32_t header[3];
    if (S_SpellPreparationPending() || fread(header,sizeof(header),1,file)!=1 ||
        header[0] > 1048576u || header[1] > header[0] || !header[2]) return false;
    /* Bound corrupted input only; runtime registry growth has no fixture cap. */
    preparationItem_t *rows=calloc((size_t)header[0]+1,sizeof(*rows));
    bool *free_seen=calloc((size_t)header[0]+1,sizeof(*free_seen));
    uint32_t *heads=calloc(MAX_ENTITIES,sizeof(*heads));
    if (!rows || !free_seen || !heads) { free(rows);free(free_seen);free(heads);return false; }
    bool valid=true;
    for(uint32_t slot=1;valid && slot<=header[0];++slot) {
        preparationItemSave_t saved;
        if(fread(&saved,sizeof(saved),1,file)!=1 || saved.item>globals.num_edicts ||
           saved.carrier>globals.num_edicts || !saved.generation || saved.generation>=header[2] ||
           saved.next_free>header[0] || (!!saved.item != !!saved.carrier)) { valid=false;break; }
        rows[slot]=(preparationItem_t){.item=saved.item ? g_edicts+saved.item-1 : NULL,
            .carrier=saved.carrier ? g_edicts+saved.carrier-1 : NULL,.item_spawn=saved.item_spawn,
            .carrier_spawn=saved.carrier_spawn,.code=saved.code,.ordinal=saved.ordinal,
            .generation=saved.generation,.next_free=saved.next_free};
        if(saved.item) {
            if(saved.next_free || !item_attachment_registered(saved.code) || !preparation_identity_live(&rows[slot])) { valid=false;break; }
            uint32_t number=saved.item-1;
            for(uint32_t prior=heads[number];prior;prior=rows[prior].next_item)
                if(rows[prior].ordinal==saved.ordinal) valid=false;
            uint32_t *link=&heads[number];
            while(*link && rows[*link].ordinal<saved.ordinal)link=&rows[*link].next_item;
            rows[slot].next_item=*link;*link=slot;
        }
    }
    for(uint32_t slot=header[1];valid && slot;slot=rows[slot].next_free) {
        if(free_seen[slot] || rows[slot].item) { valid=false;break; }
        free_seen[slot]=true;
    }
    for(uint32_t slot=1;valid && slot<=header[0];++slot)
        if(!rows[slot].item && !free_seen[slot]) valid=false;
    if(valid && (!preparation_registry_complete(rows,heads) || !preparation_availability_complete(rows,header[0])))valid=false;
    if(valid) {
        free(preparation_items);preparation_items=rows;rows=NULL;
        preparation_item_count=header[0];preparation_item_capacity=header[0]+1;
        preparation_free=header[1];preparation_generation=header[2];
        memcpy(preparation_heads,heads,sizeof(preparation_heads));
    }
    free(rows);free(free_seen);free(heads);return valid;
}

static float preparation_amount(uint32_t code, int family) {
    return S_SpellData(code, 1, family < 2 ? 1 : 2);
}

static bool preparation_item(edict_t const *caster, edict_t *item, uint32_t slot) {
    item_t const *state = G_ItemState(item);
    if (!state || G_IsDeferredFree(item) || state->pending_use_removal ||
        state->in_world || state->carrier != caster ||
        state->inventory_slot != (int32_t)slot ||
        !item->data.ItemData || !item->data.ItemData->usable ||
        (!state->charges && item->data.ItemData->uses != 0)) return false;
    return G_InventoryItem(caster, slot) == item;
}

static bool preparation_available(edict_t const *caster, uint32_t code) {
    abilityitem_t item = S_AbilityItem(code);
    return item.ability && (item.ability->flags & AB_ITEM) &&
        G_IsUnitAbilityAvailable(caster, code) &&
        !S_AbilityExecutionDisabled(caster, code) &&
        !S_AbilityOrderBusy(caster, code) &&
        S_SpellCooldownReady((edict_t *)caster, code);
}

float S_PreparatoryManaPotential(edict_t const *caster) {
    if (!caster || !caster->inuse || M_IsDead(caster) || G_IsDeferredFree(caster) ||
        !(caster->aiflags & AI_TOWN_OWNED) || !G_InventoryCanUseItems(caster)) return 0;
    float amount = 0;
    uint32_t capacity = G_InventoryCapacity(caster);
    for (uint32_t slot = 0; slot < capacity; ++slot) {
        edict_t *item = G_InventoryItem(caster, slot);
        if (!preparation_item(caster, item, slot)) continue;
        for (uint32_t identity=preparation_heads[item-g_edicts]; identity;
             identity=preparation_items[identity].next_item) {
            preparationItem_t const *row=&preparation_items[identity];
            if (preparation_family(row->code)<0 || !preparation_identity_live(row) || row->carrier!=caster ||
                !preparation_available(caster,row->code)) continue;
            float candidate=preparation_amount(row->code,preparation_family(row->code));
            if (isfinite(candidate) && candidate>0) amount+=candidate;
        }
    }
    return amount;
}

static bool preparation_source_live(spellPreparation_t const *scope) {
    return preparation_live(scope) && scope->ability_slot<=preparation_item_count &&
        preparation_items[scope->ability_slot].generation==scope->ability_generation &&
        preparation_items[scope->ability_slot].item==scope->item &&
        preparation_identity_live(&preparation_items[scope->ability_slot]) &&
        scope->item->inuse && scope->item->spawn_time==scope->item_spawn &&
        preparation_item(scope->caster,scope->item,(uint32_t)scope->item->item->inventory_slot);
}

static void preparation_phase(spellPreparation_t const *scope, uint32_t code,
                              EVENTTYPE player, EVENTTYPE unit) {
    S_DispatchSpellPhase(&(spellPhaseParams_t){.caster=scope->caster, .code=code,
        .target={.type=SPELL_TARGET_UNIT, .entity=scope->caster},
        .player_event=player, .unit_event=unit, .override_point=true, .point={0,0}});
}

static void preparation_dispatch(edict_t *caster, edict_t *item, uint32_t slot,
                                 uint32_t code, uint32_t ability_slot, uint32_t ability_generation) {
    spellPreparation_t scope = {.previous=preparation, .caster=caster, .item=item,
        .caster_spawn=caster->spawn_time, .item_spawn=item->spawn_time,
        .epoch=preparation_epoch,.ability_slot=ability_slot,.ability_generation=ability_generation};
    preparation = &scope;
    /* 688790 notifies before executing the retained item packet and does not
     * install that transient packet as the user head. */
    G_PublishIssuedImmediateOrder(caster, 852008u + slot, caster->s.player, "preparatory item");
    if (!preparation_source_live(&scope)) goto done;
    preparation_phase(&scope, code, EVENT_PLAYER_UNIT_SPELL_CHANNEL, EVENT_UNIT_SPELL_CHANNEL);
    if (!preparation_source_live(&scope)) goto done;
    preparation_phase(&scope, code, EVENT_PLAYER_UNIT_SPELL_CAST, EVENT_UNIT_SPELL_CAST);
    if (!preparation_source_live(&scope)) goto done;
    preparation_phase(&scope, code, EVENT_PLAYER_UNIT_SPELL_EFFECT, EVENT_UNIT_SPELL_EFFECT);
    if (!preparation_source_live(&scope)) goto done;

    /* All four admitted classes have v34c=0x00200001 (435900 dispatches
     * 624b80/624b90/502400/502410). None has the 0x02000000 exemption:
     * v394=3fa400 requires ANY live user head. A nested successor can satisfy
     * it; the old revision is not required. EFFECT alone does not commit use. */
    bool committed = caster->current_order_id != 0;
    if (committed) {
        abilityitem_t ability = S_AbilityItem(code);
        abilityCall_t call = {.item=&ability, .source_item=item,
            .source_item_spawn_time=scope.item_spawn};
        S_AbilityMessage(caster, A_ITEM_USE, &call);
        if (!preparation_source_live(&scope)) goto done;
        S_SpellStartCooldown(caster, code, 1);
    }
    preparation_phase(&scope, code, EVENT_PLAYER_UNIT_SPELL_FINISH, EVENT_UNIT_SPELL_FINISH);
    if (!preparation_source_live(&scope)) goto done;
    preparation_phase(&scope, code, EVENT_PLAYER_UNIT_SPELL_ENDCAST, EVENT_UNIT_SPELL_ENDCAST);
    if (committed && preparation_source_live(&scope)) G_CompletePreparatoryItemUse(caster, item);
done:
    preparation = scope.previous;
}


typedef struct { uint32_t slot, generation; int family; } preparationCandidate_t;
static int preparation_compare(void const *left, void const *right) {
    preparationCandidate_t const *a=left,*b=right;
    if(a->family!=b->family)return a->family<b->family ? -1 : 1;
    return a->slot<b->slot ? -1 : a->slot>b->slot;
}

bool S_PrepareSpellMana(edict_t *caster, uint32_t code, uint32_t rank) {
    if (!caster || !caster->inuse || M_IsDead(caster) || G_IsDeferredFree(caster) ||
        !(caster->aiflags & AI_TOWN_OWNED) || !G_InventoryCanUseItems(caster) ||
        S_SpellCanPay(caster, code, rank)) return false;
    uint64_t epoch = preparation_epoch;
    uint32_t spawn = caster->spawn_time;
    bool dispatched = false;
    preparationCandidate_t *candidates=NULL;
    size_t count=0,capacity=0;
    /* 609260 snapshots attached ability identities once. A callback-added
     * item is not a member of this dispatch, and slot moves cannot reorder it. */
    uint32_t inventory_capacity=G_InventoryCapacity(caster);
    for(uint32_t inventory_slot=0;inventory_slot<inventory_capacity;++inventory_slot) {
        edict_t *item=G_InventoryItem(caster,inventory_slot);
        if(!preparation_item(caster,item,inventory_slot))continue;
        for(uint32_t slot=preparation_heads[item-g_edicts];slot;slot=preparation_items[slot].next_item) {
            preparationItem_t const *row=&preparation_items[slot];
            if(preparation_family(row->code)<0 || !preparation_identity_live(row))continue;
            if(count==capacity) {
                size_t next=capacity ? capacity*2 : 8;
                if(next<capacity || next>SIZE_MAX/sizeof(*candidates)) { gi.error("Preparation snapshot exhausted");abort(); }
                preparationCandidate_t *grown=realloc(candidates,next*sizeof(*grown));
                if(!grown) { gi.error("Cannot allocate preparation snapshot");abort(); }
                candidates=grown;capacity=next;
            }
            candidates[count++]=(preparationCandidate_t){slot,row->generation,preparation_family(row->code)};
        }
    }
    if(count>1)qsort(candidates,count,sizeof(*candidates),preparation_compare);
    while(count) {
        preparationCandidate_t candidate=candidates[--count];
        if(epoch!=preparation_epoch || !caster->inuse || caster->spawn_time!=spawn ||
           G_IsDeferredFree(caster) || M_IsDead(caster) || S_SpellCanPay(caster,code,rank))break;
        if(candidate.slot>preparation_item_count)continue;
        preparationItem_t row=preparation_items[candidate.slot];
        if(row.generation!=candidate.generation || !preparation_identity_live(&row) || row.carrier!=caster)continue;
        uint32_t slot=(uint32_t)row.item->item->inventory_slot;
        if(!preparation_item(caster,row.item,slot) || !preparation_available(caster,row.code))continue;
        float amount=preparation_amount(row.code,candidate.family);
        if(!isfinite(amount) || amount<=0 || caster->mana.value>=caster->mana.max_value)continue;
        bool active=false;
        for(spellPreparation_t const *scope=preparation;scope;scope=scope->previous)
            if(scope->epoch==epoch && scope->item==row.item && scope->item_spawn==row.item_spawn)active=true;
        if(active)continue;
        preparation_dispatch(caster,row.item,slot,row.code,candidate.slot,candidate.generation);
        dispatched=true;
    }
    free(candidates);
    return dispatched;
}
