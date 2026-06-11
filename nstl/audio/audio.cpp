//
// Created by André Leite on 11/06/2026.
//

// PCM blobs are immutable after publish. The callback may hold a blob
// pointer for the duration of one render callback, so a destroyed blob
// parks on the retired list and frees only after the callback counter
// advances two past the retire point (callbacks on one device never
// overlap; +2 covers the one that was in flight when we retired).
struct AudioBlob {
    U32 frameCount;
    U32 loopBegin;
    U32 loopEnd;
    U32 pad0;
    U64 reserveSize;
    // F32 samples follow, interleaved stereo
};

static F32* audio_blob_samples_(AudioBlob* blob) {
    return (F32*)(blob + 1);
}

struct AudioBufferSlot {
    AudioBlob* blob;   // atomic: callback reads acquire
    U32 generation;    // atomic: bumped at destroy; 0 = never used
    B32 used;          // main thread only
};

struct AudioRetiredBlob {
    AudioBlob* blob;
    U64 epoch;
};

#define AUDIO_MAX_RETIRED_BLOBS 64u

struct AudioSystem {
    OS_AudioOutput* output; // atomic: published by the open thread
    B32 deviceOpen;         // atomic: flips once the device starts
    Arena* osArena;         // open-thread-owned until join
    OS_Handle openThread;

    AudioCommandRing ring;
    AudioVoice voices[AUDIO_VOICE_COUNT]; // callback-owned
    F32 masterGain;                       // callback-owned after open

    AudioBufferSlot buffers[AUDIO_MAX_BUFFERS];
    U32 buffersLive;

    AudioRetiredBlob retired[AUDIO_MAX_RETIRED_BLOBS];
    U32 retiredCount;

    U64 callbackCount;   // atomic: the reclaim epoch
    U32 voicesActive;    // atomic: callback writes, stats read
    U64 voicesDropped;   // atomic
    U64 blobsLeaked;
    U64 lastCallbackNanos; // atomic
    U64 maxCallbackNanos;  // atomic
};

static void audio_render_(F32* samples, U32 frameCount, void* user) {
    AudioSystem* system = (AudioSystem*)user;
    U64 startNanos = OS_get_time_nanoseconds();
    ATOMIC_FETCH_ADD(&system->callbackCount, 1ull, MEMORY_ORDER_RELEASE);

    MEMSET(samples, 0, (U64)frameCount * AUDIO_CHANNEL_COUNT * sizeof(F32));

    AudioCommand command;
    while (audio_ring_pop_(&system->ring, &command)) {
        switch (command.kind) {
            case AudioCommandKind_Play: {
                AudioBufferSlot* slot = system->buffers + command.bufferIndex;
                U32 generation = ATOMIC_LOAD(&slot->generation, MEMORY_ORDER_ACQUIRE);
                if (generation != command.bufferGeneration) {
                    break;
                }
                if (!audio_voice_start_(system->voices, AUDIO_VOICE_COUNT, &command)) {
                    ATOMIC_FETCH_ADD(&system->voicesDropped, 1ull, MEMORY_ORDER_RELAXED);
                }
            }
            break;

            case AudioCommandKind_StopBuffer: {
                for (U32 voiceIndex = 0u; voiceIndex < AUDIO_VOICE_COUNT; ++voiceIndex) {
                    AudioVoice* voice = system->voices + voiceIndex;
                    if (voice->active &&
                        voice->bufferIndex == command.bufferIndex &&
                        voice->bufferGeneration == command.bufferGeneration) {
                        voice->active = 0;
                    }
                }
            }
            break;

            case AudioCommandKind_MasterGain: {
                system->masterGain = command.gain;
            }
            break;

            default: {
            }
            break;
        }
    }

    U32 active = 0u;
    for (U32 voiceIndex = 0u; voiceIndex < AUDIO_VOICE_COUNT; ++voiceIndex) {
        AudioVoice* voice = system->voices + voiceIndex;
        if (!voice->active) {
            continue;
        }
        AudioBufferSlot* slot = system->buffers + voice->bufferIndex;
        U32 generation = ATOMIC_LOAD(&slot->generation, MEMORY_ORDER_ACQUIRE);
        AudioBlob* blob = ATOMIC_LOAD(&slot->blob, MEMORY_ORDER_ACQUIRE);
        if (generation != voice->bufferGeneration || !blob) {
            voice->active = 0;
            continue;
        }
        audio_voice_mix_(voice, audio_blob_samples_(blob), blob->frameCount,
                         blob->loopBegin, blob->loopEnd, system->masterGain,
                         samples, frameCount);
        if (voice->active) {
            active += 1u;
        }
    }
    ATOMIC_STORE(&system->voicesActive, active, MEMORY_ORDER_RELAXED);

    U64 elapsedNanos = OS_get_time_nanoseconds() - startNanos;
    ATOMIC_STORE(&system->lastCallbackNanos, elapsedNanos, MEMORY_ORDER_RELAXED);
    if (elapsedNanos > ATOMIC_LOAD(&system->maxCallbackNanos, MEMORY_ORDER_RELAXED)) {
        ATOMIC_STORE(&system->maxCallbackNanos, elapsedNanos, MEMORY_ORDER_RELAXED);
    }
}

