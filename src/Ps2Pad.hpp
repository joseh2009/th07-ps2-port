#pragma once
#include "inttypes.hpp"

#ifdef __PS2__
// Native joypad 1 input through libpad. SDL's PS2 joystick backend goes through the multitap RPC, which can hang
// at start-up when the multitap module never answers, so the game reads the pad directly.
namespace Ps2Pad
{
bool Init();
u16 GetButtons(); // TH_BUTTON_* bit mask
} // namespace Ps2Pad
#endif
