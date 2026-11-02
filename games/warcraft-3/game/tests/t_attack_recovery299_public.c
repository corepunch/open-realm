#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "jass/jass.h"
#include "fixtures/retail_attack299_replay_draft.h"

/* Opt-in private-retail-assets diagnostic. Load the exact captured map, including
 * its config/main, object overrides, geometry, and stock-data selection. Do not
 * call PathProbeInit again or replace the map's main with an abbreviated main. */
extern void (*test_preload_marker)(cstring_t);
extern void (*test_unit_position_query)(edict_t *,unsigned,float);
void reset_entities(void);
void setup_test_world(void);
void CM_SetupTestWorldBounds(box2_t const *);
typedef struct {
    bool active,complete;
    unsigned scene,tick,public_count,public_bad,poll_count,poll_bad;
    edict_t *last_position_unit,*roles[6][3];
    attack299Poll_t polls[12000];
} recovery299Run_t;
static recovery299Run_t recovery299;
static bool recovery299_outgoing;

static unsigned Recovery299Role(edict_t const *u) {
    if(recovery299.scene<6)
        FOR_LOOP(i,3)if(u && recovery299.roles[recovery299.scene][i]==u)return i+1;
    return 0; /* Unknown actor is a mismatch, never a row-filter condition. */
}
static void Recovery299Position(edict_t *u,unsigned component,float value) {
    (void)component;(void)value;recovery299.last_position_unit=u;
}
static void Recovery299Record(attack299Poll_t row) {
    if(!recovery299.active)return;
    unsigned n=recovery299.poll_count++;
    if(n<12000)recovery299.polls[n]=row;
    else recovery299.poll_bad++;
}
static void Recovery299Arrival(moveGroup_t const *g,moveGroupMember_t const *m,wc3Arrival_t const *a) {
    (void)g;
    Recovery299Record((attack299Poll_t){recovery299.scene,0,Recovery299Role(m->unit),10,{
        wc3_float_bits(level.pathing_clock.time),wc3_float_bits(a->source[0]),wc3_float_bits(a->source[1]),
        wc3_float_bits(a->target[0]),wc3_float_bits(a->target[1]),wc3_float_bits(a->heading),
        wc3_float_bits(a->range),wc3_float_bits(a->error),m->in_range,m->arrived}});
}
static void Recovery299Committed(moveGroup_t const *g,moveGroupMember_t const *m) {
    (void)g;edict_t const *u=m->unit;
    Recovery299Record((attack299Poll_t){recovery299.scene,2,Recovery299Role(u),6,{
        wc3_float_bits(level.pathing_clock.time),wc3_float_bits(u->movement.fine_pose.x),wc3_float_bits(u->movement.fine_pose.y),
        wc3_float_bits(wc3_mul(u->movement.velocity.x,1.0f/32)),wc3_float_bits(wc3_mul(u->movement.velocity.y,1.0f/32)),wc3_float_bits(u->s.angle)}});
}
static void Recovery299FourCC(char *line,cstring_t key) {
    char *p=strstr(line,key);if(!p)return;p+=strlen(key);
    char *end;uint32_t v=(uint32_t)strtoul(p,&end,10);
    uint32_t raw=((v&255u)<<24)|((v&0xff00u)<<8)|((v>>8)&0xff00u)|(v>>24);
    char text[32];snprintf(text,sizeof(text),"%u",raw);
    size_t len=strlen(text);memmove(p+len,end,strlen(end)+1);memcpy(p,text,len);
}
static void Recovery299Marker(cstring_t text) {
    if(strncmp(text,"AR299 ",6)||recovery299.complete)return;
    unsigned tick,scene;
    if(sscanf(text,"AR299 tick=%u scene=%u",&tick,&scene)!=2)return;
    if(recovery299_outgoing)fprintf(stderr,"RECOVERY299 OUTGOING %s\n",text);
    recovery299.tick=tick;recovery299.scene=scene;
    if(strstr(text," label=start"))recovery299.active=true;
    unsigned role;char const *pose=strstr(text," label=pose role=");
    if(pose && sscanf(pose," label=pose role=%u",&role)==1 && scene<6 && role>=1 && role<=3) {
        /* GetUnitX/Y in ARUnit immediately precede this pose marker. This binds
         * original roles without guessing edict indices or mutating user data. */
        recovery299.roles[scene][role-1]=recovery299.last_position_unit;
    }
    char line[512];snprintf(line,sizeof(line),"%s",text);
    Recovery299FourCC(line," type=");Recovery299FourCC(line," code=");
    /* Real retail common.j supplies the original UNIT spell-event constants.
     * No PLAYER-event conversion and no numeric/float normalization. */
    unsigned n=recovery299.public_count++;
    unsigned total=sizeof(attack299_public)/sizeof(*attack299_public);
    if(n>=total || strcmp(line,attack299_public[n])) {
        if(!recovery299.public_bad)fprintf(stderr,"RECOVERY299 FIRST PUBLIC %u\n actual %s\n native %s\n",n,line,n<total?attack299_public[n]:"<extra>");
        recovery299.public_bad++;
    }
    if(strstr(text," label=complete")){recovery299.complete=true;recovery299.active=false;}
}
static void Recovery299Check(void) {
    T_EQ(recovery299.public_count,2066);T_EQ(recovery299.public_bad,0);
    unsigned total=sizeof(attack299_polls)/sizeof(*attack299_polls);
    T_EQ(total,5342);T_EQ(recovery299.poll_count,total);
    FOR_LOOP(i,MIN(MIN(recovery299.poll_count,12000),total)) {
        attack299Poll_t const *a=recovery299.polls+i,*b=attack299_polls+i;
        bool equal=a->scene==b->scene && a->phase==b->phase && a->role==b->role && a->count==b->count && !memcmp(a->words,b->words,b->count*sizeof(unsigned));
        if(!equal) {
            if(!recovery299.poll_bad) {
                fprintf(stderr,"RECOVERY299 FIRST POLL %u actual=%u/%u/%u native=%u/%u/%u\n",i,a->scene,a->phase,a->role,b->scene,b->phase,b->role);
                FOR_LOOP(k,b->count)fprintf(stderr,"RECOVERY299 WORD %u actual=%08x native=%08x\n",k,a->words[k],b->words[k]);
            }
            recovery299.poll_bad++;
        }
    }
    fprintf(stderr,"RECOVERY299 COUNTS public=%u mismatches=%u motion=%u mismatches=%u\n",recovery299.public_count,recovery299.public_bad,recovery299.poll_count,recovery299.poll_bad);
    T_EQ(recovery299.poll_bad,0);
}
static void Recovery299Run(bool saved) {
    char const *map=getenv("WC3_ATTACK299_MAP");
    T_ASSERT(map && *map);if(!map || !*map)return;
    reset_entities();setup_test_world();G_BotShutdown();
    /* As in wc3_journey.replay: release ownership of the static test world
     * before the actual map loader installs its heap-owned terrain. */
    world.map=NULL;CM_SetupTestWorldBounds(NULL);
    memset(&recovery299,0,sizeof(recovery299));
    test_preload_marker=Recovery299Marker;test_unit_position_query=Recovery299Position;
    move_test_group_arrival=Recovery299Arrival;move_test_group_committed=Recovery299Committed;
    bool loaded=globals.LoadMap(map);T_ASSERT(loaded);
    if(loaded) {
        /* G_LoadMap -> G_StartScripts already configures/seeds/executes the
         * original main. Keep that lifecycle; no borrowed RNG state. */
        static unsigned const checkpoints[]={5,60,360,660,960,1261,1265,1560,1590};
        unsigned checkpoint=0;
        recovery299Run_t *branch=saved ? malloc(sizeof(*branch)) : NULL;
        if(saved)T_NOT_NULL(branch);
        while(!recovery299.complete && level.time<181000) {
            level.time+=5;globals.RunFrame();
            if(jass_rterror_pending(level.vm))break;
            if(saved && branch && checkpoint<(sizeof(checkpoints)/sizeof(*checkpoints)) && recovery299.tick>=checkpoints[checkpoint]) {
                /* Save only after the real frame returns. No pose, RNG, timer
                 * deadline or expected row is installed by this observer. */
                fprintf(stderr,"RECOVERY299 SAVE tick=%u scene=%u public=%u motion=%u\n",
                    recovery299.tick,recovery299.scene,recovery299.public_count,recovery299.poll_count);
                cstring_t path=Test_TempPath("attack299-cold.bin");
                bool written=WriteGame(path);T_ASSERT(written);
                if(!written)break;
                *branch=recovery299;
                /* ReadGame settles the outgoing clock owner by0.2s. That
                 * discarded branch can emit callbacks before restoration.
                 * Roll back this observer's recorded prefix with its checkpoint,
                 * as in the saved Patrol replay; gameplay restores only via
                 * ReadGame. Retain outgoing marker rows separately in the log. */
                recovery299_outgoing=true;
                S_ClearMoveGroups();
                bool restored=ReadGame(path);T_ASSERT(restored);remove(path);
                recovery299_outgoing=false;
                fprintf(stderr,"RECOVERY299 OUTGOING COUNTS public=%u motion=%u\n",
                    recovery299.public_count-branch->public_count,recovery299.poll_count-branch->poll_count);
                if(!restored)break;
                recovery299=*branch;checkpoint++;
            }
        }
        if(saved)T_EQ(checkpoint,(sizeof(checkpoints)/sizeof(*checkpoints)));
        free(branch);
        T_ASSERT(recovery299.complete);T_ASSERT(!jass_rterror_pending(level.vm));Recovery299Check();
    }
    test_preload_marker=NULL;test_unit_position_query=NULL;
    move_test_group_arrival=NULL;move_test_group_committed=NULL;
    G_ReleaseLevel();CM_W3ClearMapData();setup_test_world();reset_entities();
}
TEST(stress_wc3_attack_recovery299, full_public_and_ordered_motion) {Recovery299Run(false);}
TEST(stress_wc3_attack_recovery299, path_cleared_saved_public_and_ordered_motion) {Recovery299Run(true);}
#endif
