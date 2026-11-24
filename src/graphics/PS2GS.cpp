#include "PS2GS.hpp"

#include "AnmManager.hpp"
#include "GameErrorContext.hpp"
#include "GameWindow.hpp"
#include "Supervisor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dmaKit.h>
#include <gsInline.h>
#include <gsToolkit.h>
#include <kernel.h>
#include <malloc.h>

// Framebuffer depth. 16 bit (with dithering) leaves ~1.2 MB more VRAM for textures than 32 bit.
// Build with -DTH07_PS2_CT32 to get a 32 bit framebuffer instead.
#ifdef TH07_PS2_CT32
#define TH07_FB_PSM GS_PSM_CT32
#else
#define TH07_FB_PSM GS_PSM_CT16
#endif

// Everything is uploaded as RGBA32 (alpha scaled to the GS range 0..128)
#define TH07_TEX_PSM GS_PSM_CT32

size_t g_Ps2TexBytes = 0; // EE RAM held by GS texture copies (read by Ps2HeapLog)
unsigned g_Ps2TexCount = 0;
bool g_Ps2TexAllocFailed = false; // set when a texture could not be allocated; AnmManager turns it into a load error
static bool g_Ps2NextTexture16 = false; // one-shot: the next SetTextureImage stores A1B5G5R5 (CT16), half the RAM

void Ps2SetNextTexture16(bool on)
{
    g_Ps2NextTexture16 = on;
}

namespace
{
constexpr int kMaxBatchVerts = 3 * 256;
constexpr f32 kNearW = 0.001f;   // w below this is treated as behind the camera
constexpr f32 kGuardBand = 4.0f; // clip |x|,|y| to 4*w so coordinates stay inside the GS primitive range
constexpr int kMinTexSize = 8;   // the GS upload path needs at least 8x8 texels


// 0..255 (D3D) -> 0..128 (GS, where 128 == 1.0)
inline u8 ToGS(f32 v255)
{
    int v = (int)(v255 * 0.5f + 0.5f);
    return (u8)(v < 0 ? 0 : (v > 128 ? 128 : v));
}

inline u32 PackGS(u32 r, u32 g, u32 b, u32 a8)
{
    u32 a = (a8 + 1) >> 1; // 0..255 -> 0..128
    return r | (g << 8) | (b << 16) | (a << 24);
}

// Expand a `bits` wide channel to 8 bits, replicating the high bits so the maximum maps to 255
inline u32 Expand(u32 v, u32 bits)
{
    switch (bits)
    {
    case 1:
        return v ? 255 : 0;
    case 4:
        return (v << 4) | v;
    case 5:
        return (v << 3) | (v >> 2);
    default: // 6
        return (v << 2) | (v >> 4);
    }
}

inline bool IsIdentity(const ZunMatrix &m)
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            if (m.m[i][j] != (i == j ? 1.0f : 0.0f))
                return false;
    return true;
}

// Signed distance to each of the 5 clip planes (>= 0 means inside)
inline f32 PlaneDist(int plane, f32 x, f32 y, f32 w)
{
    switch (plane)
    {
    case 0:
        return w - kNearW;
    case 1:
        return kGuardBand * w - x;
    case 2:
        return kGuardBand * w + x;
    case 3:
        return kGuardBand * w - y;
    default:
        return kGuardBand * w + y;
    }
}

inline u32 OutCode(f32 x, f32 y, f32 w)
{
    u32 code = 0;
    for (int p = 0; p < 5; p++)
        if (PlaneDist(p, x, y, w) < 0.0f)
            code |= 1u << p;
    return code;
}
} // namespace

// ------------------------------------------------------------------------------------------------
// Init / Exit
// ------------------------------------------------------------------------------------------------

