#pragma once

// Diagnostic timers for the PS2 build (shown on the R3 panel in Supervisor.cpp).
// They only measure: they never change what the game does. On other platforms the macro expands to nothing.

#ifdef __PS2__
#include <SDL2/SDL_timer.h>

#include "inttypes.hpp"

enum Ps2TimerSlot
{
    PS2_T_CALC_CHAIN,   // all update callbacks (Chain::RunCalcChain)
    PS2_T_DRAW_CHAIN,   // all draw callbacks (Chain::RunDrawChain)
    PS2_T_BULLET_UPD,   // BulletManager::OnUpdate
    PS2_T_BULLET_DRAW,  // BulletManager::OnDraw
    PS2_T_ENEMY_UPD,    // EnemyManager::OnUpdate
    PS2_T_ENEMY_DRAW,   // EnemyManager::OnDraw1 and OnDraw2
    PS2_T_EFFECT_UPD,   // EffectManager::OnUpdate
    PS2_T_EFFECT_DRAW,  // EffectManager::OnDraw
    PS2_T_COUNT
};

// Summed ticks since the last panel refresh (Supervisor.cpp resets them).
extern u64 g_Ps2TimerTicks[PS2_T_COUNT];

struct Ps2ScopeTimer
{
    Ps2TimerSlot slot;
    u64 start;
    explicit Ps2ScopeTimer(Ps2TimerSlot s) : slot(s), start(SDL_GetPerformanceCounter()) {}
    ~Ps2ScopeTimer() { g_Ps2TimerTicks[slot] += SDL_GetPerformanceCounter() - start; }
};

#define PS2_TIME(slot) Ps2ScopeTimer ps2ScopeTimer_(slot)
#else
#define PS2_TIME(slot)
#endif
