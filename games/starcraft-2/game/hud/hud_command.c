/* Native CUnit.CardLayouts selects the card; CButton supplies its artwork and hotkey. */
#include "hud.h"

static char sc2_command_click[SC2_COMMAND_SLOTS][160];
static char sc2_command_tip[SC2_COMMAND_SLOTS][512];

bool SC2_HUD_CommandEnabled(edict_t const *unit, cstring_t abilcmd) {
    if (!unit || !SC2_UnitAlive(&unit->unit) || (unit->unit.states & (1u<<SC2_UNIT_PAUSED))) return false;
    char ability[64], command[64];
    if (sscanf(abilcmd,"%63[^,],%63s",ability,command)!=2) return false;
    for (int i=0;i<unit->unit.abil_n;i++) {
        sc2UnitAbil_t const *abil=&unit->unit.abils[i];
        if (strcmp(abil->link,ability)) continue;
        return !abil->disabled && !abil->hidden && SC2_CommandSupported(ability,command);
    }
    return false;
}
void SC2_HUD_PrepareCommandPanel(sc2BaseFrame_t *frames, uint32_t count, sc2BaseFrame_t *root, edict_t const *unit) {
    if (!frames || !root) return;
    sc2UnitPresentation_t presentation={0};
    if (unit) SC2_MapUnitPresentation(unit->unit.type,&presentation);
    for (uint32_t i=0;i<count;i++) {
        sc2BaseFrame_t *btn=&frames[i]; int slot;
        if (btn->sc2_type!=SC2_FRAMETYPE_COMMAND_BUTTON || btn->parent_index!=root->number) continue;
        if (!btn->name || sscanf(btn->name,"CommandButton%2d",&slot)!=1 || slot>=SC2_COMMAND_SLOTS) continue;
        btn->type=FT_FRAME; btn->image=0; btn->onclick=NULL; btn->tooltip=NULL; btn->hotkey=0;
        /* Empty slots remain geometry carriers: later buttons anchor to them. */
        for (uint32_t j=0;j<count;j++) if (frames[j].parent_index==btn->number) {
            frames[j].ui_flags|=SC2_UIFLAG_HIDDEN;
            if (frames[j].sc2_type==SC2_FRAMETYPE_IMAGE) frames[j].image=0;
        }
        for (int n=0;n<SC2_COMMAND_SLOTS;n++) {
            sc2CardButton_t const *card=&presentation.cards[n];
            if (!*card->face || card->row*5+card->column!=(uint32_t)slot) continue;
            sc2ButtonFace_t face;
            if (!SC2_MapButtonFace(card->face,&face)) { fprintf(stderr,"SC2 HUD: unresolved button '%s' for '%s'\n",card->face,unit->unit.type); continue; }
            bool enabled=SC2_HUD_CommandEnabled(unit,card->abilcmd);
            bool hidden=false;
            for (int a=0;a<unit->unit.abil_n;a++) {
                size_t len=strlen(unit->unit.abils[a].link);
                if (!strncmp(card->abilcmd,unit->unit.abils[a].link,len) && card->abilcmd[len]==',' && unit->unit.abils[a].hidden) hidden=true;
            }
            if (hidden) continue;
            btn->type=FT_COMMANDBUTTON; btn->image=gi.ImageIndex(face.icon);
            btn->color=enabled ? COLOR32_WHITE : (color32_t){96,96,96,255};
            cstring_t name=SC2_HUD_Localized(face.name), key=SC2_HUD_Localized(face.hotkey);
            snprintf(sc2_command_tip[slot],sizeof(sc2_command_tip[slot]),"%s%s",name,enabled ? "" : " (unavailable)");
            btn->tooltip=sc2_command_tip[slot];
            snprintf(sc2_command_click[slot],sizeof(sc2_command_click[slot]),"button %s",card->abilcmd);
            btn->onclick=enabled ? sc2_command_click[slot] : NULL;
            btn->hotkey=enabled && strlen(key)==1 ? (uint8_t)key[0] : 0;
        }
    }
}
