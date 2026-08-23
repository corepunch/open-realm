#ifndef cl_key_h
#define cl_key_h

#define MAX_KEYS 256

typedef unsigned char keyCode_t;

enum {
    K_TAB = 9,
    K_ENTER = 13,
    K_ESCAPE = 27,
    K_SPACE = 32,
    
    K_F1  = 128, K_F2  = 129, K_F3  = 130, K_F4  = 131,
    K_F5  = 132, K_F6  = 133, K_F7  = 134, K_F8  = 135,
    K_F9  = 136, K_F10 = 137, K_F11 = 138, K_F12 = 139,

    K_MOUSE1 = 200,
    K_MOUSE2 = 201,
    K_MOUSE3 = 202,
    K_ALT_MOUSE1 = 210,
    K_ALT_MOUSE2 = 211,
    K_ALT_MOUSE3 = 212,
};

void Key_Init(void);
void Key_SetBinding(keyCode_t key, LPCSTR binding);
LPCSTR Key_GetBinding(keyCode_t key);
void Key_Event(keyCode_t key, bool down, DWORD time);
void Key_WriteBindings(FILE *file);

#endif
