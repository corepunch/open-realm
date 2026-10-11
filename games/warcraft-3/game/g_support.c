#include "g_local.h"
#include "games/warcraft-3/common/wc3_math.h"

/* Terrain/deck consumers share one authoritative model query. A prepared mesh
 * miss must stay a miss; only the existing unprepared/animated path uses the
 * footprint rectangle until those model poses are implemented. */
bool S_GetWalkableSupport(vec2_t point,float overlap,float *height) {
    bool hit=false;float highest=-FLT_MAX;
    for (edict_t *surface=level.ground_surfaces;surface;surface=surface->ground_next) {
        if(!surface->inuse||surface->destructable->dead||!surface->destructable->placement_solid)continue;
        float deck;
        int mesh=G_WalkableModelHeight(surface,point,&deck);
        if(!mesh)continue;
        if(mesh<0) {
            if(!surface->pathtex)continue;
            pathTexTransform_t transform=CM_GetPathTexTransform(surface);
            float cell=CM_PathCellWorldSize();
            bool elevator=surface->class_id==MAKEFOURCC('D','T','r','x')||surface->class_id==MAKEFOURCC('D','T','r','f');
            float extra=elevator?overlap:0;
            if(fabsf(point.x-surface->s.origin.x)>transform.width*cell*.5f+extra||
                fabsf(point.y-surface->s.origin.y)>transform.height*cell*.5f+extra)continue;
            deck=surface->s.origin.z+(elevator?surface->destructable->occluder_height:0);
        }
        if(!hit||deck>highest)highest=deck;
        hit=true;
    }
    if(hit)*height=highest;
    return hit;
}

float S_GetLocationSupport(vec2_t point) {
    float height=CM_GetHeightAtPoint(point.x,point.y),deck;
    if(S_GetWalkableSupport(point,0,&deck)&&deck>height)height=deck;
    float water=CM_GetWaterHeightAtPoint(point.x,point.y);
    return water>height?water:height;
}

/* Terrain+7a4: map-start flyer support is independent of live ground support.
 * Keep one immutable completed field, shared by every flyer. */
typedef struct {
    float *values;
    uint32_t width, height;
    float cell;
    vec2_t origin;
    bool finalized;
} flightSupport_t;

/* Native743810/73fc80 use SSE float arithmetic, not Warcraft software
 * scalars. Explicit stores preserve each rounding and prevent contraction. */
static float support_add(float a,float b) { volatile float r=a+b; return r; }
static float support_sub(float a,float b) { volatile float r=a-b; return r; }
static float support_mul(float a,float b) { volatile float r=a*b; return r; }
static float support_div(float a,float b) { volatile float r=a/b; return r; }

static flightSupport_t flight_support, slope_support;

/*748470 builds mode0 from four packed terrain vertices per128-unit tile.
 * It is distinct from both physical height and the maximized flyer field. */
static void slope_support_initialize(void) {
    gi.MemFree(slope_support.values); slope_support=(flightSupport_t){0};
    if (!world.map || !world.map->vertices || world.map->width<3 || world.map->height<3) return;
    flightSupport_t *f=&slope_support;
    f->width=world.map->width-1; f->height=world.map->height-1;
    f->cell=TILE_SIZE; f->origin=world.map->center;
    f->values=gi.MemAlloc((size_t)f->width*f->height*sizeof(float));
    war3mapVertex_t const *vertices=world.map->vertices;
    FOR_LOOP(y,f->height) FOR_LOOP(x,f->width) {
        float z=0;
        /* Native corner order:00,10,01,11 (6fb85aa4/6fb85ab4). */
        static uint8_t const offsets[4][2]={{0,0},{1,0},{0,1},{1,1}};
        FOR_LOOP(k,4) {
            war3mapVertex_t const *v=vertices+(size_t)(y+offsets[k][1])*world.map->width+x+offsets[k][0];
            float h=support_sub(support_div((float)v->accurate_height-8192.0f,4),HEIGHT_COR);
            h=support_add(h,(float)v->level*TILE_SIZE);
            z=support_add(h,z);
        }
        f->values[(size_t)y*f->width+x]=support_mul(z,.25f);
    }
}

