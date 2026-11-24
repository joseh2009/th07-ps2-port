#ifdef __PS2__
#include "Ps2Pad.hpp"
#include "Controller.hpp"

#include <cstdio>
#include <libpad.h>
#include <loadfile.h>
#include <sifrpc.h>

void Ps2SifInitOnce(); // ps2boot.cpp

// The pad DMA buffer must be 64-byte aligned and 256 bytes long.
static char s_PadBuf[256] __attribute__((aligned(64)));
static bool s_Ready = false;

#define STICK_DEADZONE 48

// Diagnostic shown next to the fps counter: the buttons GetButtons() reports this frame, in hex (0 = none).
// The console log is not visible on hardware, so this is the only way to see it there.
u32 g_Ps2PadHeld = 0;

bool Ps2Pad::Init()
{
    Ps2SifInitOnce();
    // Newer ROMs have the X modules (libpadx), which the emulator also uses. Older ROMs, such as early SCPH-9000x
    // consoles, only have SIO2MAN and PADMAN, so fall back to those when the X pair is missing.
    // The old pair alone is not used under emulation because PADMAN can hang padInit there.
    bool modulesOk = false;
    if (SifLoadModule("rom0:XSIO2MAN", 0, NULL) >= 0 && SifLoadModule("rom0:XPADMAN", 0, NULL) >= 0)
    {
        modulesOk = true;
    }
    else if (SifLoadModule("rom0:SIO2MAN", 0, NULL) >= 0 && SifLoadModule("rom0:PADMAN", 0, NULL) >= 0)
    {
        printf("Ps2Pad: using SIO2MAN/PADMAN (X modules not in this ROM)\n");
        modulesOk = true;
    }
    if (!modulesOk)
    {
        printf("Ps2Pad: no pad modules could be loaded\n");
        return false;
    }
    padInit(0);
    int r = padPortOpen(0, 0, s_PadBuf);
    printf("Ps2Pad: padPortOpen -> %d\n", r);
    if (r == 0)
        return false;
    s_Ready = true; // the pad settles over the next few frames; GetButtons just polls
    return true;
}

u16 Ps2Pad::GetButtons()
{
    if (!s_Ready)
        return 0;
    int state = padGetState(0, 0);
    if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1)
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
