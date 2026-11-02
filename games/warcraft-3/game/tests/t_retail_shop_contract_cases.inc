/* Exact audit values from the user-supplied installed ROC/TFT War3.mpq/War3x.mpq
 * report.  The game-data tables, not prior Battle.net summaries or demo
 * fixtures, are the oracle.  See retail-shop-item-tests.md. */
typedef struct { cstring_t name, rawcode, abilList; int gold, lumber, max_stock, replenish, start; } retailItem_t;
static retailItem_t const retail_items[] = {
    { "Anti-Magic Potion", "pams", "AIxs", 100, 0, 2, 120, 0 },
    { "Boots of Speed", "bspd", "AIms", 250, 0, 1, 60, 220 },
    { "Circlet of Nobility", "cnob", "AIx2", 175, 0, 1, 120, 0 },
    { "Dust of Appearance", "dust", "AItb", 75, 0, 1, 60, 0 },
    { "Healing Salve", "hslv", "AIrl", 100, 0, 2, 60, 0 },
    { "Ivory Tower", "tsct", "AIbt", 40, 20, 3, 30, 0 },
    { "Lesser Clarity Potion", "plcl", "AIpl", 70, 0, 2, 30, 0 },
    { "Mechanical Critter", "mcri", "Amec", 50, 0, 2, 60, 0 },
    { "Moonstone", "moon", "AIct", 50, 0, 1, 90, 0 },
    { "Orb of Corruption", "ocor", "AIcb", 375, 0, 1, 120, 0 },
    { "Orb of Fire", "ofir", "AIfb", 275, 0, 1, 120, 0 },
    { "Orb of Lightning", "oli2", "AIll", 375, 0, 1, 120, 0 },
    { "Orb of Venom", "oven", "AIpb,Apo2", 325, 0, 1, 120, 0 },
    { "Periapt of Vitality", "prvt", "AIlf", 350, 0, 1, 120, 0 },
    { "Potion of Healing", "phea", "AIh1", 150, 0, 3, 120, 440 },
    { "Potion of Invisibility", "pinv", "AIv1", 100, 0, 1, 120, 440 },
    { "Potion of Lesser Invulnerability", "pnvl", "AIvl", 150, 0, 1, 120, 440 },
    { "Potion of Mana", "pman", "AIm1", 200, 0, 2, 120, 440 },
    { "Rod of Necromancy", "rnec", "AIrd", 150, 0, 1, 60, 0 },
    { "Sacrificial Skull", "skul", "Ablp", 50, 0, 2, 60, 0 },
    { "Scroll of Healing", "shea", "AIha", 250, 0, 2, 120, 440 },
    { "Scroll of Protection", "spro", "AIda", 150, 0, 2, 120, 440 },
    { "Scroll of Regeneration", "sreg", "AIsl", 100, 0, 2, 90, 0 },
    { "Scroll of Speed", "shas", "AIsa", 50, 0, 1, 60, 0 },
    { "Scroll of Town Portal", "stwp", "AItp", 350, 0, 2, 120, 440 },
    { "Staff of Preservation", "spre", "ANpr", 150, 0, 1, 120, 0 },
    { "Staff of Sanctuary", "ssan", "ANsa", 250, 0, 1, 120, 0 },
    { "Staff of Teleportation", "stel", "AImt", 150, 0, 1, 120, 220 },
    { "Tiny Great Hall", "tgrh", "AIbg", 600, 185, 1, 120, 0 },
    { "Tome of Retraining", "tret", "Aret", 300, 0, 1, 120, 440 },
};

typedef struct { cstring_t name, unit, field, inventory, source; } retailShop_t;
static retailShop_t const retail_shops[] = {
    { "Arcane Vault", "hvlt", "Makeitems", "sreg,mcri,plcl,phea,pman,stwp,tsct,ofir,ssan", "https://classic.battle.net/war3/human/arcanevault.shtml" },
    { "Voodoo Lounge", "ovln", "Makeitems", "shas,hslv,plcl,phea,pman,stwp,tgrh,oli2", "https://classic.battle.net/war3/orc/voodoolounge.shtml" },
    { "Ancient of Wonders", "eden", "Makeitems", "moon,plcl,dust,phea,pman,stwp,spre,oven,pams", "https://classic.battle.net/war3/nightelf/ancientofwonders.shtml" },
    { "Tomb of Relics", "utom", "Makeitems", "rnec,dust,skul,phea,pman,stwp,ocor,shea", "https://classic.battle.net/war3/undead/tombofrelics.shtml" },
    { "Goblin Merchant", "ngme", "Sellitems", "stwp,bspd,dust,tret,prvt,cnob,stel,pnvl,shea,spro,pinv", "https://classic.battle.net/war3/neutral/buildings.shtml" },
};

typedef struct { int shop, item; } retailListing_t;
static retailListing_t const retail_listings[] = {
    { 0, 22 }, /* Arcane Vault sreg */
    { 0, 7 }, /* Arcane Vault mcri */
    { 0, 6 }, /* Arcane Vault plcl */
    { 0, 14 }, /* Arcane Vault phea */
    { 0, 17 }, /* Arcane Vault pman */
    { 0, 24 }, /* Arcane Vault stwp */
    { 0, 5 }, /* Arcane Vault tsct */
    { 0, 10 }, /* Arcane Vault ofir */
    { 0, 26 }, /* Arcane Vault ssan */
    { 1, 23 }, /* Voodoo Lounge shas */
    { 1, 4 }, /* Voodoo Lounge hslv */
    { 1, 6 }, /* Voodoo Lounge plcl */
    { 1, 14 }, /* Voodoo Lounge phea */
    { 1, 17 }, /* Voodoo Lounge pman */
    { 1, 24 }, /* Voodoo Lounge stwp */
    { 1, 28 }, /* Voodoo Lounge tgrh */
    { 1, 11 }, /* Voodoo Lounge oli2 */
    { 2, 8 }, /* Ancient of Wonders moon */
    { 2, 6 }, /* Ancient of Wonders plcl */
    { 2, 3 }, /* Ancient of Wonders dust */
    { 2, 14 }, /* Ancient of Wonders phea */
    { 2, 17 }, /* Ancient of Wonders pman */
    { 2, 24 }, /* Ancient of Wonders stwp */
    { 2, 25 }, /* Ancient of Wonders spre */
    { 2, 12 }, /* Ancient of Wonders oven */
    { 2, 0 }, /* Ancient of Wonders pams */
    { 3, 18 }, /* Tomb of Relics rnec */
    { 3, 3 }, /* Tomb of Relics dust */
    { 3, 19 }, /* Tomb of Relics skul */
    { 3, 14 }, /* Tomb of Relics phea */
    { 3, 17 }, /* Tomb of Relics pman */
    { 3, 24 }, /* Tomb of Relics stwp */
    { 3, 9 }, /* Tomb of Relics ocor */
    { 3, 20 }, /* Tomb of Relics shea */
    { 4, 24 }, /* Goblin Merchant stwp */
    { 4, 1 }, /* Goblin Merchant bspd */
    { 4, 3 }, /* Goblin Merchant dust */
    { 4, 29 }, /* Goblin Merchant tret */
    { 4, 13 }, /* Goblin Merchant prvt */
    { 4, 2 }, /* Goblin Merchant cnob */
    { 4, 27 }, /* Goblin Merchant stel */
    { 4, 16 }, /* Goblin Merchant pnvl */
    { 4, 20 }, /* Goblin Merchant shea */
    { 4, 21 }, /* Goblin Merchant spro */
    { 4, 15 }, /* Goblin Merchant pinv */
};