void S_ClearFlightSupport(void) {
    gi.MemFree(flight_support.values);
    flight_support = (flightSupport_t){0};
    gi.MemFree(slope_support.values); slope_support=(flightSupport_t){0};
}

void S_InitFlightSupport(void) {
    S_ClearFlightSupport();
    slope_support_initialize();
    if (!world.map || !world.map->vertices || world.map->width < 2 || world.map->height < 2) return;
    flightSupport_t *f = &flight_support;
    f->width = (world.map->width-1)*4;
    f->height = (world.map->height-1)*4;
    f->cell = TILE_SIZE/4;
    f->origin = world.map->center;
    f->values = gi.MemAlloc((size_t)f->width*f->height*sizeof(float));
    war3mapVertex_t const *vertices = world.map->vertices;
    int previous_y = -1;
    FOR_LOOP(y,f->height) {
        uint32_t vy = (y+2)/4;
        float *row = f->values+(size_t)y*f->width;
        if ((int)vy == previous_y) {
            memcpy(row,row-f->width,f->width*sizeof(float));
            continue;
        }
        previous_y = vy;
        int previous_x = -1;
        float z = 0;
        FOR_LOOP(x,f->width) {
            uint32_t vx = (x+2)/4;
            if ((int)vx != previous_x) {
                previous_x = vx;
                float sx = f->origin.x+vx*TILE_SIZE, sy = f->origin.y+vy*TILE_SIZE;
                z = CM_GetHeightAtPoint(sx,sy);
                if (vertices[(size_t)vy*world.map->width+vx].water)
                    z = MAX(z,CM_GetWaterHeightAtPoint(sx,sy));
            }
            row[x] = z;
        }
    }
}

/* Retail74d4b0 compares the left endpoint, strict-rise/plateau/fall peaks,
 * then the right endpoint. Preserve that candidate order even for signed
 * zero. A deque over those peaks makes each pass O(cells), for any radius. */
static void flight_support_maximum(float const *in, float *out, uint32_t width,
                                   uint32_t height, uint32_t radius, uint32_t *queue) {
    FOR_LOOP(y,height) {
        uint32_t head = 0, tail = 0, next = 1;
        bool rising = false;
        float const *row = in+(size_t)y*width;
        FOR_LOOP(x,width) {
            uint32_t last = (uint32_t)MIN((uint64_t)x+radius,width-1);
            uint32_t first = x > radius ? x-radius : 0;
            while (next <= last) {
                if (rising) {
                    if (row[next-1] > row[next]) {
                        uint32_t peak = next-1;
                        while (tail > head && row[queue[tail-1]] < row[peak]) tail--;
                        queue[tail++] = peak;
                        rising = false;
                    }
                } else if (row[next] > row[next-1]) rising = true;
                next++;
            }
            while (head < tail && queue[head] <= first) head++;
            float value = row[first];
            if (head < tail && row[queue[head]] > value) value = row[queue[head]];
            if (row[last] > value) value = row[last];
            out[(size_t)x*height+y] = value;
        }
    }
}

static uint32_t flight_support_setting(cstring_t name) {
    cstring_t value = Stb_IniCacheFind(&game.config.misc,"FlyerMap",name);
    int32_t n = value ? (int32_t)strtol(value,NULL,10) : 0;
    return n > 0 ? (uint32_t)n : 0;
}