ZunGraphics *PS2GS::Init()
{
    PS2GS *self = new PS2GS;

    self->model.Identity();
    self->view.Identity();
    self->projection.Identity();
    self->textureMatrix.Identity();
    self->mvp.Identity();
    self->textures.emplace_back(); // reserve handle 0

    GSGLOBAL *gs = gsKit_init_global_custom(512 * 1024, 128 * 1024);
    if (gs == nullptr)
    {
        delete self;
        return nullptr;
    }
    self->gs = gs;

    gs->Mode = gsKit_check_rom();
    gs->Interlace = GS_INTERLACED;
    gs->Field = GS_FIELD;
    gs->Width = 640;
    gs->Height = (gs->Mode == GS_MODE_PAL) ? 512 : 448;
    gs->PSM = TH07_FB_PSM;
    gs->PSMZ = GS_PSMZ_16S;
    gs->ZBuffering = GS_SETTING_ON;
    gs->DoubleBuffering = GS_SETTING_ON;
    gs->PrimAlphaEnable = GS_SETTING_ON;
    gs->Dithering = GS_SETTING_ON;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsKit_set_clamp(gs, GS_CMODE_REPEAT);
    gsKit_vram_clear(gs);
    gsKit_init_screen(gs);
    gsKit_TexManager_init(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);

    self->scaleX = (f32)gs->Width / GAME_WINDOW_WIDTH;
    self->scaleY = (f32)gs->Height / GAME_WINDOW_HEIGHT;

    // Boot diagnostic: show dark blue for ~1.5 s. If the screen turns blue and then black, the GS backend works and
    // the problem is further up (e.g. missing data files, see the EE console log).
    // Build with -DTH07_PS2_NO_BOOT_FLASH to disable.
#ifndef TH07_PS2_NO_BOOT_FLASH
    const u64 bootColor = GS_SETREG_RGBAQ(0, 0, 0x50, 0x80, 0);
#else
    const u64 bootColor = GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0);
#endif
    for (int i = 0; i < 2; i++)
    {
        gsKit_clear(gs, bootColor);
        gsKit_queue_exec(gs);
        gsKit_finish();
        gsKit_sync_flip(gs);
    }
#ifndef TH07_PS2_NO_BOOT_FLASH
    for (int i = 0; i < 90; i++)
        gsKit_vsync_wait();
#endif

    self->clipVerts.reserve(1024);
    self->texBatch.reserve(kMaxBatchVerts);
    self->flatBatch.reserve(kMaxBatchVerts);

    printf("PS2 GS backend: %dx%d %s, %s framebuffer + 16 bit Z\n", (int)gs->Width, (int)gs->Height,
           gs->Mode == GS_MODE_PAL ? "PAL" : "NTSC", TH07_FB_PSM == GS_PSM_CT32 ? "32 bit" : "16 bit");
    return self;
}

void PS2GS::Exit()
{
    FlushPending();
    for (Tex &t : textures)
        ReleaseTextureMemory(&t);
    textures.clear();
    if (gs)
    {
        gsKit_vram_clear(gs);
        gsKit_deinit_global(gs);
        gs = nullptr;
    }
}

// ------------------------------------------------------------------------------------------------
// Simple state setters
// ------------------------------------------------------------------------------------------------

void PS2GS::SetFogRange(f32 nearPlane, f32 farPlane)
{
    fogNear = nearPlane;
    fogFar = farPlane;
}

void PS2GS::SetFogColor(ZunColor color)
{
    fogColor = color; // fog is not implemented on the GS backend yet (see README-PS2.md)
}

void PS2GS::SetColorOp(TextureOpComponent component, ColorOp op)
{
    FlushPending();
    if (component == COMPONENT_RGB)
        colorOpRgb = op;
    else
        colorOpAlpha = op;
}

void PS2GS::SetTextureFactor(ZunColor factor)
{
    FlushPending();
    textureFactor = factor;
}

void PS2GS::SetTextureArg(TextureArg arg)
{
    FlushPending();
    texArg = arg;
}

void PS2GS::SetTransformMatrix(TransformMatrix type, const ZunMatrix &matrix)
{
    switch (type)
    {
    case MATRIX_MODEL:
        model = matrix;
        mvpDirty = true;
        break;
    case MATRIX_VIEW:
        view = matrix;
        mvpDirty = true;
        break;
    case MATRIX_PROJECTION:
        projection = matrix;
        mvpDirty = true;
        break;
    case MATRIX_TEXTURE:
        textureMatrix = matrix;
        textureMatrixIsIdentity = IsIdentity(matrix);
        break;
    }
}

void PS2GS::SetTextureFilter()
{
    // bilinear filtering with repeat wrapping is always used
}

void PS2GS::GetViewport(ZunViewport &out)
{
    out = viewport;
}

void PS2GS::SetViewport(const ZunViewport &vp)
{
    /* Batched triangles derive their scissor from the viewport at flush time,
       so flush them under the viewport they were submitted with. */
    FlushPending();
    viewport = vp;
}

void PS2GS::Enable(Capabilities cap)
{
    FlushPending();
    switch (cap)
    {
    case CAPS_BLEND:
        blendEnabled = true;
        break;
    case CAPS_DEPTH_TEST:
        depthTest = true;
        break;
    case CAPS_ALPHA_TEST:
        alphaTest = true;
        break;
    case CAPS_FOG:
        break;
    }
}

