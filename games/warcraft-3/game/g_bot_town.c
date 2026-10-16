#include "g_local.h"
#include "skills/s_skills.h"

/* CTownSpot is map-owned. Its stable index is shared by Town AI objects;
 * authored Captain homes and ShiftTownSpot construction hints are separate.
 * Payoff291:9bc1c0/9bfe80/9ba5d0/9ba790/9ccb60. */
#define BOT_TOWN_ATTACK_OFFSET 300.0f /* Runtime scalar d3c7d8, initialized at001c45. */
#define BOT_TOWN_BUILDING_DISTANCE 80u /* Strict coarse distance in9ba5d0. */
#define BOT_TOWN_RESOURCE_DISTANCE 70u /* Strict coarse distance in9ba790. */
#define BOT_TOWN_BUILDING_WORK 150u
#define BOT_TOWN_RESOURCE_WORK 5000u

static edict_t *G_BotSpotAnchor(botTownAnchor_t const *anchor) {
    edict_t *unit=anchor->unit;
    return unit && unit->inuse && unit->spawn_time==anchor->spawn_time ? unit : NULL;
}

void G_BotRefreshTownSpot(botTownSpot_t *spot) {
    edict_t *building=G_BotSpotAnchor(&spot->building),*mine=G_BotSpotAnchor(&spot->mine);
    if (!building && !mine) return;
    if (!building || !mine) {
        spot->attack_home=spot->defense_home=(building ? building : mine)->s.origin2;
        return;
    }
    vec3_t a=mine->s.origin,b=building->s.origin;
    float x=wc3_sub(b.x,a.x),y=wc3_sub(b.y,a.y),z=wc3_sub(b.z,a.z);
    spot->defense_home=(vec2_t){wc3_add(a.x,wc3_mul(x,.5f)),wc3_add(a.y,wc3_mul(y,.5f))};
    float length=wc3_sqrt(wc3_add(wc3_add(wc3_mul(x,x),wc3_mul(y,y)),wc3_mul(z,z)));
    float inverse=wc3_div(1,length);
    spot->attack_home=(vec2_t){wc3_add(b.x,wc3_mul(wc3_mul(x,BOT_TOWN_ATTACK_OFFSET),inverse)),
        wc3_add(b.y,wc3_mul(wc3_mul(y,BOT_TOWN_ATTACK_OFFSET),inverse))};
}

static botTownSpot_t *G_BotSpot(uint32_t index) {
    return index && index<=ARRAY_COUNT(level.ai_town_spots) ? level.ai_town_spots+index-1 : NULL;
}

static uint32_t G_BotNewSpot(vec2_t point) {
    if (ARRAY_COUNT(level.ai_town_spots)==level.ai_town_spots_capacity) {
        if(level.ai_town_spots_capacity>UINT32_MAX/2 ||
           (size_t)MAX(16,level.ai_town_spots_capacity*2)>SIZE_MAX/sizeof(botTownSpot_t))
            gi.error("Town AI: spot capacity overflow");
        uint32_t capacity=MAX(16,level.ai_town_spots_capacity*2);
        botTownSpot_t *spots=gi.MemAlloc(capacity*sizeof(*spots));
        if (ARRAY_COUNT(level.ai_town_spots))memcpy(spots,level.ai_town_spots,ARRAY_COUNT(level.ai_town_spots)*sizeof(*spots));
        gi.MemFree(level.ai_town_spots);level.ai_town_spots=spots;level.ai_town_spots_capacity=capacity;
    }
    uint32_t index=ARRAY_COUNT(level.ai_town_spots)++;
    level.ai_town_spots[index]=(botTownSpot_t){.attack_home=point,.defense_home=point};
    return index+1;
}

static void G_BotAddSpot(bot_t *bot,uint32_t index) {
    FOR_LOOP(i,BOT_TOWN_SPOTS)if(bot->town_spots[i]==index)return;
    FOR_LOOP(i,BOT_TOWN_SPOTS)if(!bot->town_spots[i]){bot->town_spots[i]=index;return;}
}