void S_FinalizeFlightSupport(void) {
    flightSupport_t *f = &flight_support;
    if (f->finalized) return;
    if (!f->values) S_InitFlightSupport();
    if (!f->values) return;
    /* 24ffc0 enumerates initial widgets before the maximum/smoothing passes.
     * Its rectangle is inclusive; a texture's upper edge contributes too. */
    FOR_LOOP(i,globals.num_edicts) {
        edict_t const *ent = g_edicts+i;
        if (!ent->inuse || !G_IsDestructable(ent) || !ent->data.DestructableData || !ent->pathtex ||
            !ent->destructable->placement_solid) continue;
        float lift = ent->data.DestructableData->flyHeight;
        pathTexTransform_t t = CM_GetPathTexTransform(ent);
        int cx = (int)((ent->s.origin.x-f->origin.x)/f->cell);
        int cy = (int)((ent->s.origin.y-f->origin.y)/f->cell);
        int x0 = cx-(int)t.width/2, y0 = cy-(int)t.height/2;
        int x1 = MIN((int)f->width-1,x0+(int)t.width);
        int y1 = MIN((int)f->height-1,y0+(int)t.height);
        for (int y = MAX(0,y0); y <= y1; y++) for (int x = MAX(0,x0); x <= x1; x++) {
            float z = CM_GetHeightAtPoint(f->origin.x+((x+2)/4)*TILE_SIZE,
                                         f->origin.y+((y+2)/4)*TILE_SIZE);
            float *value = f->values+(size_t)y*f->width+x;
            z = support_add(z,lift);
            if (*value < z) *value = z;
        }
    }
    uint32_t radius = flight_support_setting("MaximizeRadius");
    float *scratch = gi.MemAlloc((size_t)f->width*f->height*sizeof(float));
    uint32_t *queue = gi.MemAlloc(MAX(f->width,f->height)*sizeof(uint32_t));
    flight_support_maximum(f->values,scratch,f->width,f->height,radius,queue);
    flight_support_maximum(scratch,f->values,f->height,f->width,radius,queue);
    gi.MemFree(queue);
    uint32_t levels = flight_support_setting("SmoothLevels");
    while (levels-- && f->width >= 4 && f->height >= 4) {
        uint32_t width = f->width/2, height = f->height/2;
        FOR_LOOP(y,height) FOR_LOOP(x,width) {
            float const *a = f->values+(size_t)(y*2)*f->width+x*2;
            float z = support_add(a[0],0);
            z = support_add(z,a[1]); z = support_add(z,a[f->width]); z = support_add(z,a[f->width+1]);
            scratch[(size_t)y*width+x] = support_mul(z,0.25f);
        }
        memcpy(f->values,scratch,(size_t)width*height*sizeof(float));
        f->width = width; f->height = height; f->cell *= 2;
    }
    gi.MemFree(scratch);
    float *completed = gi.MemAlloc((size_t)f->width*f->height*sizeof(float));
    memcpy(completed,f->values,(size_t)f->width*f->height*sizeof(float));
    gi.MemFree(f->values); f->values = completed; f->finalized = true;
}

static bool support_interpolate(flightSupport_t const *f,vec2_t point,float *height) {
    if (!f->values) return false;
    float x = support_sub(support_div(support_sub(point.x,f->origin.x),f->cell),0.5f);
    float y = support_sub(support_div(support_sub(point.y,f->origin.y),f->cell),0.5f);
    int ix = (int)x, iy = (int)y;
    x = support_sub(x,(float)ix); y = support_sub(y,(float)iy);
    if (ix < 0) ix = 0,x = 0;
    else if (ix > (int)f->width-2) ix = f->width-2,x = 0;
    if (iy < 0) iy = 0,y = 0;
    else if (iy > (int)f->height-2) iy = f->height-2,y = 0;
    float const *a = f->values+(size_t)iy*f->width+ix;
    float nx = support_sub(1,x), ny = support_sub(1,y);
    float z = support_mul(support_mul(a[1],x),ny);
    z = support_add(z,support_mul(support_mul(nx,a[0]),ny));
    z = support_add(z,support_mul(support_mul(nx,a[f->width]),y));
    *height = support_add(z,support_mul(support_mul(a[f->width+1],x),y));
    return true;
}

bool S_GetFlightSupport(vec2_t point,float *height) {
    return support_interpolate(&flight_support,point,height);
}