void PS2GS::Disable(Capabilities cap)
{
    FlushPending();
    switch (cap)
    {
    case CAPS_BLEND:
        blendEnabled = false;
        break;
    case CAPS_DEPTH_TEST:
        depthTest = false;
        break;
    case CAPS_ALPHA_TEST:
        alphaTest = false;
        break;
    case CAPS_FOG:
        break;
    }
}

void PS2GS::SetBlendMode(BlendMode srcMode, BlendMode dstMode)
{
    FlushPending();
    blendSrc = srcMode;
    blendDst = dstMode;
}

void PS2GS::SetDepthMask(bool enable)
{
    FlushPending();
    depthMask = enable;
}

void PS2GS::SetDepthFunc(DepthFunc func)
{
    FlushPending();
    depthFunc = func;
}

void PS2GS::SetClearDepth(f32 depth)
{
    clearDepth = depth;
}

void PS2GS::SetClearColor(ZunColor color)
{
    clearRGB[0] = color.bytes.r;
    clearRGB[1] = color.bytes.g;
    clearRGB[2] = color.bytes.b;
}

void PS2GS::SetAlphaTestRef(u8 ref)
{
    FlushPending();
    alphaRef = ref;
}

// ------------------------------------------------------------------------------------------------
// GS register helpers
// ------------------------------------------------------------------------------------------------

void PS2GS::PushRegister(u64 data, u64 address)
{
    u64 *p = (u64 *)gsKit_heap_alloc(gs, 1, 16, GIF_AD);
    *p++ = GIF_TAG_AD(1);
    *p++ = GIF_AD;
    *p++ = data;
    *p++ = address;
}

void PS2GS::ApplyRenderState()
{
    // Never let the draw queue overflow: if it is nearly full, kick what we have so far to the GS.
    {
        GSQUEUE *q = gs->CurQueue;
        if ((u8 *)q->pool_max[q->dbuf] - (u8 *)q->pool_cur < 64 * 1024)
        {
            gsKit_queue_exec(gs);
        }
    }

    // Blending. GS: Cd' = (A - B) * C + D with A/B/D in {Cs, Cd, 0} and C in {As, Ad, FIX}
    bool blend = blendEnabled;
    if (blendSrc == BLEND_ONE && blendDst == BLEND_NONE)
    {
        blend = false; // replace
    }
    else if (blendSrc == BLEND_ONE && blendDst == BLEND_ONE)
    {
        gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 2, 2, 1, 0x80), 0); // Cs + Cd
    }
    else if (blendSrc == BLEND_ALPHA && blendDst == BLEND_ONE)
    {
        gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 2, 0, 1, 0), 0); // Cs * As + Cd
    }
    else if (blendSrc == BLEND_ALPHA && blendDst == BLEND_NONE)
    {
        gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 2, 0, 2, 0), 0); // Cs * As
    }
    else
    {
        gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0); // Cs * As + Cd * (1 - As)
    }
    gs->PrimAlphaEnable = blend ? GS_SETTING_ON : GS_SETTING_OFF;

    // Alpha test (alpha is in the 0..128 GS range) + depth test
    PushRegister(GS_SETREG_TEXA(0, 0, 128), GS_TEXA);

    gs->Test->ATE = alphaTest ? 1 : 0;
    gs->Test->ATST = 5; // GEQUAL
    gs->Test->AREF = (alphaRef + 1) >> 1;
    gs->Test->AFAIL = 0; // KEEP
    gs->Test->ZTE = 1;
    gs->Test->ZTST = (depthTest && depthFunc == DEPTH_FUNC_LEQUAL) ? 2 /*GEQUAL*/ : 1 /*ALWAYS*/;
    gsKit_set_test(gs, 0);

    // The GS Z buffer is inverted (0 = far), written only when depth test is on and the depth mask is set
    const bool zwrite = depthTest && depthMask;
    PushRegister(GS_SETREG_ZBUF(gs->ZBuffer / 8192, gs->PSMZ, zwrite ? 0 : 1), GS_ZBUF_1 + gs->PrimContext);

    // The viewport becomes a scissor rectangle
    int x0 = (int)(viewport.x * scaleX);
    int y0 = (int)(viewport.y * scaleY);
    int x1 = (int)((viewport.x + viewport.width) * scaleX) - 1;
    int y1 = (int)((viewport.y + viewport.height) * scaleY) - 1;
    x0 = std::clamp(x0, 0, (int)gs->Width - 1);
    x1 = std::clamp(x1, x0, (int)gs->Width - 1);
    y0 = std::clamp(y0, 0, (int)gs->Height - 1);
    y1 = std::clamp(y1, y0, (int)gs->Height - 1);
    gsKit_set_scissor(gs, GS_SETREG_SCISSOR(x0, x1, y0, y1));
}

