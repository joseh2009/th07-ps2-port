#ifdef __PS2__
#include "Ps2Pad.hpp"
#include "Controller.hpp"
#include "Ps2Timing.hpp"

#include <cstdio>
#include <libpad.h>
#include <loadfile.h>
#include <kernel.h>
#include <sifrpc.h>

void Ps2SifInitOnce(); // ps2boot.cpp
extern "C" unsigned char padman_irx[]; // padman_irx.cpp
extern "C" unsigned int size_padman_irx;

// The pad DMA buffer must be 64-byte aligned and 256 bytes long.
static char s_PadBuf[256] __attribute__((aligned(64)));
static bool s_Ready = false;

#define STICK_DEADZONE 48

// Diagnostic shown next to the fps counter: the buttons GetButtons() reports this frame, in hex (0 = none).
// The console log is not visible on hardware, so this is the only way to see it there.
u32 g_Ps2PadHeld = 0;
u32 g_Ps2PadMode = 0; // pad.mode from the last successful read (0x41 digital, 0x73 analog, ...)
// Result of Ps2Pad::Init, shown on the title screen: 0 not tried, 1 no pad modules, 2 port open failed, 3 ready
int g_Ps2PadInit = 0;
// Raw data from the last successful padRead, for the title-screen status line: ok, mode and buttons, and a read counter.
// If the counter keeps going up while the buttons never change, the pad is answering but sends the same data.
u32 g_Ps2PadRaw = 0;
u32 g_Ps2PadReads = 0;

bool Ps2Pad::Init()
{
    Ps2SifInitOnce();
    // Newer ROMs have the X modules (libpadx), which the emulator also uses. Older ROMs, such as early SCPH-9000x
    // consoles, only have SIO2MAN and PADMAN, so fall back to those when the X pair is missing.
    // The old pair alone is not used under emulation because PADMAN can hang padInit there.
    bool modulesOk = false;
    bool embedded = false;
    // First try the open-source padman that ships in the ps2sdk and is embedded in this ELF (padman_irx.cpp). It
    // replaces the ROM driver, which is strict about the SIO2 timing that some pads do not follow.
    if (SifLoadModule("rom0:SIO2MAN", 0, NULL) >= 0)
    {
        int ret = 0;
        int id = SifExecModuleBuffer(padman_irx, size_padman_irx, 0, NULL, &ret);
        if (id >= 0 && ret != 1)
        {
            printf("Ps2Pad: using the embedded padman (ps2sdk)\n");
            modulesOk = true;
            embedded = true;
        }
        else
        {
            printf("Ps2Pad: embedded padman failed (id %d, ret %d)\n", id, ret);
        }
    }
    if (!modulesOk && SifLoadModule("rom0:XSIO2MAN", 0, NULL) >= 0 && SifLoadModule("rom0:XPADMAN", 0, NULL) >= 0)
    {
        modulesOk = true;
    }
    else if (!modulesOk && SifLoadModule("rom0:SIO2MAN", 0, NULL) >= 0 && SifLoadModule("rom0:PADMAN", 0, NULL) >= 0)
    {
        printf("Ps2Pad: using SIO2MAN/PADMAN (X modules not in this ROM)\n");
        modulesOk = true;
    }
    if (!modulesOk)
    {
        printf("Ps2Pad: no pad modules could be loaded\n");
        g_Ps2PadInit = 1;
        return false;
    }
    padInit(0);
    // The IOP writes pad data straight into this buffer by DMA. Read it through the uncached alias, otherwise the EE
    // can keep returning the first (idle) value from its data cache.
    int r = padPortOpen(0, 0, UNCACHED_SEG(s_PadBuf));
    printf("Ps2Pad: padPortOpen -> %d\n", r);
    if (r == 0)
    {
        g_Ps2PadInit = 2;
        return false;
    }
    s_Ready = true; // the pad settles over the next few frames; GetButtons just polls
    g_Ps2PadInit = embedded ? 4 : 3;
    return true;
}