static vec3_t slope_cross(vec3_t a,vec3_t b) {
    return (vec3_t){support_sub(support_mul(a.y,b.z),support_mul(a.z,b.y)),
        support_sub(support_mul(a.z,b.x),support_mul(a.x,b.z)),
        support_sub(support_mul(a.x,b.y),support_mul(a.y,b.x))};
}
static vec3_t slope_normalize(vec3_t v) {
    float square=support_add(support_mul(v.y,v.y),support_mul(v.x,v.x));
    square=support_add(square,support_mul(v.z,v.z));
    float inverse=support_div(1,(float)sqrt((double)square));
    return (vec3_t){support_mul(v.x,inverse),support_mul(v.y,inverse),support_mul(v.z,inverse)};
}

/*66d990: the ordinary ground-unit three-tap presentation path. Never change
 * the actor heading or physical support. Flying bank and zero-radius triangle
 * normals have separate owners; those paths keep the existing upright pose. */
void S_RefreshUnitSlope(edict_t *self) {
    self->s.pose_offset=(orientation_t){0};
    UnitUI_t const *ui=self->data.UnitUI;
    if (!ui || (self->aiflags & AI_FLYING)) return;
    float pitch_limit=wc3_degrees_to_radians(ui->maxPitchDegrees);
    float roll_limit=wc3_degrees_to_radians(ui->maxRollDegrees);
    if (fabsf(pitch_limit)<.001f && fabsf(roll_limit)<.001f) return;
    float radius=ui->elevationSampleRadius;
    if (fabsf(radius)<2.384185791015625e-7f) return;
    if (!slope_support.values) slope_support_initialize();
    if (!slope_support.values) return;
    float heading=self->s.angle;
    vec3_t forward={(float)cos((double)heading),(float)sin((double)heading),0};
    float rear=-radius,low=support_sub(heading,.78539818525314331055f),high=support_add(heading,.78539818525314331055f);
    vec3_t points[3]={
        {support_add(self->s.origin.x,support_mul(forward.x,rear)),support_add(self->s.origin.y,support_mul(forward.y,rear)),0},
        {support_add(self->s.origin.x,support_mul((float)cos((double)low),radius)),support_add(self->s.origin.y,support_mul((float)sin((double)low),radius)),0},
        {support_add(self->s.origin.x,support_mul((float)cos((double)high),radius)),support_add(self->s.origin.y,support_mul((float)sin((double)high),radius)),0}};
    float maximum=-FLT_MAX;
    FOR_LOOP(i,3) {
        float z,deck;
        support_interpolate(&slope_support,(vec2_t){points[i].x,points[i].y},&z);
        if(S_GetWalkableSupport((vec2_t){points[i].x,points[i].y},0,&deck) && deck>z)z=deck;
        points[i].z=z;
        if(maximum<=z)maximum=z;
    }
    float discontinuity=support_mul(radius,2.5f);
    FOR_LOOP(i,3)if(support_sub(maximum,points[i].z)>=discontinuity)points[i].z=maximum;
    vec3_t a={support_sub(points[1].x,points[0].x),support_sub(points[1].y,points[0].y),support_sub(points[1].z,points[0].z)};
    vec3_t b={support_sub(points[2].x,points[0].x),support_sub(points[2].y,points[0].y),support_sub(points[2].z,points[0].z)};
    vec3_t normal=slope_normalize(slope_cross(a,b));
    vec3_t right=slope_cross(normal,forward);
    forward=slope_normalize(slope_cross(right,normal)); right=slope_normalize(right);
    float yaw,pitch,roll;
    if(forward.z>=1 || forward.z<=-1) {
        /*1c7ba0 singular branch; authored positive radii ordinarily stay regular. */
        yaw=(float)atan2((double)-right.x,(double)right.y);
        pitch=forward.z>=1?-(float)M_PI_2:(float)M_PI_2;roll=0;
    } else {
        yaw=(float)atan2((double)forward.y,(double)forward.x);
        pitch=(float)asin((double)-forward.z);
        roll=(float)atan2((double)right.z,(double)normal.z);
    }
    if(pitch<-pitch_limit)pitch=-pitch_limit;else if(pitch>pitch_limit)pitch=pitch_limit;
    if(roll<-roll_limit)roll=-roll_limit;else if(roll>roll_limit)roll=roll_limit;
    float offset=support_sub(yaw,heading);
    if(offset<-(float)M_PI)offset=support_add(offset,(float)(2*M_PI));
    else if(offset>(float)M_PI)offset=support_sub(offset,(float)(2*M_PI));
    self->s.pose_offset=(orientation_t){offset,-pitch,roll};
}

