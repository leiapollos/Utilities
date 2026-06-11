//
// Created by André Leite on 11/06/2026.
//
// Pure audio mixer kernels shared by the host callback and `./sob test`.
// Everything the audio thread executes per voice lives here: the SPSC
// command ring, voice start, and the copy-accumulate mix over cooked
// 48 kHz interleaved stereo F32 PCM. No OS, no allocation, no logging.
//

#pragma once

#define AUDIO_SAMPLE_RATE 48000u
#define AUDIO_CHANNEL_COUNT 2u
#define AUDIO_VOICE_COUNT 32u
#define AUDIO_COMMAND_RING_CAPACITY 256u

enum AudioCommandKind {
    AudioCommandKind_None = 0u,
    AudioCommandKind_Play = 1u,
    AudioCommandKind_StopBuffer = 2u,
    AudioCommandKind_MasterGain = 3u,
};

struct AudioCommand {
    U32 kind;
    U32 bufferIndex;
    U32 bufferGeneration;
    U32 loop;
    F32 gain;
    U32 pad[3];
};

// Single producer (main thread) / single consumer (audio callback).
// head/tail are free-running; capacity is a power of two. A full ring
// drops the command and counts — the producer never blocks.
struct AudioCommandRing {
    AudioCommand commands[AUDIO_COMMAND_RING_CAPACITY];
    U64 head;
    U64 tail;
    U64 dropped;
};

static B32 audio_ring_push_(AudioCommandRing* ring, const AudioCommand* command) {
    U64 head = ATOMIC_LOAD(&ring->head, MEMORY_ORDER_RELAXED);
    U64 tail = ATOMIC_LOAD(&ring->tail, MEMORY_ORDER_ACQUIRE);
    if ((head - tail) >= (U64)AUDIO_COMMAND_RING_CAPACITY) {
        ATOMIC_FETCH_ADD(&ring->dropped, 1ull, MEMORY_ORDER_RELAXED);
        return 0;
    }
    ring->commands[head & (U64)(AUDIO_COMMAND_RING_CAPACITY - 1u)] = *command;
    ATOMIC_STORE(&ring->head, head + 1ull, MEMORY_ORDER_RELEASE);
    return 1;
}

static B32 audio_ring_pop_(AudioCommandRing* ring, AudioCommand* outCommand) {
    U64 tail = ATOMIC_LOAD(&ring->tail, MEMORY_ORDER_RELAXED);
    U64 head = ATOMIC_LOAD(&ring->head, MEMORY_ORDER_ACQUIRE);
    if (tail == head) {
        return 0;
    }
    *outCommand = ring->commands[tail & (U64)(AUDIO_COMMAND_RING_CAPACITY - 1u)];
    ATOMIC_STORE(&ring->tail, tail + 1ull, MEMORY_ORDER_RELEASE);
    return 1;
}

// Voices are owned by the audio callback exclusively; commands are the only
// way state crosses threads, so none of these fields are atomic.
struct AudioVoice {
    U32 bufferIndex;
    U32 bufferGeneration;
    U32 cursor; // frames into the clip
    F32 gain;
    B32 loop;
    B32 active;
};

static AudioVoice* audio_voice_start_(AudioVoice* voices, U32 voiceCount, const AudioCommand* command) {
    for (U32 voiceIndex = 0u; voiceIndex < voiceCount; ++voiceIndex) {
        AudioVoice* voice = voices + voiceIndex;
        if (voice->active) {
            continue;
        }
        voice->bufferIndex = command->bufferIndex;
        voice->bufferGeneration = command->bufferGeneration;
        voice->cursor = 0u;
        voice->gain = command->gain;
        voice->loop = command->loop ? 1 : 0;
        voice->active = 1;
        return voice;
    }
    return 0;
}

// Accumulates one voice into an interleaved stereo F32 output block and
// advances its cursor. loopEnd == 0 or out-of-range means the whole clip.
// One-shots deactivate exactly at frameCount; loops wrap to loopBegin.
static void audio_voice_mix_(AudioVoice* voice, const F32* samples, U32 frameCount,
                             U32 loopBegin, U32 loopEnd, F32 masterGain,
                             F32* outSamples, U32 outFrames) {
    if (frameCount == 0u) {
        voice->active = 0;
        return;
    }
    if (loopEnd == 0u || loopEnd > frameCount) {
        loopEnd = frameCount;
    }
    if (loopBegin >= loopEnd) {
        loopBegin = 0u;
    }

    F32 gain = voice->gain * masterGain;
    U32 written = 0u;
    while (written < outFrames) {
        U32 clipEnd = voice->loop ? loopEnd : frameCount;
        if (voice->cursor >= clipEnd) {
            if (voice->loop) {
                voice->cursor = loopBegin;
                continue;
            }
            voice->active = 0;
            return;
        }
        U32 run = clipEnd - voice->cursor;
        U32 want = outFrames - written;
        if (run > want) {
            run = want;
        }
        const F32* src = samples + (U64)voice->cursor * AUDIO_CHANNEL_COUNT;
        F32* dst = outSamples + (U64)written * AUDIO_CHANNEL_COUNT;
        for (U32 frame = 0u; frame < run; ++frame) {
            dst[frame * 2u + 0u] += src[frame * 2u + 0u] * gain;
            dst[frame * 2u + 1u] += src[frame * 2u + 1u] * gain;
        }
        voice->cursor += run;
        written += run;
        if (!voice->loop && voice->cursor >= frameCount) {
            voice->active = 0;
            return;
        }
    }
}
