/* Patch 1.24 hashtable natives: parent+child keys to typed slots. Not a gamecache substitute.
 * Tables live in level.hashtables[]; nested HT_HANDLE slots store the JASS type for save/load. */

/* Bob Jenkins lookup2 (SStrHash2): uppercase + '/'→'\\' then mix; empty → 0. */
static DWORD hashtable_sstrhash2(LPCSTR text) {
    DWORD a, b, c, len, i;
    BYTE buff[1024];
    LPCSTR p;

    if (!text || !*text) return 0;
    len = 0;
    for (p = text; *p && len < sizeof(buff); p++) {
        BYTE ch = (BYTE)*p;
        if (ch >= 'a' && ch <= 'z') buff[len++] = ch - 0x20;
        else if (ch == '/') buff[len++] = '\\';
        else buff[len++] = ch;
    }
    a = b = 0x9e3779b9u;
    c = 0;
    i = 0;
#define HT_MIX(a, b, c) do { \
    a -= b; a -= c; a ^= (c >> 13); \
    b -= c; b -= a; b ^= (a << 8); \
    c -= a; c -= b; c ^= (b >> 13); \
    a -= b; a -= c; a ^= (c >> 12); \
    b -= c; b -= a; b ^= (a << 16); \
    c -= a; c -= b; c ^= (b >> 5); \
    a -= b; a -= c; a ^= (c >> 3); \
    b -= c; b -= a; b ^= (a << 10); \
    c -= a; c -= b; c ^= (b >> 15); \
} while (0)
    while (i + 12 <= len) {
        a += buff[i] + ((DWORD)buff[i + 1] << 8) + ((DWORD)buff[i + 2] << 16) + ((DWORD)buff[i + 3] << 24);
        b += buff[i + 4] + ((DWORD)buff[i + 5] << 8) + ((DWORD)buff[i + 6] << 16) + ((DWORD)buff[i + 7] << 24);
        c += buff[i + 8] + ((DWORD)buff[i + 9] << 8) + ((DWORD)buff[i + 10] << 16) + ((DWORD)buff[i + 11] << 24);
        HT_MIX(a, b, c);
        i += 12;
    }
    c += len;
    switch (len - i) {
    case 11: c += (DWORD)buff[i + 10] << 24; /* fallthrough */
    case 10: c += (DWORD)buff[i + 9] << 16; /* fallthrough */
    case 9: c += (DWORD)buff[i + 8] << 8; /* fallthrough */
    case 8: b += (DWORD)buff[i + 7] << 24; /* fallthrough */
    case 7: b += (DWORD)buff[i + 6] << 16; /* fallthrough */
    case 6: b += (DWORD)buff[i + 5] << 8; /* fallthrough */
    case 5: b += buff[i + 4]; /* fallthrough */
    case 4: a += (DWORD)buff[i + 3] << 24; /* fallthrough */
    case 3: a += (DWORD)buff[i + 2] << 16; /* fallthrough */
    case 2: a += (DWORD)buff[i + 1] << 8; /* fallthrough */
    case 1: a += buff[i];
    }
    HT_MIX(a, b, c);
#undef HT_MIX
    return c;
}

static BOOL hashtable_is_edict(HANDLE h, DWORD *out_id) {
    LPEDICT ent = h;
    uintptr_t ptr, base;
    if (!h || !g_edicts || globals.num_edicts == 0) return false;
    ptr = (uintptr_t)ent;
    base = (uintptr_t)g_edicts;
    if (ptr < base || ptr >= base + sizeof(*g_edicts) * globals.num_edicts) return false;
    if ((ptr - base) % sizeof(*g_edicts)) return false;
    if (out_id) *out_id = (DWORD)(ent - g_edicts);
    return true;
}

/* Prefer stable registry ordinals over pointer hashes so GetHandleId survives save/load. */
static DWORD hashtable_handle_id(HANDLE h) {
    DWORD id;
    uintptr_t p;
    if (!h) return 0;
    if (hashtable_is_edict(h, &id)) return id;
    if (G_HashtableIndex(h, &id)) return HASHTABLE_HANDLE_ID_BASE + 0x3000u + id;
    FOR_LOOP(i, game.max_clients)
        if (h == &game.clients[i].ps) return HASHTABLE_HANDLE_ID_BASE + (DWORD)i;
    if (G_JassGroupIndex(h, &id)) return HASHTABLE_HANDLE_ID_BASE + 0x2000u + id;
    p = (uintptr_t)h;
    return HASHTABLE_HANDLE_ID_BASE + 0x1000u + (DWORD)((p >> 3) ^ (p >> 32));
}