/* The completed values describe map-start history, including objects later
 * removed. Rebuilding from the currently live edicts would change a replay. */
bool S_WriteFlightSupport(FILE *file) {
    flightSupport_t const *f = &flight_support;
    uint32_t header[] = { f->width,f->height,wc3_float_bits(f->cell),
        wc3_float_bits(f->origin.x),wc3_float_bits(f->origin.y),f->finalized };
    size_t count = (size_t)f->width*f->height;
    return fwrite(header,sizeof(header),1,file) == 1 &&
        (!count || fwrite(f->values,sizeof(float),count,file) == count);
}

bool S_ReadFlightSupport(FILE *file) {
    uint32_t h[6];
    if (fread(h,sizeof(h),1,file) != 1) return false;
    flightSupport_t next = { .width = h[0], .height = h[1],
        .cell = wc3_float(h[2]), .origin = {wc3_float(h[3]),wc3_float(h[4])},
        .finalized = h[5] != 0 };
    if (h[5] > 1 || (!!h[0] != !!h[1])) return false;
    if (!h[0]) {
        if (h[2] || h[3] || h[4] || h[5]) return false;
        S_ClearFlightSupport(); return true;
    }
    if (!world.map || world.map->width < 2 || world.map->height < 2 ||
        !isfinite(next.cell) || next.cell < TILE_SIZE/4 ||
        !isfinite(next.origin.x) || !isfinite(next.origin.y) ||
        next.origin.x != world.map->center.x || next.origin.y != world.map->center.y) return false;
    uint32_t width = (world.map->width-1)*4, height = (world.map->height-1)*4;
    float cell = TILE_SIZE/4;
    while (cell < next.cell && width >= 4 && height >= 4) width /= 2,height /= 2,cell *= 2;
    if (next.cell != cell || next.width != width || next.height != height ||
        (!next.finalized && cell != TILE_SIZE/4)) return false;
    size_t count = (size_t)next.width*next.height;
    long start = ftell(file);
    if (start < 0 || fseek(file,0,SEEK_END)) return false;
    long end = ftell(file);
    if (fseek(file,start,SEEK_SET) || end < start || count > (size_t)(end-start)/sizeof(float)) return false;
    next.values = gi.MemAlloc(count*sizeof(float));
    if (fread(next.values,sizeof(float),count,file) != count) {
        gi.MemFree(next.values); return false;
    }
    FOR_LOOP(i,count) if (!isfinite(next.values[i])) {
        gi.MemFree(next.values); return false;
    }
    S_ClearFlightSupport(); flight_support = next; return true;
}

#ifdef BZ_TESTS
#include "shared/test.h"
#include "tests/fixtures/retail_flight_support228.h"

TEST(wc3_support, maximum_filter_matches_original_instructions) {
    FOR_LOOP(c,sizeof(flight228_max)/sizeof(*flight228_max)) {
        size_t count = (size_t)flight228_max[c].width*flight228_max[c].height;
        float *input = gi.MemAlloc(count*sizeof(float));
        float *output = gi.MemAlloc(count*sizeof(float));
        uint32_t *queue = gi.MemAlloc(flight228_max[c].width*sizeof(uint32_t));
        FOR_LOOP(i,count) input[i] = wc3_float(flight228_max[c].input[i]);
        flight_support_maximum(input,output,flight228_max[c].width,
            flight228_max[c].height,flight228_max[c].radius,queue);
        FOR_LOOP(i,count) T_EQ(wc3_float_bits(output[i]),flight228_max[c].output[i]);
        gi.MemFree(queue); gi.MemFree(output); gi.MemFree(input);
    }
}

