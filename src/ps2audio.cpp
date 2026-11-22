#include "ps2audio.hpp"

#include <SDL2/SDL.h>
#include <audsrv.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
constexpr int kRate = 44100;
constexpr int kChunk = 512; // frames mixed per step

bool g_dev = false;
std::vector<ma_sound *> g_sounds;
volatile ma_uint64 g_framesMixed = 0;

struct Lock
{
}; // single threaded: audio is mixed from the main loop (ps2audio_pump)

float Gain(ma_sound *s, ma_uint64 nowMs)
{
    float g = s->volume;
    if (s->fading)
    {
        float t = s->fadeLenMs ? (float)(nowMs - s->fadeStartMs) / (float)s->fadeLenMs : 1.0f;
        if (nowMs < s->fadeStartMs)
            t = 0.0f;
        if (t >= 1.0f)
        {
            t = 1.0f;
        }
        g *= s->fadeFrom + (s->fadeTo - s->fadeFrom) * t;
    }
    return g;
}

// Reads up to `frames` frames from the sound as stereo f32 @ kRate. Returns frames produced.
ma_uint64 ReadSound(ma_sound *s, float *out, ma_uint64 frames)
{
    const ma_data_source_vtable *vt = ((ma_data_source_base *)s->ds)->vtable;
    static uint8_t raw[kChunk * 8];
    ma_uint64 got = 0;
    vt->onRead(s->ds, raw, frames, &got);
    for (ma_uint64 i = 0; i < got; i++)
    {
        float l, r;
        if (s->format == ma_format_s16)
        {
            const int16_t *p = (const int16_t *)raw + i * s->channels;
            l = p[0] / 32768.0f;
            r = s->channels > 1 ? p[1] / 32768.0f : l;
        }
        else
        {
            const float *p = (const float *)raw + i * s->channels;
            l = p[0];
            r = s->channels > 1 ? p[1] : l;
        }
        out[i * 2] = l;
        out[i * 2 + 1] = r;
    }
    return got;
}

void MixFrames(int16_t *dst, int frames)
{
    static float mix[kChunk * 2];
    static float tmp[kChunk * 2];

    while (frames > 0)
    {
        int n = std::min(frames, kChunk);
        std::memset(mix, 0, sizeof(float) * n * 2);
        const ma_uint64 nowMs = g_framesMixed * 1000 / kRate;

        for (ma_sound *s : g_sounds)
        {
            if (!s->playing)
                continue;
            ma_uint64 got = ReadSound(s, tmp, n);
            if (got == 0)
            {
                s->playing = false;
                s->atEnd = true;
                continue;
            }
            const float g = Gain(s, nowMs);
            for (ma_uint64 i = 0; i < got * 2; i++)
                mix[i] += tmp[i] * g;
            if (got < (ma_uint64)n)
                s->playing = false;
        }

        for (int i = 0; i < n * 2; i++)
        {
            float v = std::clamp(mix[i], -1.0f, 1.0f);
            dst[i] = (int16_t)(v * 32767.0f);
        }
        dst += n * 2;
        frames -= n;
        g_framesMixed += n;
    }
}

// ---- audio buffer data source ----
ma_result BufRead(ma_data_source *ds, void *out, ma_uint64 frames, ma_uint64 *read)
{
    ma_audio_buffer *b = (ma_audio_buffer *)ds;
    ma_uint64 n = std::min<ma_uint64>(frames, b->frames - b->cursor);
    const uint32_t fs = ma_get_bytes_per_frame(b->format, b->channels);
    std::memcpy(out, (const uint8_t *)b->pData + b->cursor * fs, n * fs);
    b->cursor += n;
    *read = n;
    return n ? MA_SUCCESS : MA_AT_END;
}
ma_result BufSeek(ma_data_source *ds, ma_uint64 f)
{
    ma_audio_buffer *b = (ma_audio_buffer *)ds;
    b->cursor = std::min(f, b->frames);
    return MA_SUCCESS;
}
ma_result BufFormat(ma_data_source *ds, ma_format *f, ma_uint32 *c, ma_uint32 *sr, ma_channel *, size_t)
{
    ma_audio_buffer *b = (ma_audio_buffer *)ds;
    if (f)
        *f = b->format;
    if (c)
        *c = b->channels;
    if (sr)
        *sr = kRate;
    return MA_SUCCESS;
}
ma_result BufCursor(ma_data_source *ds, ma_uint64 *c)
{
    *c = ((ma_audio_buffer *)ds)->cursor;
    return MA_SUCCESS;
}
ma_result BufLength(ma_data_source *ds, ma_uint64 *l)
{
    *l = ((ma_audio_buffer *)ds)->frames;
    return MA_SUCCESS;
}
const ma_data_source_vtable g_bufVtable = {BufRead, BufSeek, BufFormat, BufCursor, BufLength, nullptr, 0};
} // namespace

