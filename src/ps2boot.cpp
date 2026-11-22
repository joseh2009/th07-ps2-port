#ifdef __PS2__
#include <cstdio>
#include <cstring>
#include <debug.h>
#include <sys/stat.h>
#include <unistd.h>

// Boot screen: runs before anything else (even before the GS backend), using ps2sdk's built-in text console, so
// that something is always visible on screen and the user can see which game files were found.

static long FileSize(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0)
        return (long)st.st_size;
    FILE *f = fopen(path, "rb"); // some file systems (host:) have no working stat()
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

void Ps2BootScreen(int argc, char **argv)
{
    init_scr();
    scr_printf("\n  Touhou 7 PS2 port (experimental)\n");
    scr_printf("  ---------------------------------\n\n");
    scr_printf("  main() reached - screen output works.\n");

    const char *self = (argc > 0 && argv && argv[0]) ? argv[0] : "(none)";
    scr_printf("  ELF path: %s\n", self);

    // Make relative file names resolve next to the ELF (host:dir/th07.elf, mass:/dir/th07.elf, cdrom0:...)
    char dir[256];
    dir[0] = 0;
    if (argc > 0 && argv && argv[0])
    {
        strncpy(dir, argv[0], sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = 0;
        char *slash = strrchr(dir, '/');
        char *colon = strrchr(dir, ':');
        if (slash)
            slash[1] = 0;
        else if (colon)
            colon[1] = 0;
        else
            dir[0] = 0;
    }
    if (dir[0])
    {
        int r = chdir(dir);
        scr_printf("  chdir(%s) = %d\n", dir, r);
    }
    char cwd[256];
    if (getcwd(cwd, sizeof(cwd)))
        scr_printf("  working dir: %s\n", cwd);
    scr_printf("\n");

    static const struct
    {
        const char *name;
        bool required;
    } files[] = {{"th07.dat", true}, {"thbgm.dat", true}, {"msgothic.ttc", true}, {"th07.cfg", false}};

    bool missing = false;
    for (const auto &f : files)
    {
        long n = FileSize(f.name);
        if (n >= 0)
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
    scr_printf("  (if the screen goes black after this, check the EE log)\n");
    sleep(4);
}
#endif
