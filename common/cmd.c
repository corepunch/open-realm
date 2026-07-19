#include <string.h>
#include <ctype.h>

#include "common.h"

#define MAX_CMD_TOKENS 64
#define MAX_CMD_TOKEN_CHARS 1024

typedef struct cmd_function_s {
    struct cmd_function_s *next;
    LPCSTR name;
    xcommand_t function;
} cmd_function_t;

static sizeBuf_t cmd_text;
static BYTE cmd_text_buf[8192];
static bool cmd_wait;
static cmd_function_t *cmd_functions; // possible commands to execute
static int cmd_argc;
static char cmd_argv[MAX_CMD_TOKENS][MAX_CMD_TOKEN_CHARS];
static char cmd_args[MAX_CMD_TOKEN_CHARS];

static BOOL Cmd_NameMatches(LPCSTR name, LPCSTR partial) {
    size_t len;

    if (!name || !partial) {
        return false;
    }
    len = strlen(partial);
    return !strncasecmp(name, partial, len);
}

static void Cmd_CommonPrefix(LPSTR out, DWORD out_size, LPCSTR name) {
    DWORD i;

    if (!out || out_size == 0 || !name) {
        return;
    }
    if (!out[0]) {
        snprintf(out, out_size, "%s", name);
        return;
    }
    for (i = 0; out[i] && name[i] && i + 1 < out_size; i++) {
        if (tolower((unsigned char)out[i]) != tolower((unsigned char)name[i])) {
            break;
        }
    }
    out[i] = '\0';
}

static void Cmd_List_f(void) {
    DWORD count = 0;

    FOR_EACH_LIST(cmd_function_t, cmd, cmd_functions) {
        fprintf(stderr, "%s\n", cmd->name);
        count++;
    }
    fprintf(stderr, "%u commands\n", count);
}

char *va(char *format, ...) {
    va_list argptr;
    static char string[1024];
    va_start (argptr, format);
    vsnprintf (string, sizeof(string), format,argptr);
    va_end (argptr);
    return string;
}

void Cmd_Wait_f (void) {
    cmd_wait = true;
}

void Cbuf_Init(void) {
    cmd_wait = false;
    SZ_Init (&cmd_text, cmd_text_buf, sizeof(cmd_text_buf));
    Cmd_AddCommand("wait", Cmd_Wait_f);
    Cmd_AddCommand("cmdlist", Cmd_List_f);
}

void Cbuf_AddText(LPCSTR text) {
    DWORD l = (DWORD)strlen(text);
    if (cmd_text.cursize + l >= cmd_text.maxsize) {
        fprintf(stderr, "Cbuf_AddText: overflow\n");
        return;
    }
    SZ_Write(&cmd_text, text, l);
}

static bool Cbuf_IsCommandLineSwitch(LPCSTR arg) {
    return arg && (arg[0] == '+' || arg[0] == '-') && arg[1] != '\0';
}

static void Cbuf_AddQuotedArg(LPCSTR arg) {
    bool quote = false;

    if (!arg) {
        return;
    }
    for (LPCSTR p = arg; *p; p++) {
        if (isspace((unsigned char)*p) || *p == '"' || *p == ';') {
            quote = true;
            break;
        }
    }
    if (!quote) {
        Cbuf_AddText(arg);
        return;
    }
    Cbuf_AddText("\"");
    for (LPCSTR p = arg; *p; p++) {
        if (*p == '"' || *p == '\\') {
            Cbuf_AddText("\\");
        }
        char ch[2] = { *p, '\0' };
        Cbuf_AddText(ch);
    }
    Cbuf_AddText("\"");
}

