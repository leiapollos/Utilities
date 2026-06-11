//
// Created by André Leite on 11/06/2026.
//
// WAV (PCM16 / F32, mono / stereo, any rate) -> UAUD. All format work
// happens here: convert to F32, widen mono to stereo, linear-resample to
// ASSET_AUDIO_SAMPLE_RATE. The runtime mixer never sees anything but
// 48 kHz interleaved stereo F32.
//

#pragma once

struct WavData {
    U32 sampleRate;
    U32 channelCount;
    U32 frameCount;
    F32* samples; // interleaved, channelCount wide
};

static U32 cooker_wav_u32_(const U8* bytes) {
    return (U32)bytes[0] | ((U32)bytes[1] << 8u) | ((U32)bytes[2] << 16u) | ((U32)bytes[3] << 24u);
}

static U32 cooker_wav_u16_(const U8* bytes) {
    return (U32)bytes[0] | ((U32)bytes[1] << 8u);
}

static B32 cooker_wav_parse_(Arena* arena, const U8* data, U64 size, WavData* outWav) {
    if (size < 44u ||
        MEMCMP(data, "RIFF", 4) != 0 || MEMCMP(data + 8u, "WAVE", 4) != 0) {
        fprintf(stderr, "cooker: not a RIFF WAV\n");
        return 0;
    }

    U32 audioFormat = 0u;
    U32 channelCount = 0u;
    U32 sampleRate = 0u;
    U32 bitsPerSample = 0u;
    U32 blockAlign = 0u;
    const U8* sampleData = 0;
    U64 sampleBytes = 0u;

    U64 at = 12u;
    while (at + 8u <= size) {
        U32 chunkSize = cooker_wav_u32_(data + at + 4u);
        const U8* chunk = data + at + 8u;
        if (at + 8u + chunkSize > size) {
            break;
        }
        if (MEMCMP(data + at, "fmt ", 4) == 0 && chunkSize >= 16u) {
            audioFormat = cooker_wav_u16_(chunk + 0u);
            channelCount = cooker_wav_u16_(chunk + 2u);
            sampleRate = cooker_wav_u32_(chunk + 4u);
            blockAlign = cooker_wav_u16_(chunk + 12u);
            bitsPerSample = cooker_wav_u16_(chunk + 14u);
        } else if (MEMCMP(data + at, "data", 4) == 0) {
            sampleData = chunk;
            sampleBytes = chunkSize;
        }
        at += 8u + chunkSize + (chunkSize & 1u);
    }

    B32 isPcm16 = audioFormat == 1u && bitsPerSample == 16u;
    B32 isFloat32 = audioFormat == 3u && bitsPerSample == 32u;
    if ((!isPcm16 && !isFloat32) ||
        channelCount == 0u || channelCount > 2u ||
        sampleRate < 8000u || sampleRate > 192000u ||
        !sampleData || sampleBytes == 0u || blockAlign == 0u) {
        fprintf(stderr, "cooker: unsupported WAV (format %u, %u ch, %u Hz, %u bit)\n",
                audioFormat, channelCount, sampleRate, bitsPerSample);
        return 0;
    }

    U32 frameCount = (U32)(sampleBytes / blockAlign);
    if (frameCount == 0u) {
        fprintf(stderr, "cooker: empty WAV\n");
        return 0;
    }

    F32* samples = ARENA_PUSH_ARRAY(arena, F32, (U64)frameCount * channelCount);
    for (U64 sample = 0u; sample < (U64)frameCount * channelCount; ++sample) {
        if (isPcm16) {
            S16 raw = (S16)cooker_wav_u16_(sampleData + sample * 2u);
            samples[sample] = (F32)raw / 32768.0f;
        } else {
            U32 bits = cooker_wav_u32_(sampleData + sample * 4u);
            F32 value;
            MEMCPY(&value, &bits, sizeof(value));
            samples[sample] = value;
        }
    }

    outWav->sampleRate = sampleRate;
    outWav->channelCount = channelCount;
    outWav->frameCount = frameCount;
    outWav->samples = samples;
    return 1;
}

// Widen to stereo and linear-resample in one pass.
static F32* cooker_audio_normalize_(Arena* arena, const WavData* wav, U32 outRate, U32* outFrameCount) {
    U64 outFrames = ((U64)wav->frameCount * outRate) / wav->sampleRate;
    if (outFrames == 0u) {
        outFrames = 1u;
    }
    F32* out = ARENA_PUSH_ARRAY(arena, F32, outFrames * 2u);
    F64 step = (F64)wav->sampleRate / (F64)outRate;
    for (U64 frame = 0u; frame < outFrames; ++frame) {
        F64 srcPos = (F64)frame * step;
        U32 src0 = (U32)srcPos;
        if (src0 >= wav->frameCount - 1u) {
            src0 = wav->frameCount - 1u;
        }
        U32 src1 = (src0 + 1u < wav->frameCount) ? src0 + 1u : src0;
        F32 frac = (F32)(srcPos - (F64)src0);
        for (U32 channel = 0u; channel < 2u; ++channel) {
            U32 sourceChannel = (channel < wav->channelCount) ? channel : 0u;
            F32 a = wav->samples[(U64)src0 * wav->channelCount + sourceChannel];
            F32 b = wav->samples[(U64)src1 * wav->channelCount + sourceChannel];
            out[frame * 2u + channel] = a + (b - a) * frac;
        }
    }
    *outFrameCount = (U32)outFrames;
    return out;
}

static B32 cook_audio(Arena* arena, const U8* data, U64 size, StringU8 outputDir, StringU8 stem) {
    WavData wav = {};
    if (!cooker_wav_parse_(arena, data, size, &wav)) {
        return 0;
    }

    U32 frameCount = 0u;
    F32* samples = cooker_audio_normalize_(arena, &wav, ASSET_AUDIO_SAMPLE_RATE, &frameCount);

    AssetAudioHeader header = {};
    header.magic = ASSET_AUDIO_MAGIC;
    header.version = ASSET_AUDIO_VERSION;
    header.frameCount = frameCount;
    header.sampleRate = ASSET_AUDIO_SAMPLE_RATE;
    header.channelCount = ASSET_AUDIO_CHANNELS;
    header.loopBegin = 0u;
    header.loopEnd = 0u; // whole clip

    StringU8 outPath = str8_fmt(arena, "{}/{}.uaud", outputDir, stem);
    char* pathCStr = ARENA_PUSH_ARRAY(arena, char, outPath.size + 1);
    MEMCPY(pathCStr, outPath.data, outPath.size);
    pathCStr[outPath.size] = '\0';
    OS_Handle file = OS_file_open(pathCStr, OS_FileOpenMode_Create);
    if (!file.handle) {
        fprintf(stderr, "cooker: cannot open output audio\n");
        return 0;
    }
    OS_file_write(file, sizeof(header), &header);
    OS_file_write(file, (U64)frameCount * ASSET_AUDIO_CHANNELS * sizeof(F32), samples);
    OS_file_close(file);

    printf("cooker: audio %u frames (%.2fs) from %u Hz %u ch\n",
           frameCount, (F64)frameCount / (F64)ASSET_AUDIO_SAMPLE_RATE,
           wav.sampleRate, wav.channelCount);
    return 1;
}