// Device open runs on its own thread: AudioToolbox's component
// registration dispatches to the main queue, and the host's frame loop —
// not [NSApp run] — is what drains it, so a synchronous open from
// host_init deadlocks before the first frame. The table and ring work
// immediately; deviceOpen flips when the callback is live. No logging on
// the open thread.
static void audio_open_thread_(void* user) {
    AudioSystem* system = (AudioSystem*)user;
    OS_AudioOutput* output = OS_audio_output_open(system->osArena, AUDIO_SAMPLE_RATE,
                                                  AUDIO_CHANNEL_COUNT, audio_render_, system);
    ATOMIC_STORE(&system->output, output, MEMORY_ORDER_RELEASE);
    ATOMIC_STORE(&system->deviceOpen, output != 0 ? 1 : 0, MEMORY_ORDER_RELEASE);
}

AudioSystem* audio_open(Arena* arena) {
    ASSERT_ALWAYS(arena != 0);

    AudioSystem* system = ARENA_PUSH_STRUCT(arena, AudioSystem);
    if (!system) {
        return 0;
    }
    MEMSET(system, 0, sizeof(*system));
    system->masterGain = 1.0f;

    system->osArena = arena_alloc(.arenaSize = KB(64), .committedSize = KB(64));
    if (!system->osArena) {
        LOG_ERROR("audio", "Audio arena alloc failed; audio runs silent");
        return system;
    }
    system->openThread = OS_thread_create(audio_open_thread_, system);
    if (!system->openThread.handle) {
        LOG_ERROR("audio", "Audio open thread failed; audio runs silent");
        return system;
    }
    LOG_INFO("audio", "Output opening ({} Hz, {} ch, {} voices)",
             AUDIO_SAMPLE_RATE, AUDIO_CHANNEL_COUNT, AUDIO_VOICE_COUNT);
    return system;
}

