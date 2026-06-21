#include "cl_input_local.h"

//struct {
//    LPCSTR command;
//    DWORD key;
//} hotkey_t;
//
//hotkey_t hotkeys = {
//    { "show_quests",  }
//};

mouseEvent_t mouse;
static keyCode_t mouse_button_keys[8];

BOOL CL_AltModifierDown(void) {
    SDL_Keymod const mod = SDL_GetModState();
    return (mod & (KMOD_LALT | KMOD_RALT)) != 0;
}

static keyCode_t CL_MouseButtonKey(SDL_MouseButtonEvent const *button) {
    keyCode_t key = 0;

    if (!button) {
        return 0;
    }
    switch (button->button) {
        case SDL_BUTTON_LEFT:
            key = K_MOUSE1;
            break;
        case SDL_BUTTON_RIGHT:
            key = K_MOUSE2;
            break;
        case SDL_BUTTON_MIDDLE:
            key = K_MOUSE3;
            break;
        default:
            return 0;
    }
    if (cls.key_dest == key_game && CL_AltModifierDown()) {
        key = (keyCode_t)(K_ALT_MOUSE1 + key - K_MOUSE1);
    }
    return key;
}

BOOL CL_MouseOverGameplayUI(void) {
    return ui.HitTestLayout ? ui.HitTestLayout((int)mouse.origin.x, (int)mouse.origin.y) : false;
}

BOOL CL_GameplayInputReady(void) {
    return cls.key_dest == key_game &&
           cls.state == ca_active &&
           cl.playerstate.client_ui_state == CLIENT_UI_GAME;
}