static hashtableEntry_t *hashtable_find(LPHASHTABLE table, LONG parent, LONG child, hashtableSlotType_t type) {
    DWORD i;
    if (!table || !table->inuse) return NULL;
    for (i = 0; i < table->num_entries; i++) {
        hashtableEntry_t *e = table->entries + i;
        if (e->parent == parent && e->child == child && e->type == type) return e;
    }
    return NULL;
}

static hashtableEntry_t *hashtable_ensure(LPHASHTABLE table, LONG parent, LONG child, hashtableSlotType_t type) {
    hashtableEntry_t *e = hashtable_find(table, parent, child, type);
    if (e) return e;
    if (!table || !G_HashtableReserve(table, table->num_entries + 1)) return NULL;
    e = table->entries + table->num_entries++;
    memset(e, 0, sizeof(*e));
    e->parent = parent;
    e->child = child;
    e->type = type;
    return e;
}

static void hashtable_remove_at(LPHASHTABLE table, DWORD index) {
    if (!table || index >= table->num_entries) return;
    table->entries[index] = table->entries[table->num_entries - 1];
    table->num_entries--;
}

static void hashtable_remove(LPHASHTABLE table, LONG parent, LONG child, hashtableSlotType_t type) {
    DWORD i;
    if (!table) return;
    for (i = 0; i < table->num_entries; i++) {
        hashtableEntry_t *e = table->entries + i;
        if (e->parent == parent && e->child == child && e->type == type) {
            hashtable_remove_at(table, i);
            return;
        }
    }
}

static HANDLE hashtable_live_handle(HANDLE h) {
    DWORD id;
    if (!h) return NULL;
    if (hashtable_is_edict(h, &id)) {
        LPEDICT ent = (LPEDICT)h;
        if (!ent->inuse || G_IsDeferredFree(ent)) return NULL;
        return h;
    }
    if (G_JassGroupIndex(h, &id) && !G_JassGroupValid(h)) return NULL;
    if (G_HashtableIndex(h, &id)) return h;
    return h;
}

DWORD InitHashtable(LPJASS j) {
    LPHASHTABLE hashtable = G_AllocHashtable();
    if (!hashtable) return jass_pushnullhandle(j, "hashtable");
    return jass_pushlighthandle(j, hashtable, "hashtable");
}

DWORD GetHandleId(LPJASS j) {
    return jass_pushinteger(j, (LONG)hashtable_handle_id(jass_checkhandle(j, 1, "handle")));
}

DWORD StringHash(LPJASS j) {
    return jass_pushinteger(j, (LONG)hashtable_sstrhash2(jass_checkstring(j, 1)));
}

DWORD SaveInteger(LPJASS j) {
    LPHASHTABLE table = jass_checkhandle(j, 1, "hashtable");
    hashtableEntry_t *e = hashtable_ensure(table, jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_INTEGER);
    if (e) e->value.integer = jass_checkinteger(j, 4);
    return 0;
}

DWORD SaveReal(LPJASS j) {
    LPHASHTABLE table = jass_checkhandle(j, 1, "hashtable");
    hashtableEntry_t *e = hashtable_ensure(table, jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_REAL);
    if (e) e->value.real = jass_checknumber(j, 4);
    return 0;
}

DWORD SaveBoolean(LPJASS j) {
    LPHASHTABLE table = jass_checkhandle(j, 1, "hashtable");
    hashtableEntry_t *e = hashtable_ensure(table, jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_BOOLEAN);
    if (e) e->value.boolean = jass_checkboolean(j, 4);
    return 0;
}

DWORD SaveStr(LPJASS j) {
    LPHASHTABLE table = jass_checkhandle(j, 1, "hashtable");
    LPCSTR text = jass_checkstring(j, 4);
    hashtableEntry_t *e;
    if (!table || !text) return jass_pushboolean(j, false);
    e = hashtable_ensure(table, jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_STRING);
    if (!e) return jass_pushboolean(j, false);
    snprintf(e->value.string, sizeof(e->value.string), "%s", text);
    return jass_pushboolean(j, true);
}

static BOOL hashtable_save_handle(LPHASHTABLE table, LONG parent, LONG child, HANDLE value, LPCSTR type) {
    hashtableEntry_t *e;
    if (!table || !value || !type) return false;
    e = hashtable_ensure(table, parent, child, HT_HANDLE);
    if (!e) return false;
    e->value.handle = value;
    snprintf(e->handle_type, sizeof(e->handle_type), "%s", type);
    return true;
}

DWORD LoadInteger(LPJASS j) {
    hashtableEntry_t *e = hashtable_find(jass_checkhandle(j, 1, "hashtable"),
        jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_INTEGER);
    return jass_pushinteger(j, e ? e->value.integer : 0);
}