void audio_close(AudioSystem* system) {
    if (!system) {
        return;
    }
    B32 openThreadDone = 1;
    if (system->openThread.handle) {
        if (ATOMIC_LOAD(&system->deviceOpen, MEMORY_ORDER_ACQUIRE)) {
            OS_thread_join(system->openThread);
        } else {
            // A wedged coreaudiod parks the open thread in mach_msg
            // indefinitely (seen live 2026-06-11); joining would hang quit.
            // Detach and let it die with the process; the leaked half-open
            // client is coreaudiod's to reap.
            OS_thread_detach(system->openThread);
            openThreadDone = 0;
        }
        system->openThread.handle = 0;
    }
    OS_AudioOutput* output = ATOMIC_LOAD(&system->output, MEMORY_ORDER_ACQUIRE);
    if (output) {
        OS_audio_output_close(output);
        system->output = 0;
        system->deviceOpen = 0;
    }
    if (system->osArena && openThreadDone) {
        // A detached open thread may still write into this arena; leak it
        // in that case (it is KB(64) on the way out of the process).
        arena_release(system->osArena);
        system->osArena = 0;
    }
    // Callback is gone; everything frees immediately.
    for (U32 retiredIndex = 0u; retiredIndex < system->retiredCount; ++retiredIndex) {
        AudioBlob* blob = system->retired[retiredIndex].blob;
        OS_release(blob, blob->reserveSize);
    }
    system->retiredCount = 0u;
    for (U32 bufferIndex = 0u; bufferIndex < AUDIO_MAX_BUFFERS; ++bufferIndex) {
        AudioBufferSlot* slot = system->buffers + bufferIndex;
        AudioBlob* blob = slot->blob;
        if (blob) {
            ATOMIC_STORE(&slot->blob, (AudioBlob*)0, MEMORY_ORDER_RELEASE);
            OS_release(blob, blob->reserveSize);
        }
        slot->used = 0;
    }
    system->buffersLive = 0u;
}

void audio_frame_reclaim(AudioSystem* system) {
    if (!system || system->retiredCount == 0u) {
        return;
    }
    B32 deviceOpen = ATOMIC_LOAD(&system->deviceOpen, MEMORY_ORDER_ACQUIRE);
    U64 epoch = ATOMIC_LOAD(&system->callbackCount, MEMORY_ORDER_ACQUIRE);
    U32 kept = 0u;
    for (U32 retiredIndex = 0u; retiredIndex < system->retiredCount; ++retiredIndex) {
        AudioRetiredBlob entry = system->retired[retiredIndex];
        if (!deviceOpen || epoch >= entry.epoch + 2ull) {
            OS_release(entry.blob, entry.blob->reserveSize);
        } else {
            system->retired[kept++] = entry;
        }
    }
    system->retiredCount = kept;
}

AudioBufferHandle audio_buffer_create(AudioSystem* system, const AudioBufferDesc* desc) {
    AudioBufferHandle nilHandle = {};
    if (!system || !desc || !desc->samples || desc->frameCount == 0u ||
        (U64)desc->frameCount * AUDIO_CHANNEL_COUNT * sizeof(F32) > MB(64)) {
        return nilHandle;
    }

    AudioBufferSlot* slot = 0;
    U32 slotIndex = 0u;
    for (U32 bufferIndex = 0u; bufferIndex < AUDIO_MAX_BUFFERS; ++bufferIndex) {
        if (!system->buffers[bufferIndex].used) {
            slot = system->buffers + bufferIndex;
            slotIndex = bufferIndex;
            break;
        }
    }
    if (!slot) {
        LOG_ERROR("audio", "Buffer table full ({} slots)", AUDIO_MAX_BUFFERS);
        return nilHandle;
    }

    U64 sampleBytes = (U64)desc->frameCount * AUDIO_CHANNEL_COUNT * sizeof(F32);
    U64 reserveSize = sizeof(AudioBlob) + sampleBytes;
    AudioBlob* blob = (AudioBlob*)OS_reserve(reserveSize);
    if (!blob || !OS_commit(blob, reserveSize)) {
        if (blob) {
            OS_release(blob, reserveSize);
        }
        return nilHandle;
    }
    blob->frameCount = desc->frameCount;
    blob->loopBegin = desc->loopBegin;
    blob->loopEnd = desc->loopEnd;
    blob->pad0 = 0u;
    blob->reserveSize = reserveSize;
    MEMCPY(audio_blob_samples_(blob), desc->samples, sampleBytes);

    U32 generation = ATOMIC_LOAD(&slot->generation, MEMORY_ORDER_RELAXED);
    if (generation == 0u) {
        generation = 1u;
    }
    slot->used = 1;
    ATOMIC_STORE(&slot->blob, blob, MEMORY_ORDER_RELEASE);
    ATOMIC_STORE(&slot->generation, generation, MEMORY_ORDER_RELEASE);
    system->buffersLive += 1u;

    AudioBufferHandle handle = {};
    handle.index = slotIndex;
    handle.generation = generation;
    return handle;
}