void PS2GS::Clear(u32 clearBits)
{
    FlushPending();
    if (clearBits & CLEAR_COLOR_BUFFER)
    {
        // gsKit_clear() fills colour and writes Z = 0 (the far plane); make sure Z writes are enabled
        gsKit_set_scissor(gs, GS_SCISSOR_RESET);
        PushRegister(GS_SETREG_ZBUF(gs->ZBuffer / 8192, gs->PSMZ, 0), GS_ZBUF_1 + gs->PrimContext);
        gs->Test->ATE = 0;
        gsKit_clear(gs, GS_SETREG_RGBAQ(clearRGB[0] >> 1, clearRGB[1] >> 1, clearRGB[2] >> 1, 0x80, 0));
    }
    else if (clearBits & CLEAR_DEPTH_BUFFER)
    {
        // Depth only: draw a full screen sprite at Z = 0 with every colour channel masked out
        const u64 fbp = gs->ScreenBuffer[gs->ActiveBuffer & 1] / 8192;
        gsKit_set_scissor(gs, GS_SCISSOR_RESET);
        PushRegister(GS_SETREG_FRAME(fbp, gs->Width / 64, gs->PSM, 0xFFFFFFFF), GS_FRAME_1 + gs->PrimContext);
        PushRegister(GS_SETREG_ZBUF(gs->ZBuffer / 8192, gs->PSMZ, 0), GS_ZBUF_1 + gs->PrimContext);
        gs->Test->ATE = 0;
        gs->Test->ZTST = 1; // ALWAYS
        gsKit_set_test(gs, 0);
        gsKit_prim_sprite(gs, 0, 0, gs->Width, gs->Height, 0, GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));
        PushRegister(GS_SETREG_FRAME(fbp, gs->Width / 64, gs->PSM, 0), GS_FRAME_1 + gs->PrimContext);
    }
}

// ------------------------------------------------------------------------------------------------
// Textures
// ------------------------------------------------------------------------------------------------

void PS2GS::ReleaseTextureMemory(Tex *tex)
{
    FlushPending();
    if (tex->alive && gs)
        gsKit_TexManager_free(gs, &tex->gs);
    if (tex->gs.Mem)
    {
        g_Ps2TexBytes -= gsKit_texture_size_ee(tex->gs.Width, tex->gs.Height, tex->gs.PSM);
        g_Ps2TexCount--;
        free(tex->gs.Mem);
        tex->gs.Mem = nullptr;
    }
    tex->alive = false;
}

GfxTextureHandle PS2GS::CreateTexture()
{
    u32 id;
    if (!freeTextures.empty())
    {
        id = freeTextures.back();
        freeTextures.pop_back();
        textures[id] = Tex();
    }
    else
    {
        id = (u32)textures.size();
        textures.emplace_back();
    }
    std::memset(&textures[id].gs, 0, sizeof(GSTEXTURE));
    return {id};
}

void PS2GS::BindTexture(GfxTextureHandle handle)
{
    FlushPending();
    if (handle.id >= textures.size())
        return;
    boundTexture = &textures[handle.id];
}

void PS2GS::DeleteTexture(GfxTextureHandle handle)
{
    FlushPending();
    if (handle.id == 0 || handle.id >= textures.size())
        return;
    Tex *t = &textures[handle.id];
    if (boundTexture == t)
        boundTexture = nullptr;
    ReleaseTextureMemory(t);
    freeTextures.push_back(handle.id);
}