DWORD LoadReal(LPJASS j) {
    hashtableEntry_t *e = hashtable_find(jass_checkhandle(j, 1, "hashtable"),
        jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_REAL);
    return jass_pushnumber(j, e ? e->value.real : 0.0f);
}

DWORD LoadBoolean(LPJASS j) {
    hashtableEntry_t *e = hashtable_find(jass_checkhandle(j, 1, "hashtable"),
        jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_BOOLEAN);
    return jass_pushboolean(j, e ? e->value.boolean : false);
}

DWORD LoadStr(LPJASS j) {
    hashtableEntry_t *e = hashtable_find(jass_checkhandle(j, 1, "hashtable"),
        jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_STRING);
    return jass_pushstring(j, e ? e->value.string : "");
}

DWORD HaveSavedInteger(LPJASS j) {
    return jass_pushboolean(j, !!hashtable_find(jass_checkhandle(j, 1, "hashtable"),
        jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_INTEGER));
}
DWORD HaveSavedReal(LPJASS j) {
    return jass_pushboolean(j, !!hashtable_find(jass_checkhandle(j, 1, "hashtable"),
        jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_REAL));
}
DWORD HaveSavedBoolean(LPJASS j) {
    return jass_pushboolean(j, !!hashtable_find(jass_checkhandle(j, 1, "hashtable"),
        jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_BOOLEAN));
}
DWORD HaveSavedString(LPJASS j) {
    return jass_pushboolean(j, !!hashtable_find(jass_checkhandle(j, 1, "hashtable"),
        jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_STRING));
}
DWORD HaveSavedHandle(LPJASS j) {
    return jass_pushboolean(j, !!hashtable_find(jass_checkhandle(j, 1, "hashtable"),
        jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_HANDLE));
}

DWORD RemoveSavedInteger(LPJASS j) {
    hashtable_remove(jass_checkhandle(j, 1, "hashtable"), jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_INTEGER);
    return 0;
}
DWORD RemoveSavedReal(LPJASS j) {
    hashtable_remove(jass_checkhandle(j, 1, "hashtable"), jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_REAL);
    return 0;
}
DWORD RemoveSavedBoolean(LPJASS j) {
    hashtable_remove(jass_checkhandle(j, 1, "hashtable"), jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_BOOLEAN);
    return 0;
}
DWORD RemoveSavedString(LPJASS j) {
    hashtable_remove(jass_checkhandle(j, 1, "hashtable"), jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_STRING);
    return 0;
}
DWORD RemoveSavedHandle(LPJASS j) {
    hashtable_remove(jass_checkhandle(j, 1, "hashtable"), jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_HANDLE);
    return 0;
}

DWORD FlushParentHashtable(LPJASS j) {
    LPHASHTABLE table = jass_checkhandle(j, 1, "hashtable");
    if (table && table->inuse) {
        if (table->entries) gi.MemFree(table->entries);
        table->entries = NULL;
        table->num_entries = table->capacity = 0;
    }
    return 0;
}

DWORD FlushChildHashtable(LPJASS j) {
    LPHASHTABLE table = jass_checkhandle(j, 1, "hashtable");
    LONG parent = jass_checkinteger(j, 2);
    DWORD i;
    if (!table) return 0;
    for (i = 0; i < table->num_entries; ) {
        if (table->entries[i].parent == parent) hashtable_remove_at(table, i);
        else i++;
    }
    return 0;
}

#define HT_SAVE_HANDLE(Name, Type) \
DWORD Save##Name##Handle(LPJASS j) { \
    return jass_pushboolean(j, hashtable_save_handle(jass_checkhandle(j, 1, "hashtable"), \
        jass_checkinteger(j, 2), jass_checkinteger(j, 3), jass_checkhandle(j, 4, Type), Type)); \
}
#define HT_LOAD_HANDLE(Name, Type) \
DWORD Load##Name##Handle(LPJASS j) { \
    hashtableEntry_t *e = hashtable_find(jass_checkhandle(j, 1, "hashtable"), \
        jass_checkinteger(j, 2), jass_checkinteger(j, 3), HT_HANDLE); \
    HANDLE value = e ? hashtable_live_handle(e->value.handle) : NULL; \
    return value ? jass_pushlighthandle(j, value, Type) : jass_pushnullhandle(j, Type); \
}