float ma_volume_db_to_linear(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

void ma_free(void *p, const void *)
{
    std::free(p);
}

ma_audio_buffer_config ma_audio_buffer_config_init(ma_format format, ma_uint32 channels, ma_uint64 sizeInFrames,
                                                   const void *pData, const void *)
{
    return {format, channels, sizeInFrames, pData};
}

ma_result ma_audio_buffer_init(const ma_audio_buffer_config *cfg, ma_audio_buffer *buf)
{
    buf->base.vtable = &g_bufVtable;
    buf->format = cfg->format;
    buf->channels = cfg->channels;
    buf->frames = cfg->sizeInFrames;
    buf->pData = cfg->pData;
    buf->cursor = 0;
    return MA_SUCCESS;
}

ma_result ma_decode_memory(const void *pData, size_t dataSize, ma_decoder_config *cfg, ma_uint64 *pFrameCount,
                           void **ppFrames)
{
    SDL_RWops *rw = SDL_RWFromConstMem(pData, (int)dataSize);
    SDL_AudioSpec spec;
    Uint8 *buf = nullptr;
    Uint32 len = 0;
    if (!rw || !SDL_LoadWAV_RW(rw, 1, &spec, &buf, &len))
        return MA_ERROR;

    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_F32SYS, 2, kRate) < 0)
    {
        SDL_FreeWAV(buf);
        return MA_ERROR;
    }
    cvt.len = (int)len;
    cvt.buf = (Uint8 *)std::malloc((size_t)len * (size_t)cvt.len_mult);
    if (!cvt.buf)
    {
        SDL_FreeWAV(buf);
        return MA_ERROR;
    }
    std::memcpy(cvt.buf, buf, len);
    SDL_FreeWAV(buf);
    if (cvt.needed && SDL_ConvertAudio(&cvt) < 0)
    {
        std::free(cvt.buf);
        return MA_ERROR;
    }
    *ppFrames = cvt.buf;
    *pFrameCount = (ma_uint64)cvt.len_cvt / 8;
    return MA_SUCCESS;
}

extern "C" unsigned char audsrv_irx[];
extern "C" unsigned int size_audsrv_irx;

ma_result ps2audio_engine_init(ma_engine *e)
{
    SifInitRpc(0);
    int r = SifLoadModule("rom0:LIBSD", 0, NULL);
    std::printf("audio: LIBSD -> %d\n", r);
    int ret = 0;
    r = SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &ret);
    std::printf("audio: audsrv.irx -> %d (ret %d)\n", r, ret);
    if (r < 0)
        return MA_ERROR;
    if (audsrv_init() != AUDSRV_ERR_NOERROR)
    {
        std::printf("audio: audsrv_init failed: %d\n", audsrv_get_error());
        return MA_ERROR;
    }
    audsrv_fmt_t fmt;
    fmt.freq = kRate;
    fmt.bits = 16;
    fmt.channels = 2;
    if (audsrv_set_format(&fmt) != 0)
    {
        std::printf("audio: audsrv_set_format failed\n");
        audsrv_quit();
        return MA_ERROR;
    }
    audsrv_set_volume(MAX_VOLUME);
    g_dev = true;
    e->running = true;
    std::printf("audio: audsrv ready\n");
    return MA_SUCCESS;
}

void ps2audio_pump()
{
    if (!g_dev)
        return;
    static int16_t out[kChunk * 2];
    // Feed the SPU2 queue with as much audio as it has room for (bounded so a slow frame cannot stall for long)
    for (int i = 0; i < 8; i++)
    {
        int avail = audsrv_available();
        if (avail < (int)sizeof(out))
            break;
        MixFrames(out, kChunk);
        audsrv_play_audio((const char *)out, sizeof(out));
    }
}

void ps2audio_engine_uninit(ma_engine *e)
{
    if (g_dev)
    {
        audsrv_stop_audio();
        audsrv_quit();
        g_dev = false;
    }
    g_sounds.clear();
    e->running = false;
}

ma_uint64 ma_engine_get_time_in_milliseconds(ma_engine *)
{
    return g_framesMixed * 1000 / kRate;
}

ma_result ma_sound_init_from_data_source(ma_engine *, ma_data_source *ds, ma_uint32, void *, ma_sound *s)
{
    std::memset(s, 0, sizeof(*s));
    const ma_data_source_vtable *vt = ((ma_data_source_base *)ds)->vtable;
    if (vt->onGetDataFormat(ds, &s->format, &s->channels, &s->sampleRate, nullptr, 0) != MA_SUCCESS)
        return MA_ERROR;
    s->ds = ds;
    s->volume = 1.0f;
    Lock l;
    g_sounds.push_back(s);
    return MA_SUCCESS;
}

void ma_sound_uninit(ma_sound *s)
{
    Lock l;
    g_sounds.erase(std::remove(g_sounds.begin(), g_sounds.end(), s), g_sounds.end());
}

ma_result ma_sound_start(ma_sound *s)
{
    Lock l;
    s->playing = true;
    s->atEnd = false;
    return MA_SUCCESS;
}

ma_result ma_sound_stop(ma_sound *s)
{
    if (!s)
        return MA_INVALID_ARGS;
    Lock l;
    s->playing = false;
    return MA_SUCCESS;
}

ma_result ma_sound_seek_to_pcm_frame(ma_sound *s, ma_uint64 frame)
{
    Lock l;
    const ma_data_source_vtable *vt = ((ma_data_source_base *)s->ds)->vtable;
    return vt->onSeek ? vt->onSeek(s->ds, frame) : MA_ERROR;
}

void ma_sound_set_volume(ma_sound *s, float v)
{
    Lock l;
    s->volume = v;
}

void ma_sound_set_fade_start_in_milliseconds(ma_sound *s, float volBeg, float volEnd, ma_uint64 lenMs,
                                             ma_uint64 absoluteGlobalTimeMs)
{
    Lock l;
    s->fading = true;
    s->fadeFrom = volBeg < 0 ? 1.0f : volBeg; // -1 = current volume (the per-sound volume is applied separately)
    s->fadeTo = volEnd;
    s->fadeStartMs = absoluteGlobalTimeMs;
    s->fadeLenMs = lenMs;
}