/*9ba5d0 uses a live resource anchor and a tightening hierarchy-work budget.
 * World distance is not a substitute for the owner-excluding coarse query. */
static uint32_t G_BotSpotForBuilding(edict_t *unit,vec2_t point) {
    uint32_t best=0,distance=BOT_TOWN_BUILDING_DISTANCE,budget=BOT_TOWN_BUILDING_WORK;
    FOR_LOOP(i,ARRAY_COUNT(level.ai_town_spots)) {
        edict_t *mine=G_BotSpotAnchor(&level.ai_town_spots[i].mine);
        if(!mine)continue;
        uint32_t work=0;
        moveOwnerDistance_t input={.actor=mine,.source=unit,.target=mine,
            .from=mine->s.origin2,.to=unit ? unit->s.origin2 : point,.budget=budget};
        uint32_t result=G_MoveOwnerDistance(&input,&work);
        budget=MIN(budget,work+50);
        if(result<distance){distance=result;best=i+1;}
    }
    return best;
}

/*9ba790 prefers mine, building, then stored defense coordinates. A populated
 * overlay anchor is excluded for resource registration. Ties retain encounter
 * order; a zero distance ends the scan. */
static uint32_t G_BotSpotForUnit(edict_t *unit,bool resource) {
    uint32_t best=0,distance=BOT_TOWN_RESOURCE_DISTANCE,budget=BOT_TOWN_RESOURCE_WORK;
    FOR_LOOP(i,ARRAY_COUNT(level.ai_town_spots)) {
        botTownSpot_t *spot=level.ai_town_spots+i;
        if(resource && G_BotSpotAnchor(&spot->overlay))continue;
        edict_t *anchor=G_BotSpotAnchor(&spot->mine);
        if(!anchor)anchor=G_BotSpotAnchor(&spot->building);
        uint32_t work=0;
        moveOwnerDistance_t input={.actor=unit,.source=unit,.target=anchor,
            .from=unit->s.origin2,.to=anchor ? anchor->s.origin2 : spot->defense_home,.budget=budget};
        uint32_t result=G_MoveOwnerDistance(&input,&work);
        budget=MIN(budget,work+100);
        if(result<distance){distance=result;best=i+1;if(!result)break;}
    }
    return best;
}

bool G_BotUnitIsWorker(edict_t const *unit) {
    cstring_t list=unit->data.UnitBalance ? unit->data.UnitBalance->type : NULL;
    if(!list || !*list)list=unit->data.UnitData ? unit->data.UnitData->unitClassification : NULL;
    if(list){PARSE_LIST(list,item,parse_segment)if(!strcasecmp(item,"peon"))return true;}
    return false;
}

static uint32_t G_BotSpotForOwnedUnit(bot_t const *bot,edict_t *unit) {
    if(!bot->town_spots[0])return G_BotSpotForUnit(unit,false);
    uint32_t best=0,distance=0,budget=BOT_TOWN_RESOURCE_WORK;
    FOR_LOOP(i,BOT_TOWN_SPOTS) {
        botTownSpot_t *spot=G_BotSpot(bot->town_spots[i]);
        if(!spot)continue;
        edict_t *anchor=G_BotSpotAnchor(&spot->building);
        if(!anchor)anchor=G_BotSpotAnchor(&spot->mine);
        if(!anchor)continue;
        uint32_t work=0;
        moveOwnerDistance_t input={.actor=unit,.source=unit,.target=anchor,
            .from=unit->s.origin2,.to=anchor->s.origin2,.budget=budget};
        uint32_t result=G_MoveOwnerDistance(&input,&work);
        budget=MIN(budget,work+100);
        if(!best || result<distance){distance=result;best=bot->town_spots[i];}
    }
    return best ? best : bot->town_spots[0];
}

/*9c01f0 owns both explicit initial selection and gold-return building admission.
 * Keep nullable unit/start-point domains and shared anchor publication in one path. */