HT_SAVE_HANDLE(Player, "player")
HT_SAVE_HANDLE(Widget, "widget")
HT_SAVE_HANDLE(Destructable, "destructable")
HT_SAVE_HANDLE(Item, "item")
HT_SAVE_HANDLE(Unit, "unit")
HT_SAVE_HANDLE(Ability, "ability")
HT_SAVE_HANDLE(Timer, "timer")
HT_SAVE_HANDLE(Trigger, "trigger")
HT_SAVE_HANDLE(TriggerCondition, "triggercondition")
HT_SAVE_HANDLE(TriggerAction, "triggeraction")
HT_SAVE_HANDLE(TriggerEvent, "event")
HT_SAVE_HANDLE(Force, "force")
HT_SAVE_HANDLE(Group, "group")
HT_SAVE_HANDLE(Location, "location")
HT_SAVE_HANDLE(Rect, "rect")
HT_SAVE_HANDLE(BooleanExpr, "boolexpr")
HT_SAVE_HANDLE(Sound, "sound")
HT_SAVE_HANDLE(Effect, "effect")
HT_SAVE_HANDLE(UnitPool, "unitpool")
HT_SAVE_HANDLE(ItemPool, "itempool")
HT_SAVE_HANDLE(Quest, "quest")
HT_SAVE_HANDLE(QuestItem, "questitem")
HT_SAVE_HANDLE(DefeatCondition, "defeatcondition")
HT_SAVE_HANDLE(TimerDialog, "timerdialog")
HT_SAVE_HANDLE(Leaderboard, "leaderboard")
HT_SAVE_HANDLE(Multiboard, "multiboard")
HT_SAVE_HANDLE(MultiboardItem, "multiboarditem")
HT_SAVE_HANDLE(Trackable, "trackable")
HT_SAVE_HANDLE(Dialog, "dialog")
HT_SAVE_HANDLE(Button, "button")
HT_SAVE_HANDLE(TextTag, "texttag")
HT_SAVE_HANDLE(Lightning, "lightning")
HT_SAVE_HANDLE(Image, "image")
HT_SAVE_HANDLE(Ubersplat, "ubersplat")
HT_SAVE_HANDLE(Region, "region")
HT_SAVE_HANDLE(FogState, "fogstate")
HT_SAVE_HANDLE(FogModifier, "fogmodifier")
HT_SAVE_HANDLE(Agent, "agent")
HT_SAVE_HANDLE(Hashtable, "hashtable")
HT_SAVE_HANDLE(Frame, "framehandle")

HT_LOAD_HANDLE(Player, "player")
HT_LOAD_HANDLE(Widget, "widget")
HT_LOAD_HANDLE(Destructable, "destructable")
HT_LOAD_HANDLE(Item, "item")
HT_LOAD_HANDLE(Unit, "unit")
HT_LOAD_HANDLE(Ability, "ability")
HT_LOAD_HANDLE(Timer, "timer")
HT_LOAD_HANDLE(Trigger, "trigger")
HT_LOAD_HANDLE(TriggerCondition, "triggercondition")
HT_LOAD_HANDLE(TriggerAction, "triggeraction")
HT_LOAD_HANDLE(TriggerEvent, "event")
HT_LOAD_HANDLE(Force, "force")
HT_LOAD_HANDLE(Group, "group")
HT_LOAD_HANDLE(Location, "location")
HT_LOAD_HANDLE(Rect, "rect")
HT_LOAD_HANDLE(BooleanExpr, "boolexpr")
HT_LOAD_HANDLE(Sound, "sound")
HT_LOAD_HANDLE(Effect, "effect")
HT_LOAD_HANDLE(UnitPool, "unitpool")
HT_LOAD_HANDLE(ItemPool, "itempool")
HT_LOAD_HANDLE(Quest, "quest")
HT_LOAD_HANDLE(QuestItem, "questitem")
HT_LOAD_HANDLE(DefeatCondition, "defeatcondition")
HT_LOAD_HANDLE(TimerDialog, "timerdialog")
HT_LOAD_HANDLE(Leaderboard, "leaderboard")
HT_LOAD_HANDLE(Multiboard, "multiboard")
HT_LOAD_HANDLE(MultiboardItem, "multiboarditem")
HT_LOAD_HANDLE(Trackable, "trackable")
HT_LOAD_HANDLE(Dialog, "dialog")
HT_LOAD_HANDLE(Button, "button")
HT_LOAD_HANDLE(TextTag, "texttag")
HT_LOAD_HANDLE(Lightning, "lightning")
HT_LOAD_HANDLE(Image, "image")
HT_LOAD_HANDLE(Ubersplat, "ubersplat")
HT_LOAD_HANDLE(Region, "region")
HT_LOAD_HANDLE(FogState, "fogstate")
HT_LOAD_HANDLE(FogModifier, "fogmodifier")
HT_LOAD_HANDLE(Hashtable, "hashtable")
HT_LOAD_HANDLE(Frame, "framehandle")

#undef HT_SAVE_HANDLE
#undef HT_LOAD_HANDLE