void CL_Input(void) {
    SDL_Event event;

    mouse.event = UI_EVENT_NONE;
    mouse.wheel = 0;
    while(SDL_PollEvent(&event)) {
        switch(event.type) {
            case SDL_MOUSEBUTTONDOWN:
                {
                    keyCode_t mousevt = CL_MouseButtonKey(&event.button);
                    mouse.origin.x = event.button.x;
                    mouse.origin.y = event.button.y;
                    if (cls.key_dest == key_game) {
                        CL_InputModeMouseButton(&event.button, true);
                    }
                    if (mousevt && cls.key_dest != key_console) {
                        mouse_button_keys[event.button.button] = mousevt;
                        Key_Event(mousevt, true, event.button.timestamp);
                    }
                }
                break;
            case SDL_MOUSEBUTTONUP:
                {
                    keyCode_t mousevt = event.button.button < sizeof(mouse_button_keys)
                                      ? mouse_button_keys[event.button.button]
                                      : 0;
                    mouse.origin.x = event.button.x;
                    mouse.origin.y = event.button.y;
                    if (cls.key_dest == key_game) {
                        CL_InputModeMouseButton(&event.button, false);
                    }
                    if (mousevt && cls.key_dest != key_console) {
                        Key_Event(mousevt, false, event.button.timestamp);
                        mouse_button_keys[event.button.button] = 0;
                    }
                }
                break;
            case SDL_MOUSEMOTION:
                mouse.origin.x = event.motion.x;
                mouse.origin.y = event.motion.y;
                break;
            case SDL_MOUSEWHEEL:
                {
                    int x;
                    int y;

                    SDL_GetMouseState(&x, &y);
                    mouse.origin.x = x;
                    mouse.origin.y = y;
                    mouse.wheel += event.wheel.y;
                }
                break;
        }
        
        switch(event.type) {
            case SDL_TEXTINPUT:
                if (cls.key_dest == key_console) {
                    CON_TextInput(event.text.text);
                } else if (cls.key_dest == key_menu) {
                    if (ui.TextInput) {
                        ui.TextInput(event.text.text);
                    }
                }
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_BACKQUOTE) {
                    CON_ToggleConsole();
                    break;
                }
                if (cls.key_dest == key_console) {
                    CON_KeyEvent(event.key.keysym.sym, true);
                    break;
                }
                if (cls.key_dest == key_game &&
                    CL_HandleGameKey(event.key.keysym.sym, event.key.keysym.mod)) {
                    break; /* consumed by in-game handler (e.g. control groups) */
                }
                Key_Event(event.key.keysym.sym, true, event.key.timestamp);
                break;
            case SDL_KEYUP:
                if (cls.key_dest == key_console || event.key.keysym.sym == SDLK_BACKQUOTE) {
                    CON_KeyEvent(event.key.keysym.sym, false);
                    break;
                }
                Key_Event(event.key.keysym.sym, false, event.key.timestamp);
//                if(event.key.keysym.sym == SDLK_ESCAPE) {
//                    return Com_Quit();
//                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                mouse.origin.x = event.button.x;
                mouse.origin.y = event.button.y;
                mouse.button = event.button.button;
                if (cls.key_dest == key_menu) {
                    if (ui.MouseEvent) {
                        ui.MouseEvent(UI_MOUSE_DOWN, event.button.x, event.button.y, event.button.button);
                    }
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        mouse.event = UI_LEFT_MOUSE_DOWN;
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        mouse.event = UI_RIGHT_MOUSE_DOWN;
                    }
                    break;
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouse.event = UI_LEFT_MOUSE_DOWN;
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    mouse.event = UI_RIGHT_MOUSE_DOWN;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                mouse.origin.x = event.button.x;
                mouse.origin.y = event.button.y;
                mouse.button = 0;
                if (cls.key_dest == key_menu) {
                    if (ui.MouseEvent) {
                        ui.MouseEvent(UI_MOUSE_UP, event.button.x, event.button.y, event.button.button);
                    }
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        mouse.event = UI_LEFT_MOUSE_UP;
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        mouse.event = UI_RIGHT_MOUSE_UP;
                    }
                    break;
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouse.event = UI_LEFT_MOUSE_UP;
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    mouse.event = UI_RIGHT_MOUSE_UP;
                }
                break;
            case SDL_MOUSEMOTION:
                mouse.origin.x = event.motion.x;
                mouse.origin.y = event.motion.y;
                if (cls.key_dest == key_menu) {
                    if (ui.MouseEvent) {
                        ui.MouseEvent(UI_MOUSE_MOVE, event.motion.x, event.motion.y, 0);
                    }
                    break;
                }
                CL_InputModeMouseMotion(&event.motion);
                break;
            case SDL_MOUSEWHEEL:
                if (cls.key_dest == key_game && CL_InputModeMouseWheel(&event.wheel)) {
                    break;
                }
                if (cls.key_dest == key_menu && ui.MouseEvent) {
                    int x, y;
                    SDL_GetMouseState(&x, &y);
                    ui.MouseEvent(UI_MOUSE_SCROLL, x, y, UI_MOUSE_PARAM(event.wheel.x, event.wheel.y));
                }
                break;
            case SDL_WINDOWEVENT:
                switch (event.window.event) {
                    case SDL_WINDOWEVENT_CLOSE:   // exit game
                        return Com_Quit();
                    default:
                        break;
                }
                break;
        }
    }
    CL_InputModeFrame();
//    cl.viewDef.camera.origin.z = CM_GetHeightAtPoint(cl.viewDef.camera.origin.x, cl.viewDef.camera.origin.y);
}

void CL_SetMenuBindings(void) {
    cls.key_dest = key_menu;
    SDL_StartTextInput();
}

void CL_SetGameplayInput(void) {
    if (cls.key_dest != key_game) {
        fprintf(stderr, "CL_SetGameplayInput: switching key_dest %d -> key_game\n", cls.key_dest);
    }
    cls.key_dest = key_game;
    SDL_StopTextInput();
    CL_InputModeSetGameplay();
}

void CL_SetGameplayBindings(void) {
    CL_SetGameplayInput();
    cls.netchan.remote_address.type = NA_LOOPBACK;
}

