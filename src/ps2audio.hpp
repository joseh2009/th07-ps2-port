#pragma once

// Tiny subset of the miniaudio API, implemented on top of SDL2 audio, used by SoundPlayer on the PlayStation 2
// (miniaudio itself needs dlfcn/threads that ps2sdk does not provide). Only what SoundPlayer.cpp uses exists here.

#include <cstddef>
#include <cstdint>

typedef int ma_result;
typedef uint32_t ma_uint32;
typedef uint64_t ma_uint64;
typedef uint8_t ma_channel;

#define MA_SUCCESS 0
#define MA_ERROR (-1)
#define MA_INVALID_ARGS (-2)
#define MA_AT_END (-17)
#define MA_TRUE 1

enum ma_format
{
    ma_format_unknown = 0,
    ma_format_s16 = 2,
    ma_format_f32 = 5,
};

enum ma_standard_channel_map
{
    ma_standard_channel_map_default = 0
};

typedef void ma_data_source;

struct ma_data_source_vtable
{
    ma_result (*onRead)(ma_data_source *, void *, ma_uint64, ma_uint64 *);
    ma_result (*onSeek)(ma_data_source *, ma_uint64);
    ma_result (*onGetDataFormat)(ma_data_source *, ma_format *, ma_uint32 *, ma_uint32 *, ma_channel *, size_t);
    ma_result (*onGetCursor)(ma_data_source *, ma_uint64 *);
    ma_result (*onGetLength)(ma_data_source *, ma_uint64 *);
    ma_result (*onSetLooping)(ma_data_source *, ma_uint32);
    ma_uint32 flags;
};

struct ma_data_source_base
{
    const ma_data_source_vtable *vtable;
};

struct ma_data_source_config
{
    const ma_data_source_vtable *vtable;
};

inline ma_data_source_config ma_data_source_config_init()
{
    ma_data_source_config c = {nullptr};
    return c;
}
inline ma_result ma_data_source_init(const ma_data_source_config *cfg, ma_data_source_base *base)
{
    base->vtable = cfg->vtable;
    return MA_SUCCESS;
}
inline void ma_data_source_uninit(ma_data_source_base *)
{
}
inline ma_uint32 ma_get_bytes_per_frame(ma_format f, ma_uint32 channels)
{
    return (f == ma_format_s16 ? 2u : 4u) * channels;
}
inline void ma_channel_map_init_standard(ma_standard_channel_map, ma_channel *map, size_t cap, ma_uint32)
{
    for (size_t i = 0; i < cap; i++)
        map[i] = 0;
}
float ma_volume_db_to_linear(float db);
void ma_free(void *p, const void *allocationCallbacks);

// ---- sample buffer (references the caller's memory, does not copy) ----
struct ma_audio_buffer_config
{
    ma_format format;
    ma_uint32 channels;
    ma_uint64 sizeInFrames; // frames as stored in memory
    const void *pData;
    ma_uint32 rateDiv; // stored data is at 44100/rateDiv Hz and is upsampled while mixing
};
struct ma_audio_buffer
{
    ma_data_source_base base;
    ma_format format;
    ma_uint32 channels;
    ma_uint64 frames; // frames at 44100 Hz (stored frames * rateDiv)
    const void *pData;
    ma_uint64 cursor; // in 44100 Hz frames
    ma_uint32 rateDiv;
};
ma_audio_buffer_config ma_audio_buffer_config_init(ma_format format, ma_uint32 channels, ma_uint64 sizeInFrames,
                                                   const void *pData, const void *allocationCallbacks);
ma_result ma_audio_buffer_init(const ma_audio_buffer_config *cfg, ma_audio_buffer *buf);
inline void ma_audio_buffer_uninit(ma_audio_buffer *)
{
}

// ---- WAV decoding to s16 (mono or stereo) @ 44100; release the result with ma_free ----
struct ma_decoder_config
{
    ma_format format;
    ma_uint32 channels;
    ma_uint32 sampleRate;
};
inline ma_decoder_config ma_decoder_config_init(ma_format f, ma_uint32 c, ma_uint32 sr)
{
    return {f, c, sr};
}
ma_result ma_decode_memory(const void *pData, size_t dataSize, ma_decoder_config *cfg, ma_uint64 *pFrameCount,
                           void **ppFrames);

// ---- engine / sounds ----
struct ma_sound;
struct ma_engine
{
    bool running = false;
};

struct ma_sound
{
    ma_data_source *ds;
    ma_format format;
    ma_uint32 channels;
    ma_uint32 sampleRate;
    bool playing;
    bool atEnd;
    float volume;
    // fade
    bool fading;
    float fadeFrom, fadeTo;
    ma_uint64 fadeStartMs, fadeLenMs;
};

ma_result ps2audio_engine_init(ma_engine *e); // loads LIBSD + audsrv.irx, opens audsrv (44100 Hz, 16 bit, stereo)
void ps2audio_engine_uninit(ma_engine *e);
void ps2audio_pump(); // call once per frame: mixes and queues audio for the SPU2
ma_uint64 ma_engine_get_time_in_milliseconds(ma_engine *e);
ma_result ma_sound_init_from_data_source(ma_engine *e, ma_data_source *ds, ma_uint32 flags, void *group,
                                         ma_sound *sound);
void ma_sound_uninit(ma_sound *s);
ma_result ma_sound_start(ma_sound *s);
ma_result ma_sound_stop(ma_sound *s);
ma_result ma_sound_seek_to_pcm_frame(ma_sound *s, ma_uint64 frame);
void ma_sound_set_volume(ma_sound *s, float v);
void ma_sound_set_fade_start_in_milliseconds(ma_sound *s, float volBeg, float volEnd, ma_uint64 lenMs,
                                             ma_uint64 absoluteGlobalTimeMs);
