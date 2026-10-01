/* Basic CAbilMove/CAbilStop/CAbilAttack orders own their persistent behavior. */
#include "../g_sc2_local.h"
#include "../hud/hud.h"
#include "games/starcraft-2/common/sc2_minimap.h"

enum { SC2_ORDER_NONE, SC2_ORDER_MOVE, SC2_ORDER_STOP, SC2_ORDER_HOLD, SC2_ORDER_PATROL, SC2_ORDER_ATTACK };
static struct { cstring_t ability, command; uint32_t kind; } const sc2_order_commands[]={
    {"move","Move",SC2_ORDER_MOVE}, {"stop","Stop",SC2_ORDER_STOP},
    {"move","HoldPos",SC2_ORDER_HOLD}, {"move","Patrol",SC2_ORDER_PATROL},
    {"attack","Execute",SC2_ORDER_ATTACK},
};
static uint32_t sc2_order_kind(cstring_t ability, cstring_t command) {
    FOR_LOOP(i,(sizeof(sc2_order_commands)/sizeof(*sc2_order_commands)))
        if (!strcmp(ability,sc2_order_commands[i].ability) && !strcmp(command,sc2_order_commands[i].command)) return sc2_order_commands[i].kind;
    return SC2_ORDER_NONE;
}
bool SC2_CommandSupported(cstring_t ability, cstring_t command) { return sc2_order_kind(ability,command)!=SC2_ORDER_NONE; }

static bool sc2_order_selected(edict_t const *client, edict_t const *unit) {
    uint32_t player=client->client->ps.number;
    return unit->inuse && unit->s.player==player && (unit->selected & (1u<<player)) &&
        unit->unit.initialized && SC2_UnitAlive(&unit->unit) && !(unit->unit.states & ((1u<<SC2_UNIT_PAUSED)|(1u<<SC2_UNIT_HIDDEN)));
}
edict_t *SC2_SelectedUnit(edict_t const *client) {
    if (!client || !client->client) return NULL;
    edict_t *edicts=globals.edicts;
    for (uint32_t i=globals.max_clients;i<globals.num_edicts;i++) if (edicts[i].inuse && edicts[i].unit.initialized && SC2_UnitAlive(&edicts[i].unit) &&
        (edicts[i].selected & (1u<<client->client->ps.number)) && !(edicts[i].unit.states & (1u<<SC2_UNIT_HIDDEN))) return &edicts[i];
    return NULL;
}
static bool sc2_order_enemy(edict_t const *unit, edict_t const *target) {
    if (!target->inuse || !target->unit.initialized || !SC2_UnitAlive(&target->unit) ||
        (target->unit.states & ((1u<<SC2_UNIT_HIDDEN)|(1u<<SC2_UNIT_INVULNERABLE)))) return false;
    uint32_t player=unit->s.player,other=target->s.player;
    return other && player<32 && other<32 && player!=other && !(sc2_players[player].alliances[other]&1u);
}
void SC2_CommandButton(edict_t *client, cstring_t abilcmd) {
    char ability[64], command[64];
    if (sscanf(abilcmd,"%63[^,],%63s",ability,command)!=2) return;
    edict_t *selected=SC2_SelectedUnit(client),*edicts=globals.edicts;
    if (!SC2_HUD_CommandEnabled(selected,abilcmd)) return;
    uint32_t kind=sc2_order_kind(ability,command);
    client->client->minimap_signal=false;
    client->client->pending_order=kind==SC2_ORDER_STOP || kind==SC2_ORDER_HOLD ? 0 : kind;
    client->client->ps.stats[UI_PLAYERSTAT_CURSOR_FLAGS]=client->client->pending_order ? CURSOR_INPUT_MINIMAP_POINT : 0;
    client->client->ps.stats[UI_PLAYERSTAT_CURSOR_INTERACTION]=client->client->pending_order ? 1 : 0;
    if (kind==SC2_ORDER_STOP || kind==SC2_ORDER_HOLD)
        for (uint32_t i=globals.max_clients;i<globals.num_edicts;i++) if (sc2_order_selected(client,&edicts[i]) && SC2_HUD_CommandEnabled(&edicts[i],abilcmd)) {
            edicts[i].order.kind=kind; edicts[i].order.target=0; SC2_StopUnit(&edicts[i]);
        }
}
static void sc2_order_clear_targeting(edict_t *client) {
    client->client->pending_order=0; client->client->minimap_signal=false;
    client->client->ps.stats[UI_PLAYERSTAT_CURSOR_FLAGS]=0;
    client->client->ps.stats[UI_PLAYERSTAT_CURSOR_INTERACTION]=0;
}
void SC2_CancelCommand(edict_t *client) { sc2_order_clear_targeting(client); }
bool SC2_CommandPoint(edict_t *client, vec2_t const *point) {
    if (client->client->minimap_signal) {
        gi.MinimapPing(client,point,3,COLOR32_WHITE,MINIMAP_PING_REMEMBER,MINIMAP_PING_DEFAULT_SIZE);
        sc2_order_clear_targeting(client); return true;
    }
    uint32_t kind=client->client->pending_order;
    if (!kind) return false;
    edict_t *edicts=globals.edicts;
    cstring_t command=kind==SC2_ORDER_ATTACK ? "attack,Execute" : kind==SC2_ORDER_PATROL ? "move,Patrol" : "move,Move";
    for (uint32_t i=globals.max_clients;i<globals.num_edicts;i++) {
        edict_t *unit=&edicts[i];
        if (!sc2_order_selected(client,unit) || !SC2_HUD_CommandEnabled(unit,command)) continue;
        unit->order.kind=kind; unit->order.target=0;
        unit->order.origin=unit->s.origin2; unit->order.destination=*point; unit->order.returning=false;
        SC2_OrderMove(unit,point);
    }
    sc2_order_clear_targeting(client);
    gi.Write(PF_BYTE,&(int32_t){svc_temp_entity}); gi.Write(PF_BYTE,&(int32_t){TE_MOVE_CONFIRMATION});
    gi.Write(PF_POSITION,&(vec3_t){point->x,point->y,0}); gi.unicast(client);
    return true;
}
bool SC2_CommandTarget(edict_t *client, uint32_t number) {
    edict_t *edicts=globals.edicts;
    if (number>=globals.num_edicts || !edicts[number].inuse) return client->client->pending_order!=0;
    edict_t *target=&edicts[number];
    if (client->client->pending_order && client->client->pending_order!=SC2_ORDER_ATTACK)
        return SC2_CommandPoint(client,&target->s.origin2);
    bool issued=false;
    for (uint32_t i=globals.max_clients;i<globals.num_edicts;i++) {
        edict_t *unit=&edicts[i];
        if (!sc2_order_selected(client,unit) || !sc2_order_enemy(unit,target) || !SC2_HUD_CommandEnabled(unit,"attack,Execute")) continue;
        unit->order.kind=SC2_ORDER_ATTACK; unit->order.target=number;
        unit->order.destination=target->s.origin2;
        SC2_OrderMove(unit,&target->s.origin2); issued=true;
    }
    if (issued) sc2_order_clear_targeting(client);
    return issued || client->client->pending_order!=0;
}