void IN_SelectDown(void) {
    if (!CL_GameplayInputReady()) {
        cl.selection.in_progress = false;
        return;
    }
    /* A left-click on the minimap recenters the camera instead of selecting. */
    if (CL_TryMinimapClick(mouse.origin.x, mouse.origin.y)) {
        cl.selection.in_progress = false;
        return;
    }
    cl.selection.in_progress = true;
    cl.selection.rect.x = mouse.origin.x;
    cl.selection.rect.y = mouse.origin.y;
    cl.selection.rect.w = 0;
    cl.selection.rect.h = 0;

    if (CL_MouseOverGameplayUI()) {
        cl.selection.in_progress = false;
    }
}

void IN_SelectUp(void) {
    CL_EndMinimapDrag();
    if (!CL_GameplayInputReady()) {
        cl.selection.in_progress = false;
        return;
    }
    if (!cl.selection.in_progress)
        return;
    RECT const r = cl.selection.rect;
    cl.selection.in_progress = false;
    DWORD entnum;
    VECTOR3 point;
    if (fabs(r.w)+fabs(r.h) < 10) {
        if (re.TraceEntity(&cl.viewDef, r.x, r.y, &entnum)) {
            MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
            SZ_Printf(&cls.netchan.message, "select %d", entnum);
            
            /* Store selected entity and request UI data (Phase 8.6) */
            cl.selection.num_selected = 1;
            cl.selection.entity_nums[0] = entnum;
            CL_RequestUnitUI(1, cl.selection.entity_nums);
        }
        if (re.TraceLocation(&cl.viewDef, r.x, r.y, &point)){
            MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
            SZ_Printf(&cls.netchan.message, "point %d %d", (int)point.x, (int)point.y);
            if (cl.selection.num_selected) {
                CL_RequestUnitUI(cl.selection.num_selected, cl.selection.entity_nums);
            }
        }
    } else {
        DWORD selected[MAX_SELECTED_ENTITIES] = { 0 };
        DWORD num = re.EntitiesInRect(&cl.viewDef, &cl.selection.rect, MAX_SELECTED_ENTITIES, selected);
        char buffer[1024] = { 0 };
        if (num == 0)
            return;
        if (num > MAX_SELECTED_ENTITIES) {
            num = MAX_SELECTED_ENTITIES;
        }
        /* Shift+drag adds to the existing selection (deduped) instead of
         * replacing it, matching WC3. */
        if (SDL_GetModState() & (KMOD_LSHIFT | KMOD_RSHIFT)) {
            DWORD merged[MAX_SELECTED_ENTITIES];
            DWORD mn = 0;
            FOR_LOOP(i, cl.selection.num_selected) {
                if (mn < MAX_SELECTED_ENTITIES)
                    merged[mn++] = cl.selection.entity_nums[i];
            }
            FOR_LOOP(i, num) {
                BOOL dup = false;
                FOR_LOOP(j, mn) if (merged[j] == selected[i]) { dup = true; break; }
                if (!dup && mn < MAX_SELECTED_ENTITIES)
                    merged[mn++] = selected[i];
            }
            num = mn;
            memcpy(selected, merged, sizeof(DWORD) * mn);
        }
        strcpy(buffer, "select");
        FOR_LOOP(i, num) {
            size_t used = strlen(buffer);
            snprintf(buffer + used, sizeof(buffer) - used, " %d", selected[i]);
        }
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        SZ_Printf(&cls.netchan.message, buffer);
        
        /* Store selected entities and request UI data (Phase 8.6) */
        cl.selection.num_selected = num;
        memcpy(cl.selection.entity_nums, selected, sizeof(DWORD) * num);
        CL_RequestUnitUI(num, cl.selection.entity_nums);
    }
}

void CL_ForwardToServer_f(void) {
    extern LPCSTR current_command;
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, current_command+4);
}

void CL_InitInput(void) {
    fprintf(stderr, "Input initialization.\n");
    fprintf(stderr, "%d joysticks were found.\n", SDL_NumJoysticks());
    fprintf(stderr, "Input initialized.\n\n");

    Cmd_AddCommand("+select", IN_SelectDown);
    Cmd_AddCommand("-select", IN_SelectUp);
    Cmd_AddCommand("cmd", CL_ForwardToServer_f);
    CL_InputModeInit();
}