static void G_BotAdmitTownSpot(bot_t *bot,edict_t *unit,vec2_t point) {
    uint32_t index=G_BotSpotForBuilding(unit,point);
    if(!index)index=G_BotNewSpot(point);
    botTownSpot_t *spot=G_BotSpot(index);
    if(unit) {
        if(G_BotSpotAnchor(&spot->building))spot->building.unit->ai_anchor_spot=0;
        spot->building=(botTownAnchor_t){unit,unit->spawn_time};unit->ai_anchor_spot=index;
        G_BotRefreshTownSpot(spot);
    }
    G_BotAddSpot(bot,index);
}

/* Public StopGathering9c02a0 selects the first Town spot. It never cancels
 * harvest orders. Select by owned insertion rank in one O(units) pass rather
 * than sorting, rescanning each predecessor, or relying on reusable edict IDs. */
void G_BotStopGathering(player_t *player) {
    if(!player || PLAYER_NUM(player)>=MAX_PLAYERS)return;
    bot_t *bot=level.bots+PLAYER_NUM(player);
    bot->flags&=~BOT_GUARD_NEW_UNITS;
    if(bot->town_spots[0])return;
    edict_t *worker=NULL,*building=NULL,*other=NULL;
    FILTER_EDICTS(unit,unit->inuse && unit->s.player==PLAYER_NUM(player) &&
        (unit->aiflags&AI_TOWN_OWNED) && !M_IsDead(unit)) {
        if(G_BotUnitIsWorker(unit)) {
            if(!worker || unit->own_seq>worker->own_seq)worker=unit;
        } else if(G_UnitIsStructure(unit)) {
            if(!building || unit->own_seq<building->own_seq)building=unit;
        } else if(!other || unit->own_seq<other->own_seq)other=unit;
    }
    edict_t *unit=worker ? worker : building ? building : other;
    if(unit)G_BotAdmitTownSpot(bot,unit,unit->s.origin2);
}

void G_BotRegisterTownUnit(edict_t *unit) {
    if(!unit || !unit->inuse || !level.ai_owned_players)return;
    if(S_GoldMineIsMine(unit)) {
        botTownSpot_t *prior=G_BotSpot(unit->ai_anchor_spot);
        bool registered=prior && G_BotSpotAnchor(&prior->mine)==unit;
        if(!registered) {
            uint32_t index=G_BotSpotForUnit(unit,true);
            if(!index)index=G_BotNewSpot(unit->s.origin2);
            botTownSpot_t *spot=G_BotSpot(index);
            if(!G_BotSpotAnchor(&spot->mine)) {
                spot->mine=(botTownAnchor_t){unit,unit->spawn_time};unit->ai_anchor_spot=index;
                G_BotRefreshTownSpot(spot);
            }
        }
    }
    if(!(unit->aiflags&AI_TOWN_OWNED) || unit->ai_town_player)return;
    bot_t *bot=level.bots+unit->s.player;
    if(S_UnitTypeReturnsGold(unit->class_id))G_BotAdmitTownSpot(bot,unit,unit->s.origin2);
    /*9c4a20 restricts worker/ordinary building selection to the four owner
     * slots. Combat-unit9b9130 uses the shared pool directly. */
    unit->ai_town_spot=G_BotUnitIsWorker(unit) || G_UnitIsStructure(unit) ?
        G_BotSpotForOwnedUnit(bot,unit) : G_BotSpotForUnit(unit,false);
    unit->ai_town_player=unit->s.player+1;
    botTownSpot_t *spot=G_BotSpot(unit->ai_town_spot);
    if(spot)spot->members[unit->s.player]++;
}

