#ifndef R_PROFILE_H
#define R_PROFILE_H

typedef enum {
    RP_OTHER, RP_FOG, RP_WORLD, RP_ENTITIES, RP_ALPHA, RP_PARTICLES, RP_SCALE, RP_UI,
    RP_SWAP, RP_SLEEP, RP_COUNT
} renderPass_t;

typedef struct {
    uint32_t alive, batches, texfull, blends, texsum;
    uint64_t bytes;
} particlePerf_t;

void R_ProfileBeginFrame(void);
void R_ProfileStage(renderPass_t pass);
void R_ProfileEndFrame(void);
void R_ProfileShutdown(void);
void R_ProfileParticles(particlePerf_t const *stats);

#endif