void PS2GS::ConvertAndStore(Tex *tex, i32 x, i32 y, i32 w, i32 h, PixelFormat fmt, PixelDataType type, const void *data)
{
    const i32 stride = (i32)tex->gs.Width;
    const bool ct16 = (tex->gs.PSM == GS_PSM_CT16);
    const u8 *src = (const u8 *)data;

    for (i32 row = 0; row < h; row++)
    {
        u32 *dst32 = tex->gs.Mem + (size_t)(y + row) * stride + x;
        u16 *dst16 = (u16 *)tex->gs.Mem + (size_t)(y + row) * stride + x;
        for (i32 i = 0; i < w; i++)
        {
            u32 r, g, b, a8;
            if (type == PIXEL_UNSIGNED_BYTE)
            {
                if (fmt == PIXEL_RGB)
                {
                    const u8 *s = src + ((size_t)row * w + i) * 3;
                    r = s[0];
                    g = s[1];
                    b = s[2];
                    a8 = 255;
                }
                else
                {
                    const u8 *s = src + ((size_t)row * w + i) * 4;
                    r = s[0];
                    g = s[1];
                    b = s[2];
                    a8 = s[3];
                }
            }
            else
            {
                const u32 p = ((const u16 *)src)[(size_t)row * w + i];
                switch (type)
                {
                case PIXEL_UNSIGNED_SHORT_4_4_4_4:
                    r = Expand(p >> 12, 4);
                    g = Expand((p >> 8) & 15, 4);
                    b = Expand((p >> 4) & 15, 4);
                    a8 = Expand(p & 15, 4);
                    break;
                case PIXEL_UNSIGNED_SHORT_5_5_5_1:
                    r = Expand(p >> 11, 5);
                    g = Expand((p >> 6) & 31, 5);
                    b = Expand((p >> 1) & 31, 5);
                    a8 = Expand(p & 1, 1);
                    break;
                default: // 5_6_5
                    r = Expand(p >> 11, 5);
                    g = Expand((p >> 5) & 63, 6);
                    b = Expand(p & 31, 5);
                    a8 = 255;
                    break;
                }
            }

            if (ct16)
            {
                // A1 B5 G5 R5. Alpha is binary here: anything at or above one half is opaque.
                dst16[i] = (u16)((a8 >= 128 ? 0x8000u : 0u) | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3));
            }
            else
            {
                dst32[i] = PackGS(r, g, b, a8);
            }
        }
    }
}

void PS2GS::SetTextureImage(u32 width, u32 height, PixelFormat fmt, PixelDataType type, const void *data)
{
    FlushPending();
    const u32 psm = g_Ps2NextTexture16 ? GS_PSM_CT16 : TH07_TEX_PSM;
    g_Ps2NextTexture16 = false; // one-shot, even when the call below is ignored

    Tex *tex = boundTexture;
    if (tex == nullptr)
        return;

    ReleaseTextureMemory(tex);

    // The GS wraps/addresses textures in powers of two: pad the allocation and scale the UVs to match
    u32 w = kMinTexSize, h = kMinTexSize;
    while (w < width)
        w <<= 1;
    while (h < height)
        h <<= 1;
    tex->scaleU = (f32)width / (f32)w;
    tex->scaleV = (f32)height / (f32)h;
    const u32 bytes = gsKit_texture_size_ee(w, h, psm);

    std::memset(&tex->gs, 0, sizeof(GSTEXTURE));
    tex->gs.Mem = (u32 *)memalign(128, bytes);
    if (tex->gs.Mem == nullptr)
    {
        g_Ps2TexAllocFailed = true;
        g_GameErrorContext.Fatal("error : out of EE memory for a %ux%u texture (%u bytes)\n", width, height, bytes);
        return;
    }
    std::memset(tex->gs.Mem, 0, bytes);
    g_Ps2TexBytes += bytes;
    g_Ps2TexCount++;
    tex->gs.Width = w;
    tex->gs.Height = h;
    tex->gs.PSM = psm;
    tex->gs.Filter = GS_FILTER_LINEAR;
    tex->alive = true;

    if (data)
        ConvertAndStore(tex, 0, 0, (i32)width, (i32)height, fmt, type, data);

    SyncDCache(tex->gs.Mem, (u8 *)tex->gs.Mem + bytes);
    gsKit_TexManager_invalidate(gs, &tex->gs);
}

void PS2GS::SetTextureSubImage(i32 xoffset, i32 yoffset, i32 width, i32 height, const void *data)
{
    FlushPending();
    Tex *tex = boundTexture;
    if (tex == nullptr || !tex->alive || data == nullptr)
        return;

    ConvertAndStore(tex, xoffset, yoffset, width, height, PIXEL_RGBA, PIXEL_UNSIGNED_BYTE, data);

    SyncDCache(tex->gs.Mem, (u8 *)tex->gs.Mem + gsKit_texture_size_ee(tex->gs.Width, tex->gs.Height, tex->gs.PSM));
    gsKit_TexManager_invalidate(gs, &tex->gs);
}

void PS2GS::ReadPixels(i32 x, i32 y, i32 width, i32 height, void *pixels)
{
    FlushPending();
    // Reading the frame buffer back from the GS is not supported yet: return opaque black.
    u8 *dst = (u8 *)pixels;
    for (i32 i = 0; i < width * height; i++, dst += 4)
    {
        dst[0] = dst[1] = dst[2] = 0;
        dst[3] = 255;
    }
}


// ------------------------------------------------------------------------------------------------
// Drawing
// ------------------------------------------------------------------------------------------------

