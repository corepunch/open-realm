#include "s_skills.h"

/* Shop system — minimal implementation.
 * WarSmash: CAbilityNeutralBuilding auto-selects heroes, CAbilitySellItems
 * handles purchase. For now, these are marker abilities that can be extended
 * when the inventory/shop UI is wired up. */

/* Neutral Building (Aneu): auto-selects nearest hero per player within radius.
 * WarSmash: CAbilityNeutralBuilding.onTick scans units in rect, maintains
 * selectedPlayerUnit[MAX_PLAYERS]. */
static void SP_ability_neutral_building(LPCSTR classname, ability_t *self) {
    /* activation_radius read from SLK "AcqRange" when implemented. */
}

ability_t a_neutral_building = {
    .init = SP_ability_neutral_building,
};

/* Shop Purchase Item (Apit): marker ability on shops. Actual purchase
 * is handled by CAbilitySellItems in WarSmash — reads itemsSold list,
 * checks player gold/lumber, creates item in hero inventory. */
static void shop_stub_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
}

ability_t a_shop_purchase_item = {
    .cmd = shop_stub_command,
};

/* Shop Sharing (Aall): allows allies to use the shop. */
ability_t a_shop_sharing = {0};

/* Inventory (AInv): passive ability on heroes — 6 item slots.
 * WarSmash: CAbilityInventory manages itemsHeld[], giveItem/dropItem,
 * useAutomaticallyWhenAcquired, perishable charge consumption. */
ability_t a_inventory = {0};
