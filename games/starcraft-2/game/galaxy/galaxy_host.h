#ifndef galaxy_host_h
#define galaxy_host_h

#include "games/warcraft-3/jass/jass_api.h"

/* galaxy_set_script_dir — override the default "data/TRaynor01-galaxy" base
 * path used when resolving Galaxy include directives and script file paths. */
void galaxy_set_script_dir(cstring_t dir);

/* galaxy_open — set up JASSHOST, load MapScript (includes TriggerLibs via its
 * own include directives), return VM.  Returns NULL on load failure. */
jass_t *galaxy_open(handle_t (*readfile)(cstring_t, uint32_t *),
                   uint32_t  (*gettime)(void),
                   handle_t (*memalloc)(long),
                   void   (*memfree)(handle_t));

/* galaxy_start — initialize map globals, then register map triggers. Library initialization remains diagnosed. */
void galaxy_start(jass_t *vm);

/* galaxy_fire_mapinit — fire the MapInit event; call after galaxy_start(). */
void galaxy_fire_mapinit(jass_t *vm);

/* galaxy_tick — pump pending coroutines; call once per server frame. */
void galaxy_tick(jass_t *vm);

/* galaxy_close — destroy VM and reset all tables. */
void galaxy_close(jass_t *vm);

/* galaxy_reset — reset trigger/unit/point/camera tables without closing VM. */
void galaxy_reset(void);

/* galaxy_get_natives — return the SC2 native function table for JASSHOST. */
jassModule_t const *galaxy_get_natives(void);

/* -------------------------------------------------------------------------
 * Callbacks from Galaxy natives into g_sc2.c.
 * Set these function pointers during SC2_InitGalaxyHost() before galaxy_open.
 * ------------------------------------------------------------------------- */

/* CameraApplyInfo — move the client camera (duration=0: instant snap). */
extern void (*sc2_galaxy_on_camera)(float target_x, float target_y,
                                    float yaw, float pitch,
                                    float distance, float fov, float height_offset,
                                    float duration);

/* CinematicMode — toggle letterbox bars on/off. */
extern void (*sc2_galaxy_on_cinematic)(bool enable, float duration);

/* CinematicFade — set screen fade alpha (0=clear, 1=black). */
extern void (*sc2_galaxy_on_fade)(float alpha, float duration);

/* Sound catalog lookup: returns the selected asset duration in seconds. */
extern cstring_t (*sc2_galaxy_conversation_field)(cstring_t key, cstring_t field);
extern float (*sc2_galaxy_sound_length)(cstring_t sound_id, int asset);
extern void (*sc2_galaxy_on_sound)(cstring_t sound_id, int asset);

/* UnitCreate returns edict_t *cast to void*, *or NULL. Host headings are radians; native APIs decode degrees. */
extern void *(*sc2_galaxy_on_unit_create)(cstring_t unit_type, int player,
                                          float x, float y, float angle);

/* Map data lookups — filled from sc2_map objects by g_sc2.c */

/* Camera lookup: fills target, orientation, optics; returns false if not found */
extern bool (*sc2_galaxy_get_camera_by_id)(uint32_t map_id,
    float *target_x, float *target_y, float *target_z,
    float *pitch, float *yaw, float *distance, float *fov, float *height_offset);

/* Point lookup: fills x, y from a POINT-type map object; returns false if not found */
extern bool (*sc2_galaxy_get_point_by_id)(uint32_t map_id, float *x, float *y);

/* Unit model: resolves unit type name to M3 model path; returns "" if unknown */
extern char const *(*sc2_galaxy_get_unit_model)(cstring_t unit_type);

/* Entity operations via unit handle pointer */
extern void (*sc2_galaxy_unit_set_position)(void *ent, float x, float y, float facing);
extern void (*sc2_galaxy_unit_move)(void *ent, float x, float y);
extern bool (*sc2_galaxy_unit_is_moving)(void *ent);
extern bool (*sc2_galaxy_unit_is_alive)(void *ent);
extern int (*sc2_galaxy_unit_owner)(void *ent);

/* Actor lifecycle — called when Galaxy scripts create, message, or destroy actors.
 * actor_id: 1-based index into sc2_gactors[]; unit_id: owning unit or 0. */
extern void (*sc2_galaxy_on_actor_create)(unsigned actor_id, char const *model,
                                          unsigned unit_id, float x, float y);
extern void (*sc2_galaxy_on_actor_send)(unsigned actor_id, char const *msg);
extern void (*sc2_galaxy_on_actor_destroy)(unsigned actor_id);

/* Debug-only state inspection for bounded cinematic traces. */
extern void *sc2_gunits[];
extern uint32_t sc2_gunit_n;

#endif