void G_BotWithdrawTownUnit(edict_t *unit,bool removed) {
    if(!unit)return;
    if(unit->ai_town_player && unit->ai_town_player<=MAX_PLAYERS) {
        bot_t *bot=level.bots+unit->ai_town_player-1;
        uint32_t player=unit->ai_town_player-1;
        botTownSpot_t *spot=G_BotSpot(unit->ai_town_spot);
        if(spot && spot->members[player])spot->members[player]--;
        /*9c4280 removes unreferenced slots and compacts their order. Counts
         * replace its whole-owned-list replay; removal is O(4), not O(units). */
        unsigned write=0;
        FOR_LOOP(i,BOT_TOWN_SPOTS) {
            botTownSpot_t *candidate=G_BotSpot(bot->town_spots[i]);
            if(candidate && candidate->members[player])bot->town_spots[write++]=bot->town_spots[i];
        }
        while(write<BOT_TOWN_SPOTS)bot->town_spots[write++]=0;
    }
    unit->ai_town_spot=unit->ai_town_player=0;
    botTownSpot_t *spot=removed ? G_BotSpot(unit->ai_anchor_spot) : NULL;
    if(spot) {
        botTownAnchor_t *anchors[]={&spot->building,&spot->mine,&spot->overlay};
        FOR_LOOP(k,3)if(anchors[k]->unit==unit)*anchors[k]=(botTownAnchor_t){0};
        unit->ai_anchor_spot=0;
    }
}

void G_BotDefaultCaptainHomes(player_t *player) {
    if(!player || PLAYER_NUM(player)>=MAX_PLAYERS)return;
    bot_t *bot=level.bots+PLAYER_NUM(player);
    if(!bot->town_spots[0]) {
        gameClient_t const *client=G_GetPlayerClientByNumber(PLAYER_NUM(player));
        if(!level.mapinfo || client->ps.start_location>=MAX_PLAYERS)return;
        vec2_t point=level.mapinfo->players[client->ps.start_location].startingPosition;
        G_BotAdmitTownSpot(bot,NULL,point);
    }
    botTownSpot_t const *spot=G_BotSpot(bot->town_spots[0]);
    if(!spot)return;
    FOR_LOOP(i,BOT_CAPTAIN_COUNT) {
        botCaptain_t *captain=bot->captains+i;
        captain->home=i==BOT_CAPTAIN_ATTACK ? spot->attack_home : spot->defense_home;
        captain->home_set=true;
        S_SetCaptainHomeActor(captain,PLAYER_NUM(player),i+1);
    }
}

void G_BotResetTownCaptainHomes(player_t *player) {
    if(!player || PLAYER_NUM(player)>=MAX_PLAYERS)return;
    bot_t *bot=level.bots+PLAYER_NUM(player);
    botTownSpot_t *spot=G_BotSpot(bot->town_spots[0]);
    if(!spot)return;
    G_BotRefreshTownSpot(spot);
    /* Each home callback precedes its own teleport; do not update both homes
     * first or replace either retained point request unconditionally. */
    FOR_LOOP(i,BOT_CAPTAIN_COUNT) {
        vec2_t home=i==BOT_CAPTAIN_ATTACK ? spot->attack_home : spot->defense_home;
        G_BotSetCaptainHome(player,i+1,home.x,home.y);
        S_TeleportCaptainActor(bot->captains+i,&home);
    }
}

void G_BotClearTownSpots(void) {
    gi.MemFree(level.ai_town_spots);level.ai_town_spots=NULL;
    ARRAY_COUNT(level.ai_town_spots)=level.ai_town_spots_capacity=0;
    FOR_LOOP(p,MAX_PLAYERS) {
        memset(level.bots[p].town_spots,0,sizeof(level.bots[p].town_spots));
    }
    FILTER_EDICTS(unit,unit->inuse)unit->ai_town_spot=unit->ai_town_player=unit->ai_anchor_spot=0;
}

/* Save logical values and edict identities. Allocation addresses and derived
 * membership counts never enter the format. */
typedef struct {
    vec2_t attack_home,defense_home;
    uint32_t anchors[3],spawn_times[3];
} botTownSpotSave_t;

bool G_WriteTownSpots(FILE *file) {
    if(fwrite(&ARRAY_COUNT(level.ai_town_spots),sizeof(uint32_t),1,file)!=1)return false;
    FOR_LOOP(i,ARRAY_COUNT(level.ai_town_spots)) {
        botTownSpot_t const *spot=level.ai_town_spots+i;
        botTownSpotSave_t record={.attack_home=spot->attack_home,.defense_home=spot->defense_home};
        botTownAnchor_t const *anchors[]={&spot->building,&spot->mine,&spot->overlay};
        FOR_LOOP(k,3) {
            edict_t *unit=G_BotSpotAnchor(anchors[k]);
            if(unit){record.anchors[k]=(uint32_t)(unit-g_edicts)+1;record.spawn_times[k]=unit->spawn_time;}
        }
        if(fwrite(&record,sizeof(record),1,file)!=1)return false;
    }
    return true;
}