static int s_stuckFrames = 0; // frames in a state other than stable / find

u16 Ps2Pad::GetButtons()
{
    PS2_TIME(PS2_T_PAD);
    if (!s_Ready)
        return 0;
    int state = padGetState(0, 0);
    if (state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1)
    {
        s_stuckFrames = 0;
    }
    else if (++s_stuckFrames == 600)
    {
        // About 10 seconds without a ready pad (for example a clone that never finishes a command): reopen the port once
        padPortClose(0, 0);
        padPortOpen(0, 0, UNCACHED_SEG(s_PadBuf));
    }
    // EXECCMD means the pad is busy with a command (for example after padSetMainMode); the data can still be read
    if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1 && state != PAD_STATE_EXECCMD)
    {
        g_Ps2PadHeld = 0x20000 | (u32)state; // diagnostic: pad not ready yet
        return 0;
    }

    struct padButtonStatus pad;
    if (padRead(0, 0, &pad) == 0)
    {
        g_Ps2PadHeld = 0x10000; // diagnostic: padRead returned no data
        return 0;
    }

    u16 held = ~pad.btns; // active low
    g_Ps2PadHeld = held;
    g_Ps2PadMode = pad.mode;
    g_Ps2PadRaw = ((u32)pad.ok << 24) | ((u32)pad.mode << 16) | pad.btns;
    g_Ps2PadReads++;

    // Some clone pads only send button data after they are switched to analog mode. If a digital pad has reported
    // no buttons for about 3 seconds, ask it once to switch. A genuine DualShock is unaffected: it is already analog
    // or it ignores the request, and buttons are read the same way either way.
    static int s_idleDigitalFrames = 0;
    static bool s_analogTried = false;
    if (pad.mode == 0x41 && held == 0)
    {
        if (!s_analogTried && ++s_idleDigitalFrames > 180)
        {
            padSetMainMode(0, 0, 1, 0);
            s_analogTried = true;
        }
    }
    else
    {
        s_idleDigitalFrames = 0;
    }
    u16 out = 0;
    if (held & PAD_UP)
        out |= TH_BUTTON_UP;
    if (held & PAD_DOWN)
        out |= TH_BUTTON_DOWN;
    if (held & PAD_LEFT)
        out |= TH_BUTTON_LEFT;
    if (held & PAD_RIGHT)
        out |= TH_BUTTON_RIGHT;
    // Layout: Cross = shoot/confirm (Z), Circle = bomb/cancel (X), L1 = focus (Shift), R1 = skip dialogue (Ctrl),
    // Select = pause/back (Esc), Start = Enter. Cancel in menus is MENU|BOMB, so Circle and Select both back out.
    if (held & PAD_CROSS)
        out |= TH_BUTTON_SHOOT;
    if (held & PAD_CIRCLE)
        out |= TH_BUTTON_BOMB;
    if (held & PAD_L1)
        out |= TH_BUTTON_FOCUS;
    if (held & PAD_R1)
        out |= TH_BUTTON_SKIP;
    if (held & PAD_SELECT)
        out |= TH_BUTTON_MENU;
    if (held & PAD_START)
        out |= TH_BUTTON_ENTER;

    if (pad.mode >> 4 == 0x7 || pad.mode >> 4 == 0x5) // analog stick mirrors the d-pad
    {
        if (pad.ljoy_h < 128 - STICK_DEADZONE)
            out |= TH_BUTTON_LEFT;
        if (pad.ljoy_h > 128 + STICK_DEADZONE)
            out |= TH_BUTTON_RIGHT;
        if (pad.ljoy_v < 128 - STICK_DEADZONE)
            out |= TH_BUTTON_UP;
        if (pad.ljoy_v > 128 + STICK_DEADZONE)
            out |= TH_BUTTON_DOWN;
    }
    return out;
}
#endif
