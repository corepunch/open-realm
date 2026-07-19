/* jass.c — Standalone JASS interpreter.
 *
 * Mirrors the structure of Lua's lua.c:
 *   1. Host callbacks (memory, file I/O, text preprocessing)
 *   2. Argument parsing
 *   3. Script execution helpers
 *   4. REPL
 *   5. Entry point (main)
 *
 * Usage:
 *   jass [options] [script.j [args]]
 *   jass -e 'call SomeFunc()'
 *   jass -i                      (interactive REPL)
 *   jass                         (interactive if stdin is a tty)
 *
 * Options:
 *   -e <stmt>   Execute statement string
 *   -l <file>   Load (execute) file before script
 *   -i          Enter interactive mode after executing script
 *   -v          Print version and exit
 *   --          Stop option processing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#ifdef _WIN32
#  include <io.h>
#  define isatty _isatty
#  define fileno _fileno
#else
#  include <unistd.h>
#endif

#ifndef TOOL_COMMON_NO_MPQ
#define TOOL_COMMON_NO_MPQ
#endif
#include "tool_common.h"

#include "jass/jass.h"

/* =========================================================================
 * Version banner
 * ========================================================================= */

#define JASS_VERSION  "JASS 1.0 (openwarcraft3)"
#define JASS_PROMPT   "> "
#define JASS_PROMPT2  ">> "

/* =========================================================================
 * Host interface implementation
 * ========================================================================= */

static HANDLE read_file(LPCSTR filename, DWORD *out_size) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "jass: cannot open '%s': %s\n", filename, strerror(errno));
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    LPSTR buf = Tool_MemAlloc(size);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    fread(buf, 1, size, f);
    fclose(f);
    *out_size = (DWORD)size;
    return buf;
}

static DWORD get_time_ms(void) {
    /* Standalone tool: time is always 0 (no game loop). */
    return 0;
}

static JASSHOST make_host(void) {
    return MAKE(JASSHOST,
        .MemAlloc           = Tool_MemAlloc,
        .MemFree            = Tool_MemFree,
        .GetTime            = get_time_ms,
        .ReadFile = read_file,
        .natives            = NULL,
        .GetPlayerByNumber  = NULL,
    );
}

/* =========================================================================
 * Execution helpers
 * ========================================================================= */

static LPJASS g_jass;   /* global state for signal handler */

static void handle_sigint(int sig) {
    (void)sig;
    /* Best-effort interrupt: just print a message. The interpreter has no
     * async-safe cancellation hook yet. */
    fprintf(stderr, "\n(interrupted)\n");
}

static int do_file(LPJASS j, LPCSTR filename) {
    if (!jass_dofile(j, filename)) {
        fprintf(stderr, "jass: error loading '%s'\n", filename);
        return 1;
    }
    return 0;
}

static int do_string(LPJASS j, LPCSTR src) {
    size_t len = strlen(src);
    LPSTR buf = Tool_MemAlloc((long)len + 1);
    memcpy(buf, src, len + 1);
    BOOL ok = jass_dobuffer(j, buf);
    Tool_MemFree(buf);
    if (!ok) {
        fprintf(stderr, "jass: error in -e string\n");
        return 1;
    }
    return 0;
}

/* =========================================================================
 * REPL — interactive line reader
 * ========================================================================= */

static int do_repl(LPJASS j) {
    char line[4096];
    char block[65536];
    block[0] = '\0';
    int in_block = 0;

    fprintf(stdout, "%s\n", JASS_VERSION);
    fprintf(stdout, "Type JASS statements. Empty line submits a block.\n");
    fflush(stdout);

    for (;;) {
        fprintf(stdout, "%s", in_block ? JASS_PROMPT2 : JASS_PROMPT);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            fprintf(stdout, "\n");
            break;
        }

        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }

        /* Empty line: submit the accumulated block */
        if (len == 0) {
            if (block[0] == '\0') continue;
            do_string(j, block);
            /* Run any coroutines that were started */
            jass_runevents(j);
            block[0] = '\0';
            in_block = 0;
            continue;
        }

        /* Detect block-start keywords */
        if (strncmp(line, "function ", 9) == 0 ||
            strncmp(line, "globals",   7) == 0 ||
            strncmp(line, "native ",   7) == 0 ||
            strncmp(line, "type ",     5) == 0) {
            in_block = 1;
        }

        if (strlen(block) + len + 2 >= sizeof(block)) {
            fprintf(stderr, "jass: input block too large\n");
            block[0] = '\0';
            in_block = 0;
            continue;
        }
        strlcat(block, line, sizeof(block));
        strlcat(block, "\n", sizeof(block));
    }

    return 0;
}

/* =========================================================================
 * Argument processing
 * ========================================================================= */

static void print_usage(LPCSTR prog) {
    fprintf(stderr,
        "Usage: %s [options] [script.j]\n"
        "Options:\n"
        "  -e <stmt>   Execute statement\n"
        "  -l <file>   Load file\n"
        "  -i          Interactive mode\n"
        "  -v          Print version\n"
        "  --          End option processing\n",
        prog);
}

static void print_version(void) {
    fprintf(stdout, "%s\n", JASS_VERSION);
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(int argc, char *argv[]) {
    JASSHOST host = make_host();
    jass_sethost(&host);

    LPJASS j = jass_newstate();
    g_jass = j;

    signal(SIGINT, handle_sigint);

    int interactive = 0;
    int script_done = 0;
    int status = 0;
    LPCSTR prog = argv[0];

    if (argc == 1 && isatty(fileno(stdin))) {
        interactive = 1;
    }

    for (int i = 1; i < argc && status == 0; i++) {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            if (i < argc && !script_done) {
                status = do_file(j, argv[i]);
                script_done = 1;
            }
            break;
        } else if (strcmp(argv[i], "-v") == 0) {
            print_version();
            jass_close(j);
            return 0;
        } else if (strcmp(argv[i], "-i") == 0) {
            interactive = 1;
        } else if (strcmp(argv[i], "-e") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "jass: -e requires an argument\n");
                status = 1;
                break;
            }
            status = do_string(j, argv[i]);
        } else if (strcmp(argv[i], "-l") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "jass: -l requires an argument\n");
                status = 1;
                break;
            }
            status = do_file(j, argv[i]);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "jass: unknown option '%s'\n", argv[i]);
            print_usage(prog);
            status = 1;
        } else if (!script_done) {
            status = do_file(j, argv[i]);
            script_done = 1;
        }
    }

    if (status == 0 && interactive) {
        status = do_repl(j);
    }

    jass_close(j);
    return status;
}
