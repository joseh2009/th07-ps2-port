#pragma once
#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER 1234
#define SDL_FLOATWORDORDER 1234
#define SDL_SwapLE16(x) (x)
#define SDL_SwapLE32(x) (x)
#define SDL_SwapLE64(x) (x)
#define SDL_SwapBE16(x) __builtin_bswap16(x)
#define SDL_SwapBE32(x) __builtin_bswap32(x)
#define SDL_SwapBE64(x) __builtin_bswap64(x)
#define SDL_Swap16(x) __builtin_bswap16(x)
#define SDL_Swap32(x) __builtin_bswap32(x)
#define SDL_Swap64(x) __builtin_bswap64(x)
