#include "s_skills.h"

/* Shop system — minimal marker abilities that can be extended when the
 * inventory and shop UI are wired up. */

/* Neutral Building (Aneu): auto-selects nearest hero per player within radius.
 * The selection scan is not implemented yet. */
static void SP_ability_neutral_building(LPCSTR classname, ability_t *self) {
    /* activation_radius read from SLK "AcqRange" when implemented. */
}

ability_t a_neutral_building = {
    .init = SP_ability_neutral_building,
};

/* Shop Purchase Item (Apit): marker ability on shops. The future purchase path
 * will read the sold-item list, charge the player, and create the item. */
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
