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

    K_MOUSE1 = 200,
    K_MOUSE2 = 201,
    K_MOUSE3 = 202,
    K_MWHEELUP = 203,
    K_MWHEELDOWN = 204,
};

void Key_Init(void);
void Key_SetBinding(keyCode_t key, DWORD mods, LPCSTR binding);
LPCSTR Key_GetBinding(keyCode_t key, DWORD mods);
void Key_Event(keyCode_t key, DWORD mods, bool down, DWORD time);
void Key_WriteBindings(FILE *file);

#endif
