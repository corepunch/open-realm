#include <ctype.h>
#include <stdarg.h>
#include <string.h>

#include "client.h"

#include <SDL2/SDL.h>

#define CON_INPUT_LEN 256
#define CON_HISTORY 32
#define CON_LINE_HEIGHT 8
#define CON_MARGIN 8

typedef struct  {
    char msg[MAX_CONSOLE_MESSAGE_LEN];
} consoleMessage_t;

static consoleMessage_t messages[MAX_CONSOLE_MESSAGES] = { 0 };
static DWORD current_message = 0;

static char con_input[CON_INPUT_LEN];
static DWORD con_cursor;
static char con_history[CON_HISTORY][CON_INPUT_LEN];
static DWORD con_history_count;
static DWORD con_history_pos;
static keydest_t con_prev_key_dest = key_game;

static void CON_PrintCvarResult(LPCSTR command);
static void CON_CompleteInput(void);

static void CON_DrawChar(int x, int y, int c) {
    re.DrawChar(x, y, c);
}

static void CON_DrawString(int x, int y, LPCSTR string) {
    if (!string) {
        return;
    }
    for (DWORD i = 0; string[i]; i++) {
        CON_DrawChar(x + i * CON_LINE_HEIGHT, y, (BYTE)string[i]);
    }
}

static void CON_DrawAltString(int x, int y, LPCSTR string) {
    if (!string) {
        return;
    }
    for (DWORD i = 0; string[i]; i++) {
        CON_DrawChar(x + i * CON_LINE_HEIGHT, y, ((BYTE)string[i]) + 128);
    }
}

static void CON_ClearInput(void) {
    con_input[0] = '\0';
    con_cursor = 0;
    con_history_pos = con_history_count;
}

static void CON_AddHistory(LPCSTR text) {
    if (!text || !*text) {
        return;
    }
    snprintf(con_history[con_history_count % CON_HISTORY],
             sizeof(con_history[0]),
             "%s",
             text);
    con_history_count++;
    con_history_pos = con_history_count;
}

static void CON_SetInput(LPCSTR text) {
    snprintf(con_input, sizeof(con_input), "%s", text ? text : "");
    con_cursor = (DWORD)strlen(con_input);
}

void CON_printf(LPCSTR fmt, ...) {
    consoleMessage_t *msg = &messages[current_message++ % MAX_CONSOLE_MESSAGES];
    va_list argptr;

    va_start(argptr,fmt);
    vsnprintf(msg->msg, sizeof(msg->msg), fmt, argptr);
    va_end(argptr);
}

static void CON_DrawFull(void) {
    size2_t window = re.GetWindowSize();
    DWORD height = MAX(window.height / 2, 120);
    DWORD rows = height / CON_LINE_HEIGHT;
    DWORD max_lines = rows > 4 ? rows - 4 : 1;
    DWORD count = MIN(current_message, MAX_CONSOLE_MESSAGES);
    DWORD first;
    DWORD y = CON_MARGIN + CON_LINE_HEIGHT;
    char prompt[CON_INPUT_LEN + 8];

    re.DrawFill(&(RECT){ 0, 0, window.width, height }, (COLOR32){ 0, 0, 0, 220 });
    re.DrawFill(&(RECT){ 0, height - 2, window.width, 2 }, (COLOR32){ 180, 160, 80, 220 });

    CON_DrawAltString(CON_MARGIN, CON_MARGIN, "OpenWarcraft3 Console");

    first = count > max_lines ? count - max_lines : 0;
    for (DWORD n = first; n < count; n++) {
        DWORD i = n % MAX_CONSOLE_MESSAGES;

        if (*messages[i].msg) {
            CON_DrawString(CON_MARGIN, y, messages[i].msg);
        }
        y += CON_LINE_HEIGHT;
    }

    snprintf(prompt, sizeof(prompt), "]%s", con_input);
    if ((cl.time / 350) & 1) {
        size_t len = strlen(prompt);
        if (len + 1 < sizeof(prompt)) {
            memmove(prompt + con_cursor + 2,
                    prompt + con_cursor + 1,
                    len - con_cursor);
            prompt[con_cursor + 1] = '_';
        }
    }
    CON_DrawString(CON_MARGIN, height - CON_LINE_HEIGHT - CON_MARGIN, prompt);
}

