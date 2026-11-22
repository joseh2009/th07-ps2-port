#ifdef __PS2__
#include "Ps2Pad.hpp"
#include "Controller.hpp"

#include <cstdio>
#include <libpad.h>
#include <loadfile.h>
#include <sifrpc.h>

// The pad DMA buffer must be 64-byte aligned and 256 bytes long.
static char s_PadBuf[256] __attribute__((aligned(64)));
static bool s_Ready = false;

#define STICK_DEADZONE 48

bool Ps2Pad::Init()
{
    SifInitRpc(0);
    // The newer X modules go with libpadx; the old rom0:PADMAN can hang padInit under emulation.
    if (SifLoadModule("rom0:XSIO2MAN", 0, NULL) < 0)
    {
        printf("Ps2Pad: failed to load XSIO2MAN\n");
        return false;
    }
    if (SifLoadModule("rom0:XPADMAN", 0, NULL) < 0)
    {
        printf("Ps2Pad: failed to load XPADMAN\n");
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
        return 0;

    struct padButtonStatus pad;
    if (padRead(0, 0, &pad) == 0)
        return 0;

    u16 held = ~pad.btns; // active low
    u16 out = 0;
    if (held & PAD_UP)
        out |= TH_BUTTON_UP;
    if (held & PAD_DOWN)
        out |= TH_BUTTON_DOWN;
    if (held & PAD_LEFT)
        out |= TH_BUTTON_LEFT;
    if (held & PAD_RIGHT)
        out |= TH_BUTTON_RIGHT;
    if (held & PAD_CROSS)
        out |= TH_BUTTON_SHOOT | TH_BUTTON_ENTER;
    if (held & (PAD_CIRCLE | PAD_TRIANGLE))
        out |= TH_BUTTON_BOMB;
    if (held & (PAD_SQUARE | PAD_L1 | PAD_R1 | PAD_L2 | PAD_R2))
        out |= TH_BUTTON_FOCUS;
    if (held & PAD_START)
        out |= TH_BUTTON_MENU;
    if (held & PAD_SELECT)
        out |= TH_BUTTON_SKIP;

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