PS2GS::ClipVert PS2GS::MakeVertex(const InVert &in, bool textured, bool screenSpace) const
{
    ClipVert o;

    if (screenSpace)
    {
        // The vertex position is already in game pixels: map it to clip space of the current viewport
        o.x = (in.x - (f32)viewport.x) / (f32)viewport.width * 2.0f - 1.0f;
        o.y = 1.0f - (in.y - (f32)viewport.y) / (f32)viewport.height * 2.0f;
        o.z = in.z;
        o.w = 1.0f;
        o.u = in.u;
        o.v = in.v;
    }
    else
    {
        // D3D convention: row vector * matrix
        const f32 p[4] = {in.x, in.y, in.z, 1.0f};
        f32 c[4];
        for (int j = 0; j < 4; j++)
            c[j] = p[0] * mvp.m[0][j] + p[1] * mvp.m[1][j] + p[2] * mvp.m[2][j] + p[3] * mvp.m[3][j];
        o.x = c[0];
        o.y = c[1];
        o.z = c[2];
        o.w = c[3];

        if (textureMatrixIsIdentity)
        {
            o.u = in.u;
            o.v = in.v;
        }
        else
        {
            const ZunMatrix &t = textureMatrix;
            o.u = in.u * t.m[0][0] + in.v * t.m[1][0] + t.m[2][0];
            o.v = in.u * t.m[0][1] + in.v * t.m[1][1] + t.m[2][1];
        }
    }

    if (textured && boundTexture)
    {
        o.u *= boundTexture->scaleU;
        o.v *= boundTexture->scaleV;
    }

    // Texture stage "argument 2": diffuse colour, texture factor, or nothing
    f32 arg[4];
    switch (texArg)
    {
    case TEX_ARG_TEXTURE:
        arg[0] = arg[1] = arg[2] = arg[3] = 255.0f;
        break;
    case TEX_ARG_TFACTOR:
        arg[0] = textureFactor.bytes.r;
        arg[1] = textureFactor.bytes.g;
        arg[2] = textureFactor.bytes.b;
        arg[3] = textureFactor.bytes.a;
        break;
    default:
        arg[0] = in.r;
        arg[1] = in.g;
        arg[2] = in.b;
        arg[3] = in.a;
        break;
    }

    if (textured)
    {
        // The GS multiplies texel * vertex colour. REPLACE/ADD are approximated by "texel only".
        const bool rgbTex = (colorOpRgb == COLOR_OP_REPLACE || colorOpRgb == COLOR_OP_ADD);
        const bool aTex = (colorOpAlpha == COLOR_OP_REPLACE || colorOpAlpha == COLOR_OP_ADD);
        o.r = rgbTex ? 255.0f : arg[0];
        o.g = rgbTex ? 255.0f : arg[1];
        o.b = rgbTex ? 255.0f : arg[2];
        o.a = aTex ? 255.0f : arg[3];
    }
    else
    {
        o.r = arg[0];
        o.g = arg[1];
        o.b = arg[2];
        o.a = arg[3];
    }
    return o;
}

void PS2GS::EmitTriangle(const ClipVert &a, const ClipVert &b, const ClipVert &c, bool textured)
{
    const ClipVert *tri[3] = {&a, &b, &c};

    for (int i = 0; i < 3; i++)
    {
        const ClipVert &v = *tri[i];
        const f32 invW = 1.0f / v.w;
        const f32 ndcX = v.x * invW;
        const f32 ndcY = v.y * invW;
        const f32 ndcZ = v.z * invW;

        const f32 sx = (((ndcX + 1.0f) * 0.5f * (f32)viewport.width) + (f32)viewport.x) * scaleX - 0.5f;
        const f32 sy = (((1.0f - (ndcY + 1.0f) * 0.5f) * (f32)viewport.height) + (f32)viewport.y) * scaleY - 0.5f;

        // PerspectiveFovLH (ZunMath.hpp) and the 2D projections already produce ndcZ in [0,1] (D3D convention)
        f32 depth = ndcZ * (viewport.maxZ - viewport.minZ) + viewport.minZ;
        depth = std::clamp(depth, 0.0f, 1.0f);
        const int iz = (int)((1.0f - depth) * 65535.0f); // inverted: the GS test is GEQUAL

        const u8 r = ToGS(v.r), g = ToGS(v.g), bl = ToGS(v.b), al = ToGS(v.a);

        if (textured)
        {
            GSPRIMSTQPOINT p;
            p.rgbaq = color_to_RGBAQ(r, g, bl, al, invW);
            p.stq.st.s = v.u * invW;
            p.stq.st.t = v.v * invW;
            p.stq.tag = GS_ST;
            p.xyz2 = vertex_to_XYZ2(gs, sx, sy, iz);
            texBatch.push_back(p);
        }
        else
        {
            GSPRIMPOINT p;
            p.rgbaq = color_to_RGBAQ(r, g, bl, al, 0.0f);
            p.xyz2 = vertex_to_XYZ2(gs, sx, sy, iz);
            flatBatch.push_back(p);
        }
    }

    if ((textured ? texBatch.size() : flatBatch.size()) >= (size_t)kMaxBatchVerts)
        FlushBatch(textured);
}