void SC2_RunOrders(edict_t *unit) {
    if (!unit->unit.initialized || !SC2_UnitAlive(&unit->unit) ||
        (unit->unit.states & ((1u<<SC2_UNIT_PAUSED)|(1u<<SC2_UNIT_HIDDEN)))) return;
    uint32_t kind=unit->order.kind;
    if (kind==SC2_ORDER_NONE || kind==SC2_ORDER_MOVE) return;
    sc2WeaponPresentation_t weapon;
    bool armed=false;
    for (uint32_t i=0;i<unit->unit.weapon_n;i++) {
        if (unit->unit.weapons[i].disabled) continue;
        if (SC2_MapWeaponPresentation(unit->unit.weapons[i].link,&weapon)) { armed=true; break; }
    }
    edict_t *edicts=globals.edicts,*target=NULL;
    if (armed && unit->order.target<globals.num_edicts && unit->order.target)
        if (sc2_order_enemy(unit,&edicts[unit->order.target])) target=&edicts[unit->order.target];
    if (armed && !target && SC2_HUD_CommandEnabled(unit,"attack,Execute")) {
        float distance=weapon.range;
        for (uint32_t i=globals.max_clients;i<globals.num_edicts;i++) {
            if (!sc2_order_enemy(unit,&edicts[i])) continue;
            float d=Vector2_distance(&unit->s.origin2,&edicts[i].s.origin2);
            if (d<distance) { distance=d; target=&edicts[i]; }
        }
    }
    if (target && armed) {
        float distance=Vector2_distance(&unit->s.origin2,&target->s.origin2);
        if (distance>weapon.range) {
            if (kind!=SC2_ORDER_HOLD && kind!=SC2_ORDER_STOP &&
                (!unit->move.moving || Vector2_distance(&unit->move.target,&target->s.origin2)>0.5f)) SC2_OrderMove(unit,&target->s.origin2);
            return;
        }
        if (unit->move.moving) SC2_StopUnit(unit);
        if ((int32_t)(gi.GetTime()-unit->order.next_attack)>=0) {
            sc2UnitPresentation_t presentation;
            float armor=SC2_MapUnitPresentation(target->unit.type,&presentation) ? presentation.armor : 0;
            /* TODO: dispatch general SC2 effects when projectile, shield-armor
             * and attribute-bonus catalog consumers exist. This direct-life
             * path currently covers the Liberty infantry weapon effects. */
            float damage=MAX(0.5f,weapon.damage-armor),old=target->unit.vitals[0].value;
            SC2_UnitSetProperty(&target->unit,0,old-damage);
            bool died=!SC2_UnitAlive(&target->unit);
            unit->s.angle=atan2f(target->s.origin.y-unit->s.origin.y,target->s.origin.x-unit->s.origin.x);
            SC2_SetUnitAnimation(unit,"Attack");
            unit->order.next_attack=gi.GetTime()+(uint32_t)(weapon.period*1000);
            SC2_UpdateUnit(target);
            if (died) unit->unit.kills++;
            else if (!target->order.kind && SC2_HUD_CommandEnabled(target,"attack,Execute")) {
                target->order.kind=SC2_ORDER_ATTACK; target->order.target=unit->s.number;
                target->order.destination=unit->s.origin2;
            }
            galaxy_combat_damage(sc2_level.vm,unit,target,MIN(old,damage),died);
        }
        return;
    }
    unit->order.target=0;
    if (kind==SC2_ORDER_PATROL && !unit->move.moving) {
        unit->order.returning=!unit->order.returning;
        SC2_OrderMove(unit,unit->order.returning ? &unit->order.origin : &unit->order.destination);
    } else if (kind==SC2_ORDER_ATTACK && !unit->move.moving && Vector2_distance(&unit->s.origin2,&unit->order.destination)>0.25f)
        SC2_OrderMove(unit,&unit->order.destination);
}
