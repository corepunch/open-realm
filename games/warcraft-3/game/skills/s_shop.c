#include "s_skills.h"

/* Neutral item-shop purchase/patron/stock state is owned by g_stock.c because
 * the merchandise itself comes from UnitProfile Sellitems rather than Apit's
 * ability row.  Keep Apit as the stock no-icon command identity; the selected
 * neutral shop command card dispatches item rawcodes through G_ShopPurchaseItem. */

BZ_COMMAND_PROC(AbilityPurchaseItem) {
    UI_AddCancelButton(clent);
}

/* Inventory is also the move identity for pickup and drop orders. */
BZ_ABILITY_PROC(CAbilityInventory) {
    return CAbilityPassive(ent, msg, call);
}