void PS2GS::ClipAndEmit(const ClipVert &a, const ClipVert &b, const ClipVert &c, bool textured)
{
    const u32 oa = OutCode(a.x, a.y, a.w), ob = OutCode(b.x, b.y, b.w), oc = OutCode(c.x, c.y, c.w);

    if ((oa & ob & oc) != 0)
        return; // entirely outside one plane
    if ((oa | ob | oc) == 0)
    {
        EmitTriangle(a, b, c, textured);
        return;
    }

    // Sutherland-Hodgman against the near plane and the 4 guard band planes
    ClipVert bufA[12], bufB[12];
    ClipVert *in = bufA, *out = bufB;
    int inCount = 3;
    in[0] = a;
    in[1] = b;
    in[2] = c;

    for (int plane = 0; plane < 5 && inCount >= 3; plane++)
    {
        int outCount = 0;
        for (int i = 0; i < inCount; i++)
        {
            const ClipVert &cur = in[i];
            const ClipVert &nxt = in[(i + 1) % inCount];
            const f32 dc = PlaneDist(plane, cur.x, cur.y, cur.w);
            const f32 dn = PlaneDist(plane, nxt.x, nxt.y, nxt.w);

            if (dc >= 0.0f)
                out[outCount++] = cur;
            if ((dc >= 0.0f) != (dn >= 0.0f))
            {
                const f32 t = dc / (dc - dn);
                ClipVert &o = out[outCount++];
                o.x = cur.x + (nxt.x - cur.x) * t;
                o.y = cur.y + (nxt.y - cur.y) * t;
                o.z = cur.z + (nxt.z - cur.z) * t;
                o.w = cur.w + (nxt.w - cur.w) * t;
                o.u = cur.u + (nxt.u - cur.u) * t;
                o.v = cur.v + (nxt.v - cur.v) * t;
                o.r = cur.r + (nxt.r - cur.r) * t;
                o.g = cur.g + (nxt.g - cur.g) * t;
                o.b = cur.b + (nxt.b - cur.b) * t;
                o.a = cur.a + (nxt.a - cur.a) * t;
            }
        }
        std::swap(in, out);
        inCount = outCount;
    }

    for (int i = 1; i + 1 < inCount; i++)
        EmitTriangle(in[0], in[i], in[i + 1], textured);
}

void PS2GS::FlushBatch(bool textured)
{
    const size_t count = textured ? texBatch.size() : flatBatch.size();
    if (count == 0)
        return;

    ApplyRenderState();

    if (textured)
    {
        boundTexture->gs.Filter = GS_FILTER_LINEAR;
        gsKit_TexManager_bind(gs, &boundTexture->gs);
        gsKit_prim_list_triangle_goraud_texture_stq_3d(gs, &boundTexture->gs, (int)count, texBatch.data());
        texBatch.clear();
    }
    else
    {
        gsKit_prim_list_triangle_gouraud_3d(gs, (int)count, flatBatch.data());
        flatBatch.clear();
    }
}

void PS2GS::DrawVertices(PrimitiveType type, const InVert *verts, i32 count, bool hasTex, bool screenSpace)
{
    if (count < 3)
        return;

    const bool textured = hasTex && boundTexture && boundTexture->alive;
    if (hasTex && !textured)
        return; // nothing sensible to draw without a texture

    if (mvpDirty)
    {
        mvp = model * view * projection; // row vector convention: v * M * V * P
        mvpDirty = false;
    }

    // Consecutive draws with the same kind and texture share one batch (one ApplyRenderState and one bind for
    // all of them). Order is kept because triangles are appended in submission order.
    if ((!texBatch.empty() || !flatBatch.empty()) &&
        (textured != batchTextured || (textured && boundTexture != batchTex)))
        FlushPending();
    batchTextured = textured;
    batchTex = textured ? boundTexture : nullptr;

    clipVerts.resize(count);
    for (i32 i = 0; i < count; i++)
        clipVerts[i] = MakeVertex(verts[i], textured, screenSpace);

    if (type == PRIM_TRIANGLE_STRIP)
    {
        for (i32 i = 0; i + 2 < count; i++)
            ClipAndEmit(clipVerts[i], clipVerts[i + 1], clipVerts[i + 2], textured);
    }
    else if (type == PRIM_TRIANGLE_FAN)
    {
        for (i32 i = 1; i + 1 < count; i++)
            ClipAndEmit(clipVerts[0], clipVerts[i], clipVerts[i + 1], textured);
    }
    else
    {
        for (i32 i = 0; i + 2 < count; i += 3)
            ClipAndEmit(clipVerts[i], clipVerts[i + 1], clipVerts[i + 2], textured);
    }
}

