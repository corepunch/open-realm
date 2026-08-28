#ifndef galaxy_host_h
#define galaxy_host_h

#include "games/warcraft-3/jass/jass_api.h"

/* galaxy_set_script_dir — override the default "data/TRaynor01-galaxy" base
 * path used when resolving Galaxy include directives and script file paths. */
void galaxy_set_script_dir(LPCSTR dir);

/* galaxy_open — set up JASSHOST, load MapScript (includes TriggerLibs via its
 * own include directives), return VM.  Returns NULL on load failure. */
LPJASS galaxy_open(HANDLE (*readfile)(LPCSTR, DWORD *),
                   DWORD  (*gettime)(void),
                   HANDLE (*memalloc)(long),
                   void   (*memfree)(HANDLE));

/* galaxy_start — register map triggers by calling InitTriggers. */
void galaxy_start(LPJASS vm);

/* galaxy_fire_mapinit — fire the MapInit event; call after galaxy_start(). */
void galaxy_fire_mapinit(LPJASS vm);

/* galaxy_tick — pump pending coroutines; call once per server frame. */
void galaxy_tick(LPJASS vm);

/* galaxy_close — destroy VM and reset all tables. */
void galaxy_close(LPJASS vm);

/* galaxy_reset — reset trigger/unit/point/camera tables without closing VM. */
void galaxy_reset(void);

/* galaxy_get_natives — return the SC2 native function table for JASSHOST. */
LPCJASSMODULE galaxy_get_natives(void);

/* -------------------------------------------------------------------------
 * Callbacks from Galaxy natives into g_sc2.c.
 * Set these function pointers during SC2_InitGalaxyHost() before galaxy_open.
 * ------------------------------------------------------------------------- */

/* CameraApplyInfo — move the client camera (duration=0: instant snap). */
extern void (*sc2_galaxy_on_camera)(float target_x, float target_y,
                                    float yaw, float pitch,
                                    float distance, float fov,
                                    float duration);

/* CinematicMode — toggle letterbox bars on/off. */
extern void (*sc2_galaxy_on_cinematic)(BOOL enable, float duration);

/* CinematicFade — set screen fade alpha (0=clear, 1=black). */
extern void (*sc2_galaxy_on_fade)(float alpha, float duration);

/* UnitCreate — spawn a unit entity; returns LPEDICT cast to void*, or NULL. */
extern void *(*sc2_galaxy_on_unit_create)(LPCSTR model, int player,
                                          float x, float y, float angle);

/* Map data lookups — filled from sc2_map objects by g_sc2.c */

/* Camera lookup: fills target, orientation, optics; returns false if not found */
extern BOOL (*sc2_galaxy_get_camera_by_id)(DWORD map_id,
    float *target_x, float *target_y, float *target_z,
    float *pitch, float *yaw, float *distance, float *fov);

/* Point lookup: fills x, y from a POINT-type map object; returns false if not found */
extern BOOL (*sc2_galaxy_get_point_by_id)(DWORD map_id, float *x, float *y);

/* Unit model: resolves unit type name to M3 model path; returns "" if unknown */
extern const char *(*sc2_galaxy_get_unit_model)(LPCSTR unit_type);

/* Entity operations via unit handle pointer */
extern void (*sc2_galaxy_unit_set_position)(void *ent, float x, float y, float facing);
extern BOOL (*sc2_galaxy_unit_is_alive)(void *ent);

#endif