/*
===============
Cbuf_AddEarlyCommands

Quake-style command-line processing for startup cvars.  +set commands and
+<known cvar> value forms are applied before the client/server modules start.
Other +commands stay in argv for Cbuf_AddLateCommands().
===============
*/
void Cbuf_AddEarlyCommands(bool clear) {
    int argc = COM_Argc();

    for (int i = 1; i < argc; i++) {
        LPCSTR arg = COM_Argv(i);

        if (!strcmp(arg, "+set") && i + 2 < argc) {
            Cvar_Set(COM_Argv(i + 1), COM_Argv(i + 2));
            if (clear) {
                COM_ClearArgv(i);
                COM_ClearArgv(i + 1);
                COM_ClearArgv(i + 2);
            }
            i += 2;
            continue;
        }
        if (arg[0] == '+' && Cvar_String(arg + 1, NULL) != NULL) {
            LPCSTR value = "1";
            int cmd_index = i;

            if (i + 1 < argc && !Cbuf_IsCommandLineSwitch(COM_Argv(i + 1))) {
                value = COM_Argv(i + 1);
                if (clear) {
                    COM_ClearArgv(i + 1);
                }
                i++;
            }
            Cvar_Set(arg + 1, value);
            if (clear) {
                COM_ClearArgv(cmd_index);
            }
        }
    }
}

/*
=================
Cbuf_AddLateCommands

Adds remaining + command-line chunks to the command buffer, stopping each
chunk at the next + or - switch.
=================
*/
bool Cbuf_AddLateCommands(void) {
    int argc = COM_Argc();
    bool added = false;

    for (int i = 1; i < argc; i++) {
        LPCSTR arg = COM_Argv(i);

        if (!arg[0] || arg[0] != '+') {
            continue;
        }
        Cbuf_AddText(arg + 1);
        while (i + 1 < argc && !Cbuf_IsCommandLineSwitch(COM_Argv(i + 1))) {
            i++;
            Cbuf_AddText(" ");
            Cbuf_AddQuotedArg(COM_Argv(i));
        }
        Cbuf_AddText("\n");
        added = true;
    }
    return added;
}

LPCSTR current_command = NULL;

static void Cmd_TokenizeString(LPCSTR text) {
    LPCSTR p = text;

    cmd_argc = 0;
    cmd_args[0] = '\0';
    memset(cmd_argv, 0, sizeof(cmd_argv));
    while (p && *p && cmd_argc < MAX_CMD_TOKENS) {
        LPSTR out;
        size_t len = 0;

        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (p[0] == '/' && p[1] == '/') {
            break;
        }
        if (!*p) {
            break;
        }
        out = cmd_argv[cmd_argc++];
        if (*p == '"') {
            p++;
            while (*p && *p != '"' && len + 1 < MAX_CMD_TOKEN_CHARS) {
                if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) {
                    p++;
                }
                out[len++] = *p++;
            }
            if (*p == '"') {
                p++;
            }
        } else {
            while (*p
                   && !isspace((unsigned char)*p)
                   && !(p[0] == '/' && p[1] == '/')
                   && len + 1 < MAX_CMD_TOKEN_CHARS) {
                out[len++] = *p++;
            }
        }
        out[len] = '\0';
    }
}

int Cmd_Argc(void) {
    return cmd_argc;
}

LPCSTR Cmd_Argv(int arg) {
    if (arg < 0 || arg >= cmd_argc) {
        return "";
    }
    return cmd_argv[arg];
}

LPCSTR Cmd_ArgsFrom(int arg) {
    cmd_args[0] = '\0';
    for (int i = arg; i < cmd_argc; i++) {
        if (i > arg) {
            strncat(cmd_args, " ", sizeof(cmd_args) - strlen(cmd_args) - 1);
        }
        strncat(cmd_args, cmd_argv[i], sizeof(cmd_args) - strlen(cmd_args) - 1);
    }
    return cmd_args;
}

