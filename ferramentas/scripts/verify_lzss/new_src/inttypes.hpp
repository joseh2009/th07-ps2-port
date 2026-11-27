#pragma once

#include <cstdint>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
#ifdef __PS2__
// On the EE, newlib's uint32_t is `unsigned long`, which conflicts with the ps2sdk's own u32 (`unsigned int`).
// Reuse the ps2sdk definitions so that both sets of headers can be included together.
#include <tamtypes.h>
#else
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#endif
typedef float f32;
typedef double f64;