void CON_DrawConsole(void) {
    if (cls.key_dest == key_console) {
        CON_DrawFull();
    }
}

void CON_ToggleConsole(void) {
    if (cls.key_dest == key_console) {
        cls.key_dest = con_prev_key_dest;
        if (cls.key_dest == key_game) {
            SDL_StopTextInput();
        } else {
            SDL_StartTextInput();
        }
        CON_ClearInput();
        return;
    }

    con_prev_key_dest = cls.key_dest;
    cls.key_dest = key_console;
    SDL_StartTextInput();
    CON_ClearInput();
}

static void CON_ToggleConsole_f(void) {
    CON_ToggleConsole();
}

static void CON_Clear_f(void) {
    memset(messages, 0, sizeof(messages));
    current_message = 0;
}

void CON_TextInput(LPCSTR text) {
    size_t input_len;
    size_t text_len;

    if (cls.key_dest != key_console || !text || !*text) {
        return;
    }
    if ((!strcmp(text, "`") || !strcmp(text, "~"))) {
        return;
    }

    input_len = strlen(con_input);
    text_len = strlen(text);
    if (input_len + text_len >= sizeof(con_input)) {
        text_len = sizeof(con_input) - input_len - 1;
    }
    if (!text_len) {
        return;
    }

    memmove(con_input + con_cursor + text_len,
            con_input + con_cursor,
            input_len - con_cursor + 1);
    memcpy(con_input + con_cursor, text, text_len);
    con_cursor += (DWORD)text_len;
}

static void CON_Submit(void) {
    char command[CON_INPUT_LEN];

    if (!con_input[0]) {
        return;
    }
    snprintf(command, sizeof(command), "%s", con_input);
    CON_printf("]%s", con_input);
    CON_AddHistory(con_input);
    Cbuf_AddText(con_input);
    Cbuf_AddText("\n");
    CON_ClearInput();
    Cbuf_Execute();
    CON_PrintCvarResult(command);
}

static void CON_History(int dir) {
    DWORD count = MIN(con_history_count, CON_HISTORY);
    DWORD first = con_history_count - count;

    if (!count) {
        return;
    }

    if (dir < 0) {
        if (con_history_pos > first) {
            con_history_pos--;
        }
    } else if (con_history_pos < con_history_count) {
        con_history_pos++;
    }

    if (con_history_pos >= con_history_count) {
        CON_ClearInput();
    } else {
        CON_SetInput(con_history[con_history_pos % CON_HISTORY]);
    }
}

static void CON_FirstToken(LPCSTR text, LPSTR token, size_t token_size, LPCSTR *rest) {
    size_t len = 0;

    if (token_size > 0) {
        token[0] = '\0';
    }
    while (text && *text && isspace((unsigned char)*text)) {
        text++;
    }
    while (text && *text && !isspace((unsigned char)*text) && len + 1 < token_size) {
        token[len++] = *text++;
    }
    if (token_size > 0) {
        token[len] = '\0';
    }
    while (text && *text && isspace((unsigned char)*text)) {
        text++;
    }
    if (rest) {
        *rest = text;
    }
}

static void CON_PrintCvarResult(LPCSTR command) {
    char token[64];
    char name[64];
    LPCSTR rest;
    LPCSTR value;

    CON_FirstToken(command, token, sizeof(token), &rest);
    if (!token[0]) {
        return;
    }

    if (!strcmp(token, "set") || !strcmp(token, "seta")) {
        CON_FirstToken(rest, name, sizeof(name), NULL);
        if (!name[0]) {
            return;
        }
        value = Cvar_String(name, NULL);
        if (value) {
            CON_printf("\"%s\" is \"%s\"", name, value);
        }
        return;
    }

    value = Cvar_String(token, NULL);
    if (value) {
        CON_printf("\"%s\" is \"%s\"", token, value);
    }
}

typedef struct {
    LPCSTR partial;
} conCompletePrint_t;

static BOOL CON_CompleteNameMatches(LPCSTR name, LPCSTR partial) {
    size_t len;

    if (!name || !partial) {
        return false;
    }
    len = strlen(partial);
    return !strncasecmp(name, partial, len);
}