void Cmd_ExecuteString(LPCSTR text) {
    LPCSTR token;

    Cmd_TokenizeString(text);
    if (!cmd_argc)
        return;
    token = Cmd_Argv(0);
    
    current_command = text;

    // check functions
    FOR_EACH_LIST(cmd_function_t, cmd, cmd_functions) {
        if (!strcmp(token, cmd->name)) {
            if (!cmd->function) {    // forward to server command
                Cmd_ExecuteString(va("cmd %s", text));
            } else {
                cmd->function ();
            }
            return;
        }
    }
    if (Cvar_Command()) {
        return;
    }
    
    // send it as a server command if we are connected
    Cmd_ForwardToServer(text);
}

void Cbuf_Execute(void) {
    DWORD i;
    LPSTR text;
    char line[1024];
    DWORD quotes;
    bool comment;

    while (cmd_text.cursize) {
        // find a \n or ; line break
        text = (LPSTR)cmd_text.data;
        text = (LPSTR)cmd_text.data;
        quotes = 0;
        comment = false;
        for (i=0 ; i< cmd_text.cursize ; i++) {
            if (!comment && i + 1 < cmd_text.cursize && text[i] == '/' && text[i + 1] == '/') {
                comment = true;
            }
            if (!comment && text[i] == '"')
                quotes++;
            if (!comment && !(quotes&1) && text[i] == ';')
                break; // don't break if inside a quoted string
            if (text[i] == '\n')
                break;
        }
                
        memcpy(line, text, i);
        line[i] = 0;
        
// delete the text from the command buffer and move remaining commands down
// this is necessary because commands (exec, alias) can insert data at the
// beginning of the text buffer

        if (i == cmd_text.cursize) {
            cmd_text.cursize = 0;
        } else {
            i++;
            cmd_text.cursize -= i;
            memmove(text, text+i, cmd_text.cursize);
        }

// execute the command line
        Cmd_ExecuteString (line);
        
        if (cmd_wait) {
            // skip out while text still remains in buffer, leaving it
            // for next frame
            cmd_wait = false;
            break;
        }
    }
}

bool Cmd_Exists(LPCSTR cmd_name) {
    FOR_EACH_LIST(cmd_function_t, cmd, cmd_functions) {
        if (!strcmp(cmd_name, cmd->name)) {
            return true;
        }
    }
    return false;
}

void Cmd_ForEachCommand(cmdListFunc_t func, void *userData) {
    if (!func) {
        return;
    }
    FOR_EACH_LIST(cmd_function_t, cmd, cmd_functions) {
        func(cmd->name, userData);
    }
}

int Cmd_CompleteCommand(LPCSTR partial, LPSTR out, DWORD out_size, bool print) {
    int matches = 0;
    char common[MAX_CMD_TOKEN_CHARS];

    if (out && out_size > 0) {
        out[0] = '\0';
    }
    common[0] = '\0';
    partial = partial ? partial : "";
    FOR_EACH_LIST(cmd_function_t, cmd, cmd_functions) {
        if (!Cmd_NameMatches(cmd->name, partial)) {
            continue;
        }
        if (print) {
            fprintf(stderr, "%s\n", cmd->name);
        }
        Cmd_CommonPrefix(common, sizeof(common), cmd->name);
        matches++;
    }
    if (out && out_size > 0 && matches > 0) {
        snprintf(out, out_size, "%s", common);
    }
    return matches;
}

void Cmd_AddCommand(LPCSTR cmd_name, xcommand_t function) {
    if (Cmd_Exists(cmd_name)) {
        fprintf(stderr, "Cmd_AddCommand: %s already defined\n", cmd_name);
        return;
    }
    cmd_function_t *cmd = MemAlloc(sizeof(cmd_function_t));
    cmd->name = cmd_name;
    cmd->function = function;
    ADD_TO_LIST(cmd, cmd_functions);
}

void Cmd_RemoveCommand(LPCSTR cmd_name) {
    cmd_function_t **prev = &cmd_functions;

    FOR_EACH_LIST(cmd_function_t, cmd, cmd_functions) {
        if (!strcmp(cmd_name, cmd->name)) {
            *prev = cmd->next;
            MemFree(cmd);
            return;
        }
        prev = &cmd->next;
    }
}
