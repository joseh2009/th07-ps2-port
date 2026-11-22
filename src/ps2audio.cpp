#include "ps2audio.hpp"

#include <SDL2/SDL.h>
#include <audsrv.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

void Ps2LogAppend(const char *text); // ps2boot.cpp

namespace
{
constexpr int kRate = 44100;
constexpr int kChunk = 512; // frames mixed per step

bool g_dev = false;
std::vector<ma_sound *> g_sounds;
volatile ma_uint64 g_framesMixed = 0;

// Everything runs on the main thread (audio is mixed in ps2audio_pump() from the main loop), so no locking is needed.

// Log to the EE console and to the on-screen exit log (see ps2boot.cpp)
void AudioLog(const char *fmt, ...)
{
    char tmp[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    Ps2LogAppend(tmp);
    std::printf("%s", tmp);
}

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
// Reads `frames` frames at 44100 Hz. Data stored at 44100/rateDiv Hz is upsampled linearly (no state is needed
// because the position is derived from the cursor).
ma_result BufRead(ma_data_source *ds, void *out, ma_uint64 frames, ma_uint64 *read)
{
    ma_audio_buffer *b = (ma_audio_buffer *)ds;
    ma_uint64 n = std::min<ma_uint64>(frames, b->frames - b->cursor);
    const uint32_t ch = b->channels;
    const uint32_t div = b->rateDiv ? b->rateDiv : 1;
    if (div == 1)
    {
        const uint32_t fs = ma_get_bytes_per_frame(b->format, ch);
        std::memcpy(out, (const uint8_t *)b->pData + b->cursor * fs, n * fs);
    }
    else
    {
        const int16_t *src = (const int16_t *)b->pData; // rateDiv > 1 is only produced for s16 data
        const ma_uint64 stored = b->frames / div;
        int16_t *dst = (int16_t *)out;
        for (ma_uint64 i = 0; i < n; i++)
        {
            const ma_uint64 pos = b->cursor + i;
            const ma_uint64 i0 = pos / div;
            const ma_uint64 i1 = std::min(i0 + 1, stored - 1);
            const int t = (int)(pos % div);
            for (uint32_t c = 0; c < ch; c++)
                dst[i * ch + c] =
                    (int16_t)((src[i0 * ch + c] * (int)(div - t) + src[i1 * ch + c] * t) / (int)div);
        }
    }
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
// Decoded effects are kept as 16 bit (mono when the source is mono) to save RAM. Each decoded buffer is allocated with
// a small header in front of the samples that records its layout, so the format travels with the memory itself and
// there is no global table that could go stale when an address is freed and reused.
constexpr uint32_t kSfxRateDiv = 2;
constexpr uint32_t kPcmMagic = 0x314D4350; // "PCM1"
struct PcmHeader
{
    uint32_t magic;
    uint32_t channels; // 1 or 2, samples are always signed 16 bit
    uint32_t rateDiv; // stored sample rate is 44100 / rateDiv
    uint32_t reserved;
};
static_assert(sizeof(PcmHeader) == 16, "keep the samples 16 byte aligned");

int16_t *AllocPcm(ma_uint64 frames, uint32_t channels, uint32_t rateDiv)
{
    const ma_uint64 bytes = frames * channels * sizeof(int16_t);
    if (frames == 0 || bytes > (64ull << 20))
        return nullptr;
    PcmHeader *h = (PcmHeader *)std::malloc(sizeof(PcmHeader) + (size_t)bytes);
    if (!h)
        return nullptr;
    h->magic = kPcmMagic;
    h->channels = channels;
    h->rateDiv = rateDiv;
    h->reserved = 0;
    return (int16_t *)(h + 1);
}

const PcmHeader *HeaderOf(const void *samples)
{
    const PcmHeader *h = samples ? (const PcmHeader *)samples - 1 : nullptr;
    return (h && h->magic == kPcmMagic) ? h : nullptr;
}
} // namespace

float ma_volume_db_to_linear(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

void ma_free(void *p, const void *)
{
    if (!p)
        return;
    const PcmHeader *h = HeaderOf(p);
    if (h)
    {
        PcmHeader *base = (PcmHeader *)h;
        base->magic = 0; // a stale pointer can no longer be mistaken for a live buffer
        std::free(base);
    }
    else
    {
        std::free(p);
    }
}

ma_audio_buffer_config ma_audio_buffer_config_init(ma_format format, ma_uint32 channels, ma_uint64 sizeInFrames,
                                                   const void *pData, const void *)
{
    const PcmHeader *h = HeaderOf(pData);
    ma_uint32 div = 1;
    if (h)
    {
        format = ma_format_s16;
        channels = h->channels;
        div = h->rateDiv ? h->rateDiv : 1;
    }
    else
    {
        AudioLog("audio: buffer %p was not produced by ma_decode_memory; assuming the caller's format\n", pData);
    }
    return {format, channels, sizeInFrames, pData, div};
}

ma_result ma_audio_buffer_init(const ma_audio_buffer_config *cfg, ma_audio_buffer *buf)
{
    buf->base.vtable = &g_bufVtable;
    buf->format = cfg->format;
    buf->channels = cfg->channels;
    buf->rateDiv = cfg->rateDiv ? cfg->rateDiv : 1;
    buf->frames = cfg->sizeInFrames * buf->rateDiv;
    buf->pData = cfg->pData;
    buf->cursor = 0;
    return MA_SUCCESS;
}

static uint32_t Rd32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t Rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

// RIFF/WAVE parser: integer PCM 8/16/24/32 bit and 32 bit float, 1..8 channels (only the first two are used), any
// sample rate (linear resampling to 44100 Hz). The result is signed 16 bit, mono if the source was mono, else stereo.
// Every failure is logged with its cause and reported to the caller as MA_ERROR; nothing is replaced by silence.
ma_result ma_decode_memory(const void *pData, size_t dataSize, ma_decoder_config *, ma_uint64 *pFrameCount,
                           void **ppFrames)
{
    *pFrameCount = 0;
    *ppFrames = nullptr;
    const uint8_t *d = (const uint8_t *)pData;

    if (!d || dataSize < 12 || std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0)
    {
        AudioLog("audio: not a RIFF/WAVE file (%u bytes)\n", (unsigned)dataSize);
        return MA_ERROR;
    }

    uint32_t tag = 0, channels = 0, bits = 0, rate = 0;
    bool haveFmt = false;
    const uint8_t *samples = nullptr;
    uint64_t sampleBytes = 0;

    uint64_t pos = 12;
    while (pos + 8 <= dataSize)
    {
        const uint32_t csize = Rd32(d + pos + 4);
        const uint8_t *c = d + pos + 8;
        const uint64_t avail = dataSize - (pos + 8);
        if (std::memcmp(d + pos, "fmt ", 4) == 0)
        {
            if (csize < 16 || avail < 16)
            {
                AudioLog("audio: fmt chunk too small (%u bytes)\n", csize);
                return MA_ERROR;
            }
            tag = Rd16(c);
            channels = Rd16(c + 2);
            rate = Rd32(c + 4);
            bits = Rd16(c + 14);
            if (tag == 0xFFFE) // WAVE_FORMAT_EXTENSIBLE: the real format tag is the first 2 bytes of the sub format
            {
                if (csize < 26 || avail < 26)
                {
                    AudioLog("audio: extensible fmt chunk too small (%u bytes)\n", csize);
                    return MA_ERROR;
                }
                tag = Rd16(c + 24);
            }
            haveFmt = true;
        }
        else if (std::memcmp(d + pos, "data", 4) == 0)
        {
            samples = c;
            if (csize > avail)
                AudioLog("audio: data chunk truncated (header says %u bytes, %u present); using what is there\n",
                         csize, (unsigned)avail);
            sampleBytes = std::min<uint64_t>(csize, avail);
            break;
        }
        pos += 8 + (uint64_t)csize + (csize & 1); // chunks are padded to an even size
    }

    if (!haveFmt || !samples)
    {
        AudioLog("audio: wav without %s chunk\n", !haveFmt ? "fmt" : "data");
        return MA_ERROR;
    }
    const bool intPcm = tag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool floatPcm = tag == 3 && bits == 32;
    if (!intPcm && !floatPcm)
    {
        AudioLog("audio: unsupported wav encoding (format tag %u, %u bit; ADPCM and friends are not supported)\n", tag,
                 bits);
        return MA_ERROR;
    }
    if (channels < 1 || channels > 8 || rate < 1 || rate > 192000)
    {
        AudioLog("audio: invalid wav header (%u channels, %u Hz)\n", channels, rate);
        return MA_ERROR;
    }

    const uint64_t bytesPerSample = bits / 8;
    const uint64_t frameBytes = bytesPerSample * channels;
    const uint64_t srcFrames = sampleBytes / frameBytes; // a trailing partial frame is dropped
    if (srcFrames == 0)
    {
        AudioLog("audio: wav contains no samples\n");
        return MA_ERROR;
    }
    // Effects are kept at half rate (22050 Hz) to fit the 32 MB of RAM; BufRead upsamples them while mixing.
    const uint32_t div = kSfxRateDiv;
    const uint32_t storeRate = kRate / div;
    const uint64_t outFrames = srcFrames * storeRate / rate;
    const uint32_t outCh = channels > 1 ? 2 : 1;
    int16_t *out = outFrames ? AllocPcm(outFrames, outCh, div) : nullptr;
    if (!out)
    {
        AudioLog("audio: cannot allocate %u frames x %u ch (out of memory or too large)\n", (unsigned)outFrames,
                 outCh);
        return MA_ERROR;
    }

    auto sampleAt = [&](uint64_t frame, uint32_t ch) -> float {
        const uint8_t *p = samples + (frame * channels + ch) * bytesPerSample;
        if (floatPcm)
        {
            float f;
            std::memcpy(&f, p, 4);
            return f;
        }
        switch (bits)
        {
        case 8:
            return ((int)p[0] - 128) / 128.0f;
        case 16:
            return (int16_t)Rd16(p) / 32768.0f;
        case 24:
            return (int32_t)((p[0] << 8) | (p[1] << 16) | ((uint32_t)p[2] << 24)) / 2147483648.0f;
        default:
            return (int32_t)Rd32(p) / 2147483648.0f;
        }
    };
    auto toS16 = [](float v) { return (int16_t)(std::clamp(v, -1.0f, 1.0f) * 32767.0f); };

    for (uint64_t i = 0; i < outFrames; i++)
    {
        const double srcPos = (double)i * rate / storeRate;
        const uint64_t i0 = (uint64_t)srcPos;
        const uint64_t i1 = std::min(i0 + 1, srcFrames - 1);
        const float t = (float)(srcPos - (double)i0);
        for (uint32_t ch = 0; ch < outCh; ch++)
            out[i * outCh + ch] = toS16(sampleAt(i0, ch) * (1 - t) + sampleAt(i1, ch) * t);
    }
    *ppFrames = out;
    *pFrameCount = outFrames;
    return MA_SUCCESS;
}

extern "C" unsigned char audsrv_irx[];
extern "C" unsigned int size_audsrv_irx;

ma_result ps2audio_engine_init(ma_engine *e)
{
    SifInitRpc(0);
    int r = SifLoadModule("rom0:LIBSD", 0, NULL);
    if (r < 0)
        AudioLog("audio: LIBSD load returned %d (continuing: it may already be resident)\n", r);
    int ret = 0;
    r = SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &ret);
    if (r < 0 || ret == 1) // ret 1 = module reported "no resident end"; ID<0 = load failure
    {
        AudioLog("audio: audsrv.irx load failed (id %d, ret %d)\n", r, ret);
        return MA_ERROR;
    }
    if (audsrv_init() != AUDSRV_ERR_NOERROR)
    {
        AudioLog("audio: audsrv_init failed: %d\n", audsrv_get_error());
        return MA_ERROR;
    }
    audsrv_fmt_t fmt;
    fmt.freq = kRate;
    fmt.bits = 16;
    fmt.channels = 2;
    if (audsrv_set_format(&fmt) != 0)
    {
        AudioLog("audio: audsrv_set_format failed: %d\n", audsrv_get_error());
        audsrv_quit();
        return MA_ERROR;
    }
    audsrv_set_volume(MAX_VOLUME);
    g_dev = true;
    e->running = true;
    AudioLog("audio: audsrv ready (%d Hz, 16 bit, stereo)\n", kRate);
    return MA_SUCCESS;
}

void ps2audio_pump()
{
    if (!g_dev)
        return;
    static int16_t out[kChunk * 2];
    // Called once per rendered frame from GameWindow::Render. Top up the audsrv queue with at most 6 chunks so a slow
    // frame cannot stall in the mixer; if the queue is full nothing is mixed at all.
    for (int i = 0; i < 6; i++)
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
    g_sounds.push_back(s);
    return MA_SUCCESS;
}

void ma_sound_uninit(ma_sound *s)
{
    g_sounds.erase(std::remove(g_sounds.begin(), g_sounds.end(), s), g_sounds.end());
}

ma_result ma_sound_start(ma_sound *s)
{
    s->playing = true;
    s->atEnd = false;
    return MA_SUCCESS;
}

ma_result ma_sound_stop(ma_sound *s)
{
    if (!s)
        return MA_INVALID_ARGS;
    s->playing = false;
    return MA_SUCCESS;
}

ma_result ma_sound_seek_to_pcm_frame(ma_sound *s, ma_uint64 frame)
{
    const ma_data_source_vtable *vt = ((ma_data_source_base *)s->ds)->vtable;
    return vt->onSeek ? vt->onSeek(s->ds, frame) : MA_ERROR;
}

void ma_sound_set_volume(ma_sound *s, float v)
{
    s->volume = v;
}

void ma_sound_set_fade_start_in_milliseconds(ma_sound *s, float volBeg, float volEnd, ma_uint64 lenMs,
                                             ma_uint64 absoluteGlobalTimeMs)
{
    s->fading = true;
    s->fadeFrom = volBeg < 0 ? 1.0f : volBeg; // -1 = current volume (the per-sound volume is applied separately)
    s->fadeTo = volEnd;
    s->fadeStartMs = absoluteGlobalTimeMs;
    s->fadeLenMs = lenMs;
}