TEST(wc3_support, interpolation_matches_original_instructions_at_edges_and_negative_coordinates) {
    flightSupport_t saved = flight_support;
    float grid[40];
    FOR_LOOP(i,40) grid[i] = wc3_float(flight228_grid[i]);
    flight_support = (flightSupport_t){ .values=grid,.width=8,.height=5,
        .cell=96,.origin={-352,224},.finalized=true };
    FOR_LOOP(i,sizeof(flight228_samples)/sizeof(*flight228_samples)) {
        float z = 0;
        T_ASSERT(S_GetFlightSupport((vec2_t){wc3_float(flight228_samples[i].x),
            wc3_float(flight228_samples[i].y)},&z));
        T_EQ(wc3_float_bits(z),flight228_samples[i].z);
    }
    flight_support = saved;
}

TEST(wc3_support, field_codec_preserves_native_grid_and_rejects_invalid_payloads_atomically) {
    extern void reset_entities(void), setup_test_world(void);
    reset_entities(); setup_test_world();
    world.map->width = world.map->height = 17;
    world.map->center = (vec2_t){0,0};
    flight_support = (flightSupport_t){ .values=gi.MemAlloc(64*sizeof(float)),
        .width=8,.height=8,.cell=256,.finalized=true };
    FOR_LOOP(i,64) flight_support.values[i] = wc3_float(flight228_map[i]);
    FILE *file = tmpfile(); T_NOT_NULL(file);
    T_ASSERT(S_WriteFlightSupport(file)); rewind(file);
    S_ClearFlightSupport(); T_ASSERT(S_ReadFlightSupport(file)); fclose(file);
    FOR_LOOP(i,64) T_EQ(wc3_float_bits(flight_support.values[i]),flight228_map[i]);
    FOR_LOOP(c,8) {
        uint32_t h[] = {8,8,wc3_float_bits(256),0,0,1};
        uint32_t values[64]; memcpy(values,flight228_map,sizeof(values));
        switch (c) {
        case 0: h[0] = UINT32_MAX; break;
        case 1: h[2] = wc3_float_bits(192); break;
        case 2: h[3] = wc3_float_bits(32); break;
        case 3: h[5] = 2; break;
        case 4: h[5] = 0; break;
        case 5: values[63] = 0x7fc00000; break;
        case 6: h[0] = h[1] = 0; break;
        case 7: break; /* Truncated values. */
        }
        float *saved = flight_support.values;
        file = tmpfile(); T_NOT_NULL(file);
        T_EQ(fwrite(h,sizeof(h),1,file),1);
        T_EQ(fwrite(values,sizeof(uint32_t),c==7 ? 63 : 64,file),c==7 ? 63 : 64);
        rewind(file); T_ASSERT(!S_ReadFlightSupport(file)); fclose(file);
        T_ASSERT(flight_support.values == saved);
        FOR_LOOP(i,64) T_EQ(wc3_float_bits(saved[i]),flight228_map[i]);
    }
    S_ClearFlightSupport(); reset_entities(); setup_test_world();
}
#endif

#ifdef BZ_TESTS
#include "tests/fixtures/retail_slope272.h"
TEST(wc3_support, presentation_height_grid_matches_read_only_retail_capture) {
    extern void reset_entities(void),setup_test_world(void);
    reset_entities();setup_test_world();
    world.map->width=world.map->height=17;world.map->center=(vec2_t){0,0};
    war3mapVertex_t *v=world.map->vertices;
    FOR_LOOP(y,17)FOR_LOOP(x,17) {
        bool water=y<=10 && x>=4 && x<=12;
        v[y*17+x]=(war3mapVertex_t){.accurate_height=water?
            0x2000-4*((x==4||x==5||x==11||x==12)?64:192):0x2000,
            .waterlevel=8550,.water=water,.level=y>=12&&x>=8?3:2};
    }
    S_InitFlightSupport();T_EQ(slope_support.width,16);T_EQ(slope_support.height,16);
    FOR_LOOP(i,256)T_EQ(wc3_float_bits(slope_support.values[i]),slope272_grid[i]);
    reset_entities();setup_test_world();
}
#endif
