//
// Created by André Leite on 11/06/2026.
//
// Host-owned audio: device output, voice mixer, and the PCM buffer table.
// The whole point of this living in the host is hot reload — module code
// must never run on the audio thread, so the module only owns handles and
// drives playback through these calls (main thread only). Buffer data is
// copied into host memory at create; reload mid-playback is uninterrupted
// by construction.
//

#pragma once

#include "audio_mixer.hpp"

#define AUDIO_MAX_BUFFERS 64u

struct AudioSystem;

// {0,0} is nil: every call accepts it and no-ops.
struct AudioBufferHandle {
    U32 index;
    U32 generation;
};

// samples: interleaved stereo F32 at AUDIO_SAMPLE_RATE, copied at create.
// loopEnd == 0 means the whole clip.
struct AudioBufferDesc {
    U32 frameCount;
    U32 loopBegin;
    U32 loopEnd;
    const F32* samples;
};

struct AudioStats {
    B32 deviceOpen;
    U32 buffersLive;
    U32 voicesActive;
    U64 voicesDropped;   // play commands that found no free voice
    U64 commandsDropped; // ring-full drops
    U64 callbackCount;
    U64 lastCallbackNanos;
    U64 maxCallbackNanos;
    U64 blobsLeaked; // retire list overflow; leaked instead of freed unsafely
};

UTILITIES_SHARED_API AudioSystem* audio_open(Arena* arena);
UTILITIES_SHARED_API void audio_close(AudioSystem* system);
// Frees retired PCM blobs once the callback provably stopped reading them;
// the host calls this once per frame.
UTILITIES_SHARED_API void audio_frame_reclaim(AudioSystem* system);

UTILITIES_SHARED_API AudioBufferHandle audio_buffer_create(AudioSystem* system, const AudioBufferDesc* desc);
UTILITIES_SHARED_API void audio_buffer_destroy(AudioSystem* system, AudioBufferHandle handle);

UTILITIES_SHARED_API void audio_play(AudioSystem* system, AudioBufferHandle handle, F32 gain, B32 loop);
UTILITIES_SHARED_API void audio_stop(AudioSystem* system, AudioBufferHandle handle);
UTILITIES_SHARED_API void audio_set_master_gain(AudioSystem* system, F32 gain);
UTILITIES_SHARED_API AudioStats audio_stats(AudioSystem* system);
