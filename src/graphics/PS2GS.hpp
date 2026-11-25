#pragma once

// Native PlayStation 2 Graphics Synthesizer backend (gsKit) for the ZunGraphics interface.
//
// Vertex transform / clipping runs on the EE (CPU); the GS rasterizes the resulting screen-space triangles
// with perspective-correct texturing (STQ), alpha blending, alpha test and a 16-bit Z-buffer.

#include "ZunGraphics.hpp"
#include <vector>

#include <gsKit.h>

class PS2GS : public ZunGraphics
{
  public:
    static ZunGraphics *Init();
    ~PS2GS() override
    {
        Exit();
    }

    void Exit() override;
    RendererType GetType() override
    {
        return RENDERER_OPENGLES; // closest existing value; nothing in the game depends on it
    }

    void SetFogRange(f32 nearPlane, f32 farPlane) override;
    void SetFogColor(ZunColor color) override;
    void SetColorOp(TextureOpComponent component, ColorOp op) override;
    void SetTextureFactor(ZunColor factor) override;
    void SetTextureArg(TextureArg arg) override;
    void SetTransformMatrix(TransformMatrix type, const ZunMatrix &matrix) override;
    void SetTextureFilter() override;
    void GetViewport(ZunViewport &viewport) override;
    void SetViewport(const ZunViewport &viewport) override;
    void Enable(Capabilities cap) override;
    void Disable(Capabilities cap) override;
    void SetBlendMode(BlendMode srcMode, BlendMode dstMode) override;
    void SetDepthMask(bool enable) override;
    void SetDepthFunc(DepthFunc func) override;
    void SetClearDepth(f32 depth) override;
    void SetClearColor(ZunColor color) override;
    void SetAlphaTestRef(u8 ref) override;
    void Clear(u32 clearBits) override;
    GfxTextureHandle CreateTexture() override;
    void BindTexture(GfxTextureHandle handle) override;
    void DeleteTexture(GfxTextureHandle handle) override;
    void SetTextureImage(u32 width, u32 height, PixelFormat fmt, PixelDataType type, const void *data) override;
    void SetTextureSubImage(i32 xoffset, i32 yoffset, i32 width, i32 height, const void *data) override;
    void ReadPixels(i32 x, i32 y, i32 width, i32 height, void *pixels) override;
    void DrawPrimitive(PrimitiveType type, i32 startVertex, i32 primitiveCount) override;
    void DrawPrimitiveUP(PrimitiveType type, i32 primitiveCount, const void *vertexData, i32 vertexStride) override;
    void SwapBuffers() override;

  private:
    struct Tex
    {
        GSTEXTURE gs;
        bool alive = false;
        f32 scaleU = 1.0f, scaleV = 1.0f; // image size / (power of two) texture size
    };

    // Vertex after the transform, before the perspective divide.
    struct ClipVert
    {
        f32 x, y, z, w;
        f32 u, v;
        f32 r, g, b, a; // 0..255, converted to the GS range (0..128) when emitted
    };

    // Input vertex in a common layout
    struct InVert
    {
        f32 x, y, z;
        f32 u, v;
        u8 r, g, b, a;
    };

    GSGLOBAL *gs = nullptr;

    std::vector<Tex> textures; // index 0 is a reserved dummy so that handle 0 means "no texture"
    std::vector<u32> freeTextures;
    Tex *boundTexture = nullptr;

    // Fixed function state
    ZunViewport viewport = {0, 0, 640, 480, 0.0f, 1.0f};
    f32 clearDepth = 1.0f;
    u8 clearRGB[3] = {0, 0, 0};
    ZunColor fogColor = {0};
    f32 fogNear = 0.0f, fogFar = 1.0f;
    ZunColor textureFactor = {0xFFFFFFFF};
    TextureArg texArg = TEX_ARG_DIFFUSE;
    ColorOp colorOpRgb = COLOR_OP_MODULATE;
    ColorOp colorOpAlpha = COLOR_OP_MODULATE;
    BlendMode blendSrc = BLEND_ALPHA, blendDst = BLEND_ALPHA;
    DepthFunc depthFunc = DEPTH_FUNC_LEQUAL;
    bool depthMask = true;
    bool depthTest = false;
    bool blendEnabled = false;
    bool alphaTest = false;
    u8 alphaRef = 0;

    ZunMatrix model, view, projection, textureMatrix;
    ZunMatrix mvp;
    bool mvpDirty = true;
    bool textureMatrixIsIdentity = true;

    f32 scaleX = 1.0f, scaleY = 1.0f; // game space (640x480) -> GS framebuffer

    // Scratch buffers reused every draw call
    std::vector<ClipVert> clipVerts;
    std::vector<GSPRIMSTQPOINT> texBatch;
    std::vector<GSPRIMUVPOINT> uvBatch;   // screen-space sprites: no Q, no perspective divide (see DrawVertices)
    std::vector<GSPRIMPOINT> flatBatch;
    bool batchTextured = false; // kind of the batch currently being filled (textured or flat)
    bool batchUV = false;       // the textured batch is in uvBatch (UV form) instead of texBatch (STQ form)
    Tex *batchTex = nullptr;    // texture the textured batch was built for
    bool drawScreen = false;    // the draw being emitted is screen space: w is 1, so the divide is skipped
    std::vector<InVert> upVerts; // DrawPrimitiveUP input, reused (a per-call heap vector fragmented the 32 MB heap)

    void ApplyRenderState();
    void PushRegister(u64 data, u64 address);
    void FlushBatch(bool textured);
    void FlushPending(); // submit the deferred texture/flat batches before any state or memory change
    void EmitTriangle(const ClipVert &a, const ClipVert &b, const ClipVert &c, bool textured);
    void ClipAndEmit(const ClipVert &a, const ClipVert &b, const ClipVert &c, bool textured);
    ClipVert MakeVertex(const InVert &in, bool textured, bool screenSpace) const;
    void DrawVertices(PrimitiveType type, const InVert *verts, i32 count, bool hasTex, bool screenSpace);
    void ConvertAndStore(Tex *tex, i32 x, i32 y, i32 w, i32 h, PixelFormat fmt, PixelDataType type, const void *data);
    void ReleaseTextureMemory(Tex *tex);
};