void audio_buffer_destroy(AudioSystem* system, AudioBufferHandle handle) {
    if (!system || handle.generation == 0u || handle.index >= AUDIO_MAX_BUFFERS) {
        return;
    }
    AudioBufferSlot* slot = system->buffers + handle.index;
    if (!slot->used || ATOMIC_LOAD(&slot->generation, MEMORY_ORDER_RELAXED) != handle.generation) {
        return;
    }

    // Generation bump first: voices die at the next callback even if the
    // blob pointer read races; the retired blob stays mapped past that.
    ATOMIC_STORE(&slot->generation, handle.generation + 1u, MEMORY_ORDER_RELEASE);
    AudioBlob* blob = slot->blob;
    ATOMIC_STORE(&slot->blob, (AudioBlob*)0, MEMORY_ORDER_RELEASE);
    slot->used = 0;
    system->buffersLive -= 1u;

    if (!blob) {
        return;
    }
    if (system->retiredCount >= AUDIO_MAX_RETIRED_BLOBS) {
        // Freeing now could yank memory from a mid-callback mix; leak it
        // loudly instead.
        system->blobsLeaked += 1u;
        LOG_ERROR("audio", "Retired-blob list full; leaking {} bytes", blob->reserveSize);
        return;
    }
    AudioRetiredBlob entry = {};
    entry.blob = blob;
    entry.epoch = ATOMIC_LOAD(&system->callbackCount, MEMORY_ORDER_ACQUIRE);
    system->retired[system->retiredCount++] = entry;
}

void audio_play(AudioSystem* system, AudioBufferHandle handle, F32 gain, B32 loop) {
    if (!system || handle.generation == 0u || handle.index >= AUDIO_MAX_BUFFERS) {
        return;
    }
    AudioCommand command = {};
    command.kind = AudioCommandKind_Play;
    command.bufferIndex = handle.index;
    command.bufferGeneration = handle.generation;
    command.loop = loop ? 1u : 0u;
    command.gain = gain;
    audio_ring_push_(&system->ring, &command);
}

void audio_stop(AudioSystem* system, AudioBufferHandle handle) {
    if (!system || handle.generation == 0u || handle.index >= AUDIO_MAX_BUFFERS) {
        return;
    }
    AudioCommand command = {};
    command.kind = AudioCommandKind_StopBuffer;
    command.bufferIndex = handle.index;
    command.bufferGeneration = handle.generation;
    audio_ring_push_(&system->ring, &command);
}

void audio_set_master_gain(AudioSystem* system, F32 gain) {
    if (!system) {
        return;
    }
    AudioCommand command = {};
    command.kind = AudioCommandKind_MasterGain;
    command.gain = gain;
    audio_ring_push_(&system->ring, &command);
}

AudioStats audio_stats(AudioSystem* system) {
    AudioStats stats = {};
    if (!system) {
        return stats;
    }
    stats.deviceOpen = ATOMIC_LOAD(&system->deviceOpen, MEMORY_ORDER_ACQUIRE);
    stats.buffersLive = system->buffersLive;
    stats.voicesActive = ATOMIC_LOAD(&system->voicesActive, MEMORY_ORDER_RELAXED);
    stats.voicesDropped = ATOMIC_LOAD(&system->voicesDropped, MEMORY_ORDER_RELAXED);
    stats.commandsDropped = ATOMIC_LOAD(&system->ring.dropped, MEMORY_ORDER_RELAXED);
    stats.callbackCount = ATOMIC_LOAD(&system->callbackCount, MEMORY_ORDER_RELAXED);
    stats.lastCallbackNanos = ATOMIC_LOAD(&system->lastCallbackNanos, MEMORY_ORDER_RELAXED);
    stats.maxCallbackNanos = ATOMIC_LOAD(&system->maxCallbackNanos, MEMORY_ORDER_RELAXED);
    stats.blobsLeaked = system->blobsLeaked;
    return stats;
}
