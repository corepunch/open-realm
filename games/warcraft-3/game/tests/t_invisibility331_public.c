#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "jass/jass.h"

/* Private-asset opt-in: the expected file must come from the accepted original
 * observation/control bundle. Never derive expected rows from this engine.
 * Load the captured map itself, including its config/main, overrides and seed. */
extern void (*test_preload_marker)(cstring_t);
void reset_entities(void);
void setup_test_world(void);
void CM_SetupTestWorldBounds(box2_t const *);

typedef struct {
    char **rows;
    size_t count,seen,bad;
    char prefix[64];
    bool complete;
} invisibility331Journal_t;
static invisibility331Journal_t invisibility331;

static void Invisibility331Free(void) {
    for(size_t i=0;i<invisibility331.count;i++)free(invisibility331.rows[i]);
    free(invisibility331.rows);
    memset(&invisibility331,0,sizeof(invisibility331));
}
static bool Invisibility331Read(cstring_t path) {
    FILE *file=fopen(path,"rb");
    if(!file)return false;
    char line[4096];bool valid=true;
    while(fgets(line,sizeof(line),file)) {
        size_t len=strlen(line);
        /* Newline-delimited UTF-8 markers; transport CRLF is not gameplay. */
        if(!len || line[len-1]!='\n') {valid=false;break;}
        line[--len]=0;if(len && line[len-1]=='\r')line[--len]=0;
        if(!len || invisibility331.count>=100000) {valid=false;break;}
        if(!invisibility331.count) {
            char *space=strchr(line,' ');
            size_t prefix=space ? (size_t)(space-line)+1 : 0;
            if(!prefix || prefix>=sizeof(invisibility331.prefix)) {valid=false;break;}
            memcpy(invisibility331.prefix,line,prefix);invisibility331.prefix[prefix]=0;
        }
        if(strncmp(line,invisibility331.prefix,strlen(invisibility331.prefix))) {valid=false;break;}
        char *row=malloc(len+1);
        if(!row) {valid=false;break;}
        memcpy(row,line,len+1);
        char **rows=realloc(invisibility331.rows,(invisibility331.count+1)*sizeof(*rows));
        if(!rows) {free(row);valid=false;break;}
        invisibility331.rows=rows;rows[invisibility331.count++]=row;
    }
    if(ferror(file))valid=false;
    fclose(file);
    char const *complete=invisibility331.count ?
        strstr(invisibility331.rows[invisibility331.count-1]," label=complete") : NULL;
    return valid && complete && !strcmp(complete," label=complete");
}
/* Match C299's declared FourCC representation adapter. Game storage is
 * little-endian rawcode; native JASS prints the same four-character identity
 * in big-endian integer form. No clock, position, flag or ordering is changed. */
static void Invisibility331FourCC(char *line) {
    char *p=strstr(line," code=");if(!p)return;
    p+=6;char *end;uint32_t value=(uint32_t)strtoul(p,&end,10);
    uint32_t native=((value&255u)<<24)|((value&0xff00u)<<8)|((value>>8)&0xff00u)|(value>>24);
    char digits[32];snprintf(digits,sizeof(digits),"%u",native);
    size_t length=strlen(digits);memmove(p+length,end,strlen(end)+1);memcpy(p,digits,length);
}
static void Invisibility331Marker(cstring_t text) {
    if(strncmp(text,invisibility331.prefix,strlen(invisibility331.prefix)))return;
    size_t index=invisibility331.seen++;
    fprintf(stderr,"INVISIBILITY331 PUBLIC %s\n",text);
    char line[4096];snprintf(line,sizeof(line),"%s",text);
    Invisibility331FourCC(line);
    if(index>=invisibility331.count || strcmp(line,invisibility331.rows[index])) {
        if(!invisibility331.bad)fprintf(stderr,"INVISIBILITY331 FIRST %zu\n actual %s\n native %s\n",
            index,line,index<invisibility331.count ? invisibility331.rows[index] : "<extra>");
        invisibility331.bad++;
    }
    /* Keep journaling the rest of this frame, including unexpected trailing
     * markers; reaching the end never suppresses extra public output. */
    if(!strcmp(text,invisibility331.rows[invisibility331.count-1]))invisibility331.complete=true;
}
TEST(stress_wc3_invisibility331,full_public_protocol) {
    cstring_t map=getenv("WC3_INVISIBILITY331_MAP");
    cstring_t expected=getenv("WC3_INVISIBILITY331_EXPECTED");
    if((!map || !*map) && (!expected || !*expected))return;
    T_ASSERT(map && *map && expected && *expected);
    if(!map || !*map || !expected || !*expected)return;
    memset(&invisibility331,0,sizeof(invisibility331));
    bool input=Invisibility331Read(expected);T_ASSERT(input);
    if(!input) {Invisibility331Free();return;}
    reset_entities();setup_test_world();G_BotShutdown();
    /* The test world's terrain is static; the real loader owns its map. */
    world.map=NULL;CM_SetupTestWorldBounds(NULL);
    void (*previous_marker)(cstring_t)=test_preload_marker;
    test_preload_marker=Invisibility331Marker;
    bool loaded=globals.LoadMap(map);T_ASSERT(loaded);
    if(loaded) {
        /* No extra main/PathProbeInit invocation or injected clock/pose/RNG. */
        for(unsigned frame=0;frame<48000 && !invisibility331.complete;frame++) {
            if(!level.vm || jass_rterror_pending(level.vm))break;
            level.time+=5;globals.RunFrame();
        }
        T_ASSERT(level.vm && !jass_rterror_pending(level.vm));
        T_ASSERT(invisibility331.complete);
        T_EQ(invisibility331.seen,invisibility331.count);
        T_EQ(invisibility331.bad,0);
        fprintf(stderr,"INVISIBILITY331 COUNTS public=%zu expected=%zu mismatches=%zu complete=%u\n",
            invisibility331.seen,invisibility331.count,invisibility331.bad,invisibility331.complete);
    }
    test_preload_marker=previous_marker;
    G_ReleaseLevel();CM_W3ClearMapData();setup_test_world();reset_entities();
    Invisibility331Free();
}
#endif
