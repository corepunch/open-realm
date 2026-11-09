#ifndef cl_key_h
#define cl_key_h

#define MAX_KEYS 256
#define KEY_MOD_SHIFT 1u // bit; left or right Shift; used as bind modifier
#define KEY_MOD_CTRL  2u // bit; left or right Ctrl; used as bind modifier
#define KEY_MOD_ALT   4u // bit; left or right Alt; used as bind modifier
#define KEY_MOD_MASK  7u // mask; SHIFT|CTRL|ALT; used to index the bind table
#define KEY_MOD_COUNT 8u // slots; 2^3 modifier combinations; used as bind-table depth

typedef unsigned char keyCode_t;

enum {
    K_TAB = 9,
    K_ENTER = 13,
    K_ESCAPE = 27,
    K_SPACE = 32,

    K_F1  = 128, K_F2  = 129, K_F3  = 130, K_F4  = 131,
    K_F5  = 132, K_F6  = 133, K_F7  = 134, K_F8  = 135,
    K_F9  = 136, K_F10 = 137, K_F11 = 138, K_F12 = 139,
    K_UPARROW = 140, K_DOWNARROW = 141, K_LEFTARROW = 142, K_RIGHTARROW = 143,
    K_PAGEUP = 144, K_PAGEDOWN = 145, K_HOME = 146, K_END = 147,
    K_LALT = 148, K_RALT = 149,

    K_MOUSE1 = 200,
    K_MOUSE2 = 201,
    K_MOUSE3 = 202,
    K_MWHEELUP = 203,
    K_MWHEELDOWN = 204,

    /* Controller buttons: JOYA..JOYRT follow SDL's standard layout, AUX1..AUX32
     * are raw joystick buttons of pads SDL has no mapping for (Quake 2 style). */
    K_JOY_A = 205, K_JOY_B, K_JOY_X, K_JOY_Y, K_JOY_LB, K_JOY_RB, K_JOY_START, K_JOY_BACK,
    K_JOY_LSTICK, K_JOY_RSTICK, K_JOY_DPUP, K_JOY_DPDOWN, K_JOY_DPLEFT, K_JOY_DPRIGHT,
    K_JOY_LT, K_JOY_RT,
    K_AUX1 = 221, /* ..K_AUX32 = 252 */
    K_AUX32 = 252,
};

void Key_Init(void);
void Key_SetBinding(keyCode_t key, uint32_t mods, cstring_t binding);
cstring_t Key_GetBinding(keyCode_t key, uint32_t mods);
void Key_Event(keyCode_t key, uint32_t mods, bool down, uint32_t time);
void Key_WriteBindings(FILE *file);
#ifdef BZ_TESTS
cstring_t Key_FindBindingForTest(keyCode_t key, uint32_t mods);
#endif

#endif