static void CON_PrintCompleteMatch(LPCSTR name, void *userData) {
    conCompletePrint_t *print = userData;

    if (print && CON_CompleteNameMatches(name, print->partial)) {
        CON_printf("%s", name);
    }
}

static void CON_CompleteReplace(DWORD start, DWORD end, LPCSTR text, BOOL add_space) {
    char completed[CON_INPUT_LEN];

    if (!text || !*text || start > end || end > strlen(con_input)) {
        return;
    }
    snprintf(completed,
             sizeof(completed),
             "%.*s%s%s%s",
             (int)start,
             con_input,
             text,
             add_space ? " " : "",
             con_input + end);
    CON_SetInput(completed);
    con_cursor = start + (DWORD)strlen(text) + (add_space ? 1 : 0);
}

static void CON_CompleteInput(void) {
    char partial[CON_INPUT_LEN];
    char completed[CON_INPUT_LEN];
    DWORD start = 0;
    DWORD end = con_cursor;
    int matches;
    BOOL cvar = false;

    while (con_input[start] && isspace((unsigned char)con_input[start]) && start < end) {
        start++;
    }
    for (DWORD i = start; i < end; i++) {
        if (isspace((unsigned char)con_input[i])) {
            return;
        }
    }
    if (end < start || end - start >= sizeof(partial)) {
        return;
    }
    memcpy(partial, con_input + start, end - start);
    partial[end - start] = '\0';

    matches = Cmd_CompleteCommand(partial, completed, sizeof(completed), false);
    if (!matches) {
        matches = Cvar_CompleteVariable(partial, completed, sizeof(completed), false);
        cvar = true;
    }
    if (!matches) {
        return;
    }
    if (matches > 1) {
        conCompletePrint_t print = { partial };

        if (cvar) {
            Cvar_ForEachVariable(CON_PrintCompleteMatch, &print);
        } else {
            Cmd_ForEachCommand(CON_PrintCompleteMatch, &print);
        }
    }
    CON_CompleteReplace(start, end, completed, matches == 1);
}

void CON_KeyEvent(int key, bool down) {
    SDL_Keymod mod;

    if (cls.key_dest != key_console || !down) {
        return;
    }

    mod = SDL_GetModState();
    if ((mod & (KMOD_LCTRL | KMOD_RCTRL)) && key == SDLK_v && SDL_HasClipboardText()) {
        char *clip = SDL_GetClipboardText();
        if (clip) {
            CON_TextInput(clip);
            SDL_free(clip);
        }
        return;
    }

    switch (key) {
        case SDLK_ESCAPE:
        case SDLK_BACKQUOTE:
            CON_ToggleConsole();
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            CON_Submit();
            break;
        case SDLK_TAB:
            CON_CompleteInput();
            break;
        case SDLK_BACKSPACE:
            if (con_cursor > 0) {
                size_t len = strlen(con_input);
                memmove(con_input + con_cursor - 1,
                        con_input + con_cursor,
                        len - con_cursor + 1);
                con_cursor--;
            }
            break;
        case SDLK_DELETE:
            if (con_input[con_cursor]) {
                size_t len = strlen(con_input);
                memmove(con_input + con_cursor,
                        con_input + con_cursor + 1,
                        len - con_cursor);
            }
            break;
        case SDLK_LEFT:
            if (con_cursor > 0) {
                con_cursor--;
            }
            break;
        case SDLK_RIGHT:
            if (con_input[con_cursor]) {
                con_cursor++;
            }
            break;
        case SDLK_HOME:
            con_cursor = 0;
            break;
        case SDLK_END:
            con_cursor = (DWORD)strlen(con_input);
            break;
        case SDLK_UP:
            CON_History(-1);
            break;
        case SDLK_DOWN:
            CON_History(1);
            break;
        case SDLK_u:
            if (mod & (KMOD_LCTRL | KMOD_RCTRL)) {
                CON_ClearInput();
            }
            break;
        default:
            break;
    }
}

void CON_Init(void) {
    Cmd_AddCommand("toggleconsole", CON_ToggleConsole_f);
    Cmd_AddCommand("clear", CON_Clear_f);
}
