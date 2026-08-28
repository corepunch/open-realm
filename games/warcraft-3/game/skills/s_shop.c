#include "s_skills.h"

/* Shop system — minimal implementation.
 * WarSmash: CAbilityNeutralBuilding auto-selects heroes, CAbilitySellItems
 * handles purchase. For now, these are marker abilities that can be extended
 * when the inventory/shop UI is wired up. */

/* TODO: Neutral Building (Aneu) must scan the data-defined acquisition radius;
 * WarSmash: CAbilityNeutralBuilding.onTick scans units in rect, maintains
 * the shop selection lifecycle is not implemented yet. */
static void SP_ability_neutral_building(LPCSTR classname, ability_t *self) {
    /* activation_radius read from SLK "AcqRange" when implemented. */
    (void)classname; (void)self;
}

ability_t a_neutral_building = {
    .init = SP_ability_neutral_building,
};

/* TODO: Shop Purchase Item (Apit) must read the sold-item list, charge the
 * player, and create the item through the authoritative lifecycle. */
static void shop_stub_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
}

ability_t a_shop_purchase_item = {
    .cmd = shop_stub_command,
};

/* Shop Sharing (Aall): allows allies to use the shop. */
ability_t a_shop_sharing = {0};

/* Inventory (AInv): passive capability marker for six inventory slots. */
ability_t a_inventory = {0};