bool G_ReadTownSpots(FILE *file) {
    uint32_t count;
    if(fread(&count,sizeof(count),1,file)!=1)return false;
    long position=ftell(file);
    if(position<0 || fseek(file,0,SEEK_END))return false;
    long end=ftell(file);
    if(end<position || (uint64_t)count*sizeof(botTownSpotSave_t)>(uint64_t)(end-position) || fseek(file,position,SEEK_SET))return false;
    /* Restored edicts are already installed. Do not clear their saved indexes. */
    gi.MemFree(level.ai_town_spots);
    level.ai_town_spots=count ? gi.MemAlloc((size_t)count*sizeof(botTownSpot_t)) : NULL;
    ARRAY_COUNT(level.ai_town_spots)=level.ai_town_spots_capacity=count;
    FOR_LOOP(i,count) {
        botTownSpotSave_t record;
        if(fread(&record,sizeof(record),1,file)!=1 || !isfinite(record.attack_home.x) || !isfinite(record.attack_home.y) ||
           !isfinite(record.defense_home.x) || !isfinite(record.defense_home.y))return false;
        botTownSpot_t *spot=level.ai_town_spots+i;
        *spot=(botTownSpot_t){.attack_home=record.attack_home,.defense_home=record.defense_home};
        botTownAnchor_t *anchors[]={&spot->building,&spot->mine,&spot->overlay};
        FOR_LOOP(k,3)if(record.anchors[k]) {
            if(record.anchors[k]>globals.num_edicts)return false;
            edict_t *unit=g_edicts+record.anchors[k]-1;
            if(!unit->inuse || unit->spawn_time!=record.spawn_times[k])return false;
            *anchors[k]=(botTownAnchor_t){unit,record.spawn_times[k]};
        }
    }
    return true;
}

bool G_BotValidateTownSpots(bool rebuild) {
    uint32_t count=ARRAY_COUNT(level.ai_town_spots);
    if(count && !level.ai_town_spots)return false;
    FOR_LOOP(p,MAX_PLAYERS)FOR_LOOP(i,BOT_TOWN_SPOTS) {
        uint32_t index=level.bots[p].town_spots[i];
        if(index>count)return false;
        FOR_LOOP(k,i)if(index && level.bots[p].town_spots[k]==index)return false;
    }
    uint32_t *members=count ? gi.MemAlloc((size_t)count*MAX_PLAYERS*sizeof(*members)) : NULL;
    if(count)memset(members,0,(size_t)count*MAX_PLAYERS*sizeof(*members));
    bool valid=true;
    FILTER_EDICTS(unit,unit->inuse) {
        if(unit->ai_town_spot>count || unit->ai_town_player>MAX_PLAYERS || unit->ai_anchor_spot>count ||
           (unit->ai_town_spot && !unit->ai_town_player) ||
           (unit->ai_town_player && (unit->ai_town_player!=unit->s.player+1 || !(unit->aiflags&AI_TOWN_OWNED)))){valid=false;break;}
        if(unit->ai_town_player && unit->ai_town_spot)members[(unit->ai_town_spot-1)*MAX_PLAYERS+unit->ai_town_player-1]++;
    }
    if(valid)FOR_LOOP(i,count) {
        botTownSpot_t *spot=level.ai_town_spots+i;
        if(!isfinite(spot->attack_home.x) || !isfinite(spot->attack_home.y) ||
           !isfinite(spot->defense_home.x) || !isfinite(spot->defense_home.y)){valid=false;break;}
        if(rebuild)memcpy(spot->members,members+i*MAX_PLAYERS,sizeof(spot->members));
        else if(memcmp(spot->members,members+i*MAX_PLAYERS,sizeof(spot->members))){valid=false;break;}
    }
    gi.MemFree(members);return valid;
}