void PS2GS::FlushPending()
{
    if (!texBatch.empty())
        FlushBatch(true);
    if (!flatBatch.empty())
        FlushBatch(false);
}

static i32 VertexCount(PrimitiveType type, i32 primitiveCount)
{
    return type == PRIM_TRIANGLES ? primitiveCount * 3 : primitiveCount + 2;
}

void PS2GS::DrawPrimitive(PrimitiveType type, i32 startVertex, i32 primitiveCount)
{
    // The only indexed draw the game uses is the 256x256 unit quad centred on the origin
    static const InVert quad[4] = {{-128.0f, -128.0f, 0.0f, 0.0f, 0.0f, 255, 255, 255, 255},
                                   {128.0f, -128.0f, 0.0f, 1.0f, 0.0f, 255, 255, 255, 255},
                                   {-128.0f, 128.0f, 0.0f, 0.0f, 1.0f, 255, 255, 255, 255},
                                   {128.0f, 128.0f, 0.0f, 1.0f, 1.0f, 255, 255, 255, 255}};
    const i32 count = VertexCount(type, primitiveCount);
    if (startVertex < 0 || startVertex + count > 4)
        return;
    DrawVertices(type, quad + startVertex, count, true, false);
}

void PS2GS::DrawPrimitiveUP(PrimitiveType type, i32 primitiveCount, const void *vertexData, i32 vertexStride)
{
    const i32 count = VertexCount(type, primitiveCount);
    if (count < 3 || vertexData == nullptr)
        return;

    bool screenSpace = false;
    bool hasTex = false;
    switch (vertexStride)
    {
    case sizeof(VertexTex1DiffuseXyzrhw):
        screenSpace = true;
        hasTex = true;
        break;
    case sizeof(VertexTex1DiffuseXyz):
        hasTex = true;
        break;
    case sizeof(VertexDiffuseXyzrhw):
        screenSpace = true;
        break;
    default:
        return;
    }

    upVerts.resize(count);
    InVert *in = upVerts.data();
    const u8 *base = (const u8 *)vertexData;
    for (i32 i = 0; i < count; i++)
    {
        InVert &o = in[i];
        const u8 *p = base + (size_t)i * vertexStride;
        if (vertexStride == (i32)sizeof(VertexTex1DiffuseXyzrhw))
        {
            const VertexTex1DiffuseXyzrhw *v = (const VertexTex1DiffuseXyzrhw *)p;
            o = {v->pos.x, v->pos.y, v->pos.z, v->textureUV.x, v->textureUV.y,
                 v->diffuse.bytes.r, v->diffuse.bytes.g, v->diffuse.bytes.b, v->diffuse.bytes.a};
        }
        else if (vertexStride == (i32)sizeof(VertexTex1DiffuseXyz))
        {
            const VertexTex1DiffuseXyz *v = (const VertexTex1DiffuseXyz *)p;
            o = {v->pos.x, v->pos.y, v->pos.z, v->textureUV.x, v->textureUV.y,
                 v->diffuse.bytes.r, v->diffuse.bytes.g, v->diffuse.bytes.b, v->diffuse.bytes.a};
        }
        else
        {
            const VertexDiffuseXyzrhw *v = (const VertexDiffuseXyzrhw *)p;
            o = {v->pos.x, v->pos.y, v->pos.z, 0.0f, 0.0f,
                 v->diffuse.bytes.r, v->diffuse.bytes.g, v->diffuse.bytes.b, v->diffuse.bytes.a};
        }
    }
    DrawVertices(type, in, count, hasTex, screenSpace);
}

void PS2GS::SwapBuffers()
{
    FlushPending();
    static int frame = 0;
#ifdef TH07_PS2_VERBOSE
    if (frame < 5)
        printf("PS2GS: SwapBuffers frame %d (queue used: %d bytes)\n", frame,
               (int)((u8 *)gs->CurQueue->pool_cur - (u8 *)gs->CurQueue->pool[gs->CurQueue->dbuf]));
#endif
    frame++;

    gsKit_queue_exec(gs);
    gsKit_finish();
    gsKit_sync_flip(gs);
    gsKit_TexManager_nextFrame(gs);
}
