#ifdef __PS2__
#include <cstdio>
#include <cstring>
#include <debug.h>
#include <malloc.h>
#include <sifrpc.h>
#include <unistd.h>

// Where the game files live. PS2 file systems (host:, mass:, cdrom0:) have no usable "current directory" for plain
// relative names, so every relative fopen() of the game is redirected through this prefix (see __wrap_fopen).
char g_Ps2BasePrefix[160] = "";

extern "C" FILE *__real_fopen(const char *path, const char *mode);

extern "C" FILE *__wrap_fopen(const char *path, const char *mode)
{
    if (!path)
        return nullptr;
    if (strchr(path, ':') || path[0] == '/' || path[0] == '\\' || g_Ps2BasePrefix[0] == 0)
        return __real_fopen(path, mode);

    const char *rel = path;
    while (rel[0] == '.' && rel[1] == '/')
        rel += 2;
    char full[320];
    snprintf(full, sizeof(full), "%s%s", g_Ps2BasePrefix, rel);
    FILE *f = __real_fopen(full, mode);
    if (!f)
        f = __real_fopen(path, mode);
    return f;
}

// Last few KB of everything the game logged, so the reason for an early exit can be shown on screen
static char s_LogTail[3072];
static size_t s_LogLen = 0;

void Ps2LogAppend(const char *text)
{
    size_t n = strlen(text);
    if (n >= sizeof(s_LogTail))
    {
        text += n - (sizeof(s_LogTail) - 1);
        n = sizeof(s_LogTail) - 1;
    }
    if (s_LogLen + n >= sizeof(s_LogTail))
    {
        size_t drop = s_LogLen + n - (sizeof(s_LogTail) - 1);
        memmove(s_LogTail, s_LogTail + drop, s_LogLen - drop);
        s_LogLen -= drop;
    }
    memcpy(s_LogTail + s_LogLen, text, n);
    s_LogLen += n;
    s_LogTail[s_LogLen] = 0;
}

void Ps2ShowExitScreen(int result)
{
    init_scr();
    scr_printf("\n  Touhou 7 PS2 - the game exited (result %d)\n", result);
    struct mallinfo mi = mallinfo();
    scr_printf("  Heap: %d KB in use, %d KB free inside heap\n", (int)(mi.uordblks / 1024), (int)(mi.fordblks / 1024));
    scr_printf("  Last messages (Japanese text is not shown):\n\n");
    // Keep only plain ASCII lines, last 24 of them
    static char lines[40][100];
    int count = 0;
    char cur[100];
    size_t ci = 0;
    for (size_t i = 0; i <= s_LogLen; i++)
    {
        char ch = s_LogTail[i];
        if (ch == '\n' || ch == 0)
        {
            cur[ci] = 0;
            if (ci > 0)
            {
                const size_t len = ci < 99 ? ci : 99;
                memcpy(lines[count % 40], cur, len);
                lines[count % 40][len] = 0;
                count++;
            }
            ci = 0;
        }
        else if (ch >= 32 && ch < 127 && ci < 96)
            cur[ci++] = ch;
        else if (ch < 0 && ci < 96)
            cur[ci++] = '?';
    }
    int start = count > 24 ? count - 24 : 0;
    for (int i = start; i < count; i++)
        scr_printf("  %s\n", lines[i % 40]);
    scr_printf("\n  Halted. Close the emulator window.\n");
    for (;;)
        sleep(1);
}

static bool CanOpen(const char *prefix, const char *name, long *size)
{
    char full[320];
    snprintf(full, sizeof(full), "%s%s", prefix, name);
    FILE *f = __real_fopen(full, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fclose(f);
    return true;
}

void Ps2BootScreen(int argc, char **argv)
{
    init_scr();
    scr_printf("\n  Touhou 7 PS2 port (experimental)\n");
    scr_printf("  ---------------------------------\n\n");
    scr_printf("  main() reached - screen output works.\n");

    const char *self = (argc > 0 && argv && argv[0]) ? argv[0] : "(none)";
    scr_printf("  ELF path: %s\n\n", self);

    // Candidate folders, first the one the ELF was started from
    char fromElf[160] = "";
    if (argc > 0 && argv && argv[0])
    {
        strncpy(fromElf, argv[0], sizeof(fromElf) - 1);
        char *slash = strrchr(fromElf, '/');
        char *bslash = strrchr(fromElf, '\\');
        char *colon = strrchr(fromElf, ':');
        char *cut = slash > bslash ? slash : bslash;
        if (cut)
            cut[1] = 0;
        else if (colon)
            colon[1] = 0;
        else
            fromElf[0] = 0;
    }
    const char *candidates[] = {fromElf, "", "host:", "host:/", "mass:/", "mass0:/", "cdrom0:\\", "mc0:/"};

    const char *chosen = nullptr;
    scr_printf("  Looking for th07.dat:\n");
    for (const char *c : candidates)
    {
        long n;
        bool ok = CanOpen(c, "th07.dat", &n);
        scr_printf("    %-12s %s\n", c[0] ? c : "(cwd)", ok ? "FOUND" : "no");
        if (ok && !chosen)
            chosen = c;
    }
    scr_printf("\n");

    if (chosen)
        strncpy(g_Ps2BasePrefix, chosen, sizeof(g_Ps2BasePrefix) - 1);
    scr_printf("  Using prefix: '%s'\n\n", g_Ps2BasePrefix);

    static const struct
    {
        const char *name;
        bool required;
    } files[] = {{"th07.dat", true}, {"thbgm.dat", true}, {"msgothic.ttc", true}, {"th07.cfg", false}};

    bool missing = false;
    for (const auto &f : files)
    {
        long n;
        if (CanOpen(g_Ps2BasePrefix, f.name, &n))
            scr_printf("  [ OK ]  %-14s %ld bytes\n", f.name, n);
        else if (f.required)
        {
            scr_printf("  [MISS]  %-14s NOT FOUND (required)\n", f.name);
            missing = true;
        }
        else
            scr_printf("  [ -- ]  %-14s not found (created on first run)\n", f.name);
    }
    scr_printf("\n");

    if (missing)
    {
        scr_printf("  Put the missing files in the same folder as the ELF\n");
        scr_printf("  (names must match exactly, lower case). Halted.\n");
        for (;;)
            sleep(1);
    }
    scr_printf("  All required files found. Starting game...\n");
    sleep(4);
}
#endif

extern size_t g_Ps2TexBytes;   // PS2GS.cpp
extern unsigned g_Ps2TexCount; // PS2GS.cpp
extern size_t g_Ps2AudioBytes; // ps2audio.cpp

// Log the EE heap (malloc) state together with the big consumers tracked separately: GS texture copies and decoded
// effects. This is EE RAM only; the 4 MB of GS VRAM is managed by gsKit and is not covered by mallinfo().
void Ps2HeapLog(const char *where)
{
    struct mallinfo mi = mallinfo();
    char tmp[200];
    snprintf(tmp, sizeof(tmp), "heap@%s: used %dK free %dK arena %dK | tex %dK (%u) sfx %dK\n", where,
             (int)(mi.uordblks / 1024), (int)(mi.fordblks / 1024), (int)(mi.arena / 1024),
             (int)(g_Ps2TexBytes / 1024), g_Ps2TexCount, (int)(g_Ps2AudioBytes / 1024));
    Ps2LogAppend(tmp);
    printf("%s", tmp);
}

// SifInitRpc must run once: the pad and the audio driver both call it at start-up
void Ps2SifInitOnce()
{
    static bool done = false;
    if (!done)
    {
        SifInitRpc(0);
        done = true;
    }
}
