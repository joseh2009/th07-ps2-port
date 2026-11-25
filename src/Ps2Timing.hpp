#pragma once

// Diagnostic timers for the PS2 build (shown on the R3 panel in Supervisor.cpp).
// They only measure: they never change what the game does. On other platforms the macros expand to nothing.

#ifdef __PS2__
#include <SDL2/SDL_timer.h>

#include "inttypes.hpp"

enum Ps2TimerSlot
{
    PS2_T_RENDER,       // GameWindow::Render, the whole frame body
    PS2_T_AUDIO,        // ps2audio_pump (audio mixing)
    PS2_T_SNDQUEUE,     // SoundPlayer::ProcessQueues
    PS2_T_ANM_FLUSH,    // AnmManager::Flush (both calls)
    PS2_T_PRESENT,      // Present(), which includes the GS swap
    PS2_T_PAD,          // Ps2Pad::GetButtons (pad read over SIF RPC)
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

// Per-callback table for the chains: which callback function took how much time, summed since the last refresh.
#define PS2_CB_SLOTS 48
struct Ps2CbStat
{
    void *fn;
    u64 ticks;
};
extern Ps2CbStat g_Ps2CalcStats[PS2_CB_SLOTS];
extern Ps2CbStat g_Ps2DrawStats[PS2_CB_SLOTS];

inline void Ps2CbRecord(Ps2CbStat *table, void *fn, u64 ticks)
{
    for (int i = 0; i < PS2_CB_SLOTS; i++)
    {
        if (table[i].fn == fn || table[i].fn == nullptr)
        {
            table[i].fn = fn;
            table[i].ticks += ticks;
            return;
        }
    }
}

typedef u32 (*Ps2ChainFn)(void *);
inline u32 Ps2TimedChainCall(Ps2ChainFn cb, void *arg, Ps2CbStat *table)
{
    const u64 start = SDL_GetPerformanceCounter();
    const u32 result = cb(arg);
    Ps2CbRecord(table, (void *)cb, SDL_GetPerformanceCounter() - start);
    return result;
}
// The chain runners set which table their callbacks are recorded in, then call every callback through CHAIN_CALL.
extern Ps2CbStat *g_Ps2ActiveCbTable;
// Restores the previous table when the runner returns. Chains can run inside other chains, so this must nest.
struct Ps2CbTableScope
{
    Ps2CbStat *saved;
    explicit Ps2CbTableScope(Ps2CbStat *t) : saved(g_Ps2ActiveCbTable) { g_Ps2ActiveCbTable = t; }
    ~Ps2CbTableScope() { g_Ps2ActiveCbTable = saved; }
};
#define PS2_CB_TABLE_SCOPE(t) Ps2CbTableScope ps2CbTableScope_(t)
#define CHAIN_CALL(cb, arg) Ps2TimedChainCall((Ps2ChainFn)(cb), (arg), g_Ps2ActiveCbTable)
#else
#define PS2_TIME(slot)
#define PS2_CB_TABLE_SCOPE(t)
#define CHAIN_CALL(cb, arg) (cb)(arg)
#endif
