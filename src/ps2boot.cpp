#ifdef __PS2__
#include <cstdio>
#include <cstring>
#include <debug.h>
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
