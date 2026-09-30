#include "hud.h"

static char sc2_vital_text[3][48], sc2_info_text[256], sc2_name_text[192];

sc2BaseFrame_t *SC2_HUD_Find(sc2BaseFrame_t *root, cstring_t name) {
    if (!root) return NULL;
    uint32_t count;
    sc2BaseFrame_t *frames=SC2_LayoutGetFrames(&count);
    for (uint32_t i=0;i<count;i++) {
        if (strcmp(frames[i].name,name)) continue;
        for (uint32_t parent=frames[i].parent_index;parent!=UINT32_MAX;parent=frames[parent].parent_index)
            if (parent==root->number) return &frames[i];
    }
    return NULL;
}
static void sc2_hud_visible(sc2BaseFrame_t *frame, bool visible) {
    if (!frame) return;
    if (visible) frame->ui_flags&=~SC2_UIFLAG_HIDDEN;
    else frame->ui_flags|=SC2_UIFLAG_HIDDEN;
}
void SC2_HUD_PrepareUnitPanel(edict_t const *unit, uint32_t selected_count) {
    sc2BaseFrame_t *info=SC2_LayoutFindFrameByName("InfoPanel");
    sc2BaseFrame_t *portrait=SC2_LayoutFindFrameByName("PortraitPanel");
    static cstring_t const inactive[]={"InfoPaneAIGroup","InfoPaneAIPlayer","InfoPaneHero","InfoPaneQueue","InfoPaneProgress","InfoPaneCargo","InfoPaneGroup",NULL};
    for (int i=0;inactive[i];i++) sc2_hud_visible(SC2_HUD_Find(info,inactive[i]),false);
    sc2_hud_visible(SC2_HUD_Find(info,"UnitPanel"),unit!=NULL);
    sc2_hud_visible(SC2_HUD_Find(info,"InfoPaneUnit"),unit!=NULL);
    sc2_hud_visible(portrait,unit!=NULL);
    SC2_HUD_SetPortraitModel(0);
    if (!unit) return;
    sc2UnitPresentation_t presentation;
    if (!SC2_MapUnitPresentation(unit->unit.type,&presentation)) { fprintf(stderr,"SC2 HUD: unresolved CUnit '%s'\n",unit->unit.type); return; }
    sc2BaseFrame_t *picture=SC2_HUD_Find(portrait,"Portrait");
    /* TODO: animated FXA portraits require CModel.RequiredAnims (.m3a) and
     * FacialController playback. Until that pipeline exists, author the
     * retail static portrait mode directly from CModel.Image. */
    if (picture) {
        picture->type=FT_TEXTURE; picture->sc2_type=SC2_FRAMETYPE_IMAGE;
        picture->image=*presentation.portrait_image ? gi.ImageIndex(presentation.portrait_image) : 0;
        if (!*presentation.portrait_image) fprintf(stderr,"SC2 HUD: CUnit '%s' has no static CModel.Image portrait\n",unit->unit.type);
    }
    sc2BaseFrame_t *pane=SC2_HUD_Find(info,"InfoPaneUnit"),*name=SC2_HUD_Find(pane,"NameLabel");
    snprintf(sc2_name_text,sizeof(sc2_name_text),selected_count>1 ? "%s (%u selected)" : "%s",SC2_HUD_Localized(presentation.name),selected_count);
    if (name) { name->text=sc2_name_text; name->label.textalignx=FONT_JUSTIFYCENTER; }
    sc2BaseFrame_t *wire=SC2_HUD_Find(info,"UnitWireframe");
    if (wire) {
        wire->type=FT_TEXTURE;
        wire->image=*presentation.wireframe ? gi.ImageIndex(presentation.wireframe) : 0;
        if (!*presentation.wireframe) fprintf(stderr,"SC2 HUD: CUnit '%s' has no wireframe\n",unit->unit.type);
        wire->color=(color32_t){96,255,96,255};
    }
    static cstring_t const labels[]={"LifeLabel","EnergyLabel","ShieldLabel"};
    for (int i=0;i<3;i++) {
        sc2BaseFrame_t *label=SC2_HUD_Find(info,labels[i]);
        if (!label) continue;
        sc2Vital_t const *v=&unit->unit.vitals[i];
        /* Empty text retains the anchor chain without reserving a line. */
        sc2_vital_text[i][0]=0;
        if (v->max_value>0) snprintf(sc2_vital_text[i],sizeof(sc2_vital_text[i]),"%.0f / %.0f",v->value,v->max_value);
        label->text=sc2_vital_text[i]; label->label.textalignx=FONT_JUSTIFYCENTER;
        label->color=i==0 ? (color32_t){96,255,96,255} : i==1 ? (color32_t){180,130,255,255} : (color32_t){100,180,255,255};
    }
    snprintf(sc2_info_text,sizeof(sc2_info_text),"Armor: %.0f\nKills: %.0f",presentation.armor,unit->unit.kills);
    sc2BaseFrame_t *text=SC2_HUD_Find(pane,"InfoLabel");
    if (text) { text->text=sc2_info_text; text->label.textalignx=FONT_JUSTIFYCENTER; }
    sc2_hud_visible(SC2_HUD_Find(pane,"ProgressBar"),false);
    sc2_hud_visible(SC2_HUD_Find(info,"BehaviorBar"),false);
    /* EquipmentPanel is populated by weapon/armor entries, not an inventory. */
    sc2_hud_visible(SC2_HUD_Find(pane,"EquipmentPanel"),false);
    sc2BaseFrame_t *type=SC2_HUD_Find(pane,"TypeLabel");
    if (type) type->text="";
}

/* Full retained snapshots on state changes. The reliable channel owns delivery;
 * selected vitals/ability state are hashed after simulation and script updates. */
void SC2_HUD_Update(edict_t *client) {
    if (!client || !client->client) return;
    uint32_t player=client->client->ps.number,count=0;
    edict_t *selected=NULL,*edicts=globals.edicts;
    uint64_t hash=1469598103934665603ull;
    for (uint32_t i=globals.max_clients;i<globals.num_edicts;i++) {
        edict_t *unit=&edicts[i];
        if (!unit->inuse || !(unit->selected & (1u<<player))) continue;
        if (!unit->unit.initialized || !SC2_UnitAlive(&unit->unit) || (unit->unit.states & (1u<<SC2_UNIT_HIDDEN))) continue;
        if (!selected) selected=unit;
        count++;
        hash=(hash^i)*1099511628211ull;
        uint8_t const *bytes=(uint8_t const *)&unit->unit;
        FOR_LOOP(b,sizeof(unit->unit)) hash=(hash^bytes[b])*1099511628211ull;
    }
    hash=(hash^client->client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED])*1099511628211ull;
    hash=(hash^client->client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP])*1099511628211ull;
    hash=(hash^client->client->ps.stats[UI_PLAYERSTAT_GAME_VARIANT])*1099511628211ull;
    hash=(hash^client->client->ps.client_ui_state)*1099511628211ull;
    if (hash==client->client->hud_hash) return;
    client->client->hud_hash=hash;
    SC2_HUD_PrepareUnitPanel(selected,count);
    SC2_HUD_WriteConsolePanel(client);
    SC2_HUD_WriteResourcePanel(client);
}
