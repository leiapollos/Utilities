//
// The audio-thread kernels: SPSC ring semantics (FIFO, full-drop,
// wraparound) and the voice mix (accumulate, one-shot end, block
// boundaries, loop wrap, loop points). What the callback executes is
// exactly what runs here.
//

static AudioCommand test_audio_command_(U32 kind, U32 bufferIndex, F32 gain) {
    AudioCommand command = {};
    command.kind = kind;
    command.bufferIndex = bufferIndex;
    command.bufferGeneration = 1u;
    command.gain = gain;
    return command;
}

static void test_audio_(void) {
    // Ring: FIFO order, empty pop fails.
    {
        static AudioCommandRing ring;
        MEMSET(&ring, 0, sizeof(ring));
        AudioCommand out = {};
        TEST_CHECK(!audio_ring_pop_(&ring, &out));
        for (U32 at = 0u; at < 3u; ++at) {
            AudioCommand command = test_audio_command_(AudioCommandKind_Play, at, (F32)at);
            TEST_CHECK(audio_ring_push_(&ring, &command));
        }
        for (U32 at = 0u; at < 3u; ++at) {
            TEST_CHECK(audio_ring_pop_(&ring, &out));
            TEST_CHECK(out.bufferIndex == at);
        }
        TEST_CHECK(!audio_ring_pop_(&ring, &out));
    }

    // Ring: full drops and counts; consuming reopens space.
    {
        static AudioCommandRing ring;
        MEMSET(&ring, 0, sizeof(ring));
        U32 accepted = 0u;
        for (U32 at = 0u; at < AUDIO_COMMAND_RING_CAPACITY; ++at) {
            AudioCommand command = test_audio_command_(AudioCommandKind_Play, at, 0.0f);
            accepted += audio_ring_push_(&ring, &command) ? 1u : 0u;
        }
        TEST_CHECK(accepted == AUDIO_COMMAND_RING_CAPACITY);
        AudioCommand overflow = test_audio_command_(AudioCommandKind_Play, 999u, 0.0f);
        TEST_CHECK(!audio_ring_push_(&ring, &overflow));
        TEST_CHECK(ring.dropped == 1ull);
        AudioCommand out = {};
        TEST_CHECK(audio_ring_pop_(&ring, &out));
        TEST_CHECK(audio_ring_push_(&ring, &overflow));
    }

    // Ring: indices stay correct far past one capacity (wraparound).
    {
        static AudioCommandRing ring;
        MEMSET(&ring, 0, sizeof(ring));
        AudioCommand out = {};
        U32 mismatches = 0u;
        for (U32 at = 0u; at < AUDIO_COMMAND_RING_CAPACITY * 3u; ++at) {
            AudioCommand command = test_audio_command_(AudioCommandKind_Play, at, 0.0f);
            if (!audio_ring_push_(&ring, &command) ||
                !audio_ring_pop_(&ring, &out) ||
                out.bufferIndex != at) {
                mismatches += 1u;
            }
        }
        TEST_CHECK(mismatches == 0u);
        TEST_CHECK(ring.dropped == 0ull);
    }

    // Voice start: fills a free slot; a full pool rejects.
    {
        AudioVoice voices[4] = {};
        AudioCommand play = test_audio_command_(AudioCommandKind_Play, 7u, 0.5f);
        play.loop = 1u;
        AudioVoice* voice = audio_voice_start_(voices, 4u, &play);
        TEST_CHECK(voice != 0);
        TEST_CHECK(voice->active && voice->loop);
        TEST_CHECK(voice->bufferIndex == 7u && voice->bufferGeneration == 1u);
        TEST_CHECK(voice->cursor == 0u);
        TEST_CHECK_NEAR(voice->gain, 0.5f, 1e-6f);
        for (U32 at = 0u; at < 4u; ++at) {
            voices[at].active = 1;
        }
        TEST_CHECK(audio_voice_start_(voices, 4u, &play) == 0);
    }

    // Stereo PCM ramp: frame i carries (i, -i) so positions are provable.
    F32 pcm[16 * 2];
    for (U32 frame = 0u; frame < 16u; ++frame) {
        pcm[frame * 2u + 0u] = (F32)frame;
        pcm[frame * 2u + 1u] = -(F32)frame;
    }

    // Mix accumulates under voice*master gain; it never overwrites.
    {
        AudioVoice voice = {};
        voice.active = 1;
        voice.gain = 0.5f;
        F32 out[4 * 2];
        for (U32 at = 0u; at < 8u; ++at) {
            out[at] = 1.0f;
        }
        audio_voice_mix_(&voice, pcm, 16u, 0u, 0u, 4.0f, out, 4u);
        TEST_CHECK_NEAR(out[0], 1.0f, 1e-6f);  // frame 0: 1 + 0*2
        TEST_CHECK_NEAR(out[2], 3.0f, 1e-6f);  // frame 1: 1 + 1*2
        TEST_CHECK_NEAR(out[3], -1.0f, 1e-6f); // frame 1 right: 1 + (-1)*2
        TEST_CHECK_NEAR(out[6], 7.0f, 1e-6f);  // frame 3: 1 + 3*2
        TEST_CHECK(voice.active && voice.cursor == 4u);
    }

    // One-shot ends exactly at frameCount; the tail stays untouched.
    {
        AudioVoice voice = {};
        voice.active = 1;
        voice.gain = 1.0f;
        F32 out[16 * 2] = {};
        audio_voice_mix_(&voice, pcm, 10u, 0u, 0u, 1.0f, out, 16u);
        TEST_CHECK(!voice.active);
        TEST_CHECK_NEAR(out[9 * 2], 9.0f, 1e-6f);
        TEST_CHECK_NEAR(out[10 * 2], 0.0f, 1e-6f);
        TEST_CHECK_NEAR(out[15 * 2 + 1], 0.0f, 1e-6f);
    }

    // One-shot across block boundaries: 10 frames over two 6-frame blocks.
    {
        AudioVoice voice = {};
        voice.active = 1;
        voice.gain = 1.0f;
        F32 blockA[6 * 2] = {};
        F32 blockB[6 * 2] = {};
        audio_voice_mix_(&voice, pcm, 10u, 0u, 0u, 1.0f, blockA, 6u);
        TEST_CHECK(voice.active && voice.cursor == 6u);
        audio_voice_mix_(&voice, pcm, 10u, 0u, 0u, 1.0f, blockB, 6u);
        TEST_CHECK(!voice.active);
        TEST_CHECK_NEAR(blockA[5 * 2], 5.0f, 1e-6f);
        TEST_CHECK_NEAR(blockB[0], 6.0f, 1e-6f);
        TEST_CHECK_NEAR(blockB[3 * 2], 9.0f, 1e-6f);
        TEST_CHECK_NEAR(blockB[4 * 2], 0.0f, 1e-6f); // past the clip
    }

    // Whole-clip loop wraps and keeps playing (loopEnd 0 = frameCount).
    {
        AudioVoice voice = {};
        voice.active = 1;
        voice.gain = 1.0f;
        voice.loop = 1;
        F32 out[20 * 2] = {};
        audio_voice_mix_(&voice, pcm, 8u, 0u, 0u, 1.0f, out, 20u);
        TEST_CHECK(voice.active);
        TEST_CHECK(voice.cursor == 4u); // 20 = 8 + 8 + 4
        TEST_CHECK_NEAR(out[7 * 2], 7.0f, 1e-6f);
        TEST_CHECK_NEAR(out[8 * 2], 0.0f, 1e-6f);  // wrapped to frame 0
        TEST_CHECK_NEAR(out[19 * 2], 3.0f, 1e-6f); // second wrap, frame 3
    }

    // Loop points: intro 0..3 plays once, then 4..7 repeats.
    {
        AudioVoice voice = {};
        voice.active = 1;
        voice.gain = 1.0f;
        voice.loop = 1;
        F32 out[12 * 2] = {};
        audio_voice_mix_(&voice, pcm, 16u, 4u, 8u, 1.0f, out, 12u);
        TEST_CHECK(voice.active);
        TEST_CHECK(voice.cursor == 8u);
        TEST_CHECK_NEAR(out[3 * 2], 3.0f, 1e-6f);  // intro
        TEST_CHECK_NEAR(out[7 * 2], 7.0f, 1e-6f);  // loop body first pass
        TEST_CHECK_NEAR(out[8 * 2], 4.0f, 1e-6f);  // wrapped to loopBegin
        TEST_CHECK_NEAR(out[11 * 2], 7.0f, 1e-6f);
    }
}
