#pragma once

// Shift-JIS -> UTF-8 for the PS2 build. The PS2 SDL2 port has no SHIFT_JIS converter (SDL_iconv_open fails), so
// SDL_iconv_string() returns NULL and the raw Shift-JIS bytes reach TTF_RenderUTF8 as invalid UTF-8 (boxes on screen).
// Table: JIS X 0208 (Ps2ShiftJisTable.inc). Unmapped sequences become '?'.

#include <cstdlib>
#include <cstring>

#include "inttypes.hpp"
#include "Ps2ShiftJisTable.inc"

static inline char *Ps2Utf8Put(char *o, u32 cp)
{
    if (cp < 0x80)
    {
        *o++ = (char)cp;
    }
    else if (cp < 0x800)
    {
        *o++ = (char)(0xC0 | (cp >> 6));
        *o++ = (char)(0x80 | (cp & 0x3F));
    }
    else
    {
        *o++ = (char)(0xE0 | (cp >> 12));
        *o++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *o++ = (char)(0x80 | (cp & 0x3F));
    }
    return o;
}

// Returns a malloc'd UTF-8 string (release with free()), or NULL on allocation failure.
static inline char *Ps2SjisToUtf8(const char *src)
{
    const size_t len = strlen(src);
    char *out = (char *)malloc(len * 3 + 1);
    if (!out)
    {
        return NULL;
    }
    char *o = out;
    const u8 *p = (const u8 *)src;
    while (*p)
    {
        const u8 b = *p;
        if (b < 0x80)
        {
            *o++ = (char)b;
            p++;
        }
        else if (b >= 0xA1 && b <= 0xDF)
        {
            // half-width katakana, U+FF61..U+FF9F
            o = Ps2Utf8Put(o, 0xFF61u + (u32)(b - 0xA1));
            p++;
        }
        else if (((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC)) && p[1] >= 0x40 &&
                 p[1] <= 0xFC && p[1] != 0x7F)
        {
            const u32 li = (b <= 0x9F) ? (u32)(b - 0x81) : (u32)(b - 0xE0 + 31);
            const u16 cp = kPs2SjisDoubleByte[li][p[1] - 0x40];
            o = Ps2Utf8Put(o, cp ? cp : (u32)'?');
            p += 2;
        }
        else
        {
            *o++ = '?';
            p++;
        }
    }
    *o = '\0';
    return out;
}
