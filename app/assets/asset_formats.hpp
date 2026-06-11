#pragma once

//
// Cooked asset formats, shared by the cooker tool and the runtime decoders.
// The cooker writes these; the runtime never parses interchange formats.
//

#define ASSET_MODEL_MAGIC 0x4C444D55u
#define ASSET_MODEL_VERSION 1u
#define ASSET_MODEL_NO_TEXTURE 0xFFFFFFFFu
#define ASSET_TEXTURE_MAGIC 0x58455455u
#define ASSET_TEXTURE_VERSION 1u
#define ASSET_TEXTURE_MAX_MIPS 14u

#define ASSET_TEXTURE_FORMAT_BC1 1u
#define ASSET_TEXTURE_FORMAT_BC3 2u

#define ASSET_AUDIO_MAGIC 0x44554155u
#define ASSET_AUDIO_VERSION 1u
#define ASSET_AUDIO_SAMPLE_RATE 48000u
#define ASSET_AUDIO_CHANNELS 2u

// Cooked model (UMDL): the whole default glTF scene flattened at cook time.
// Sections are deduped primitives in object space; instances place sections
// with model-space transforms (node tree composed, row-vector convention);
// materials are deduped by value and reference model-local textures, which
// cook as sibling files "<stem>_tex<N>.utex". File layout after the header:
//   AssetModelSection[sectionCount]
//   AssetModelInstance[instanceCount]
//   AssetModelMaterial[materialCount]
//   vertexCount * 8 F32 words (position xyz, normal xyz, uv)
//   indexCount * U32 section-relative indices
struct AssetModelHeader {
    U32 magic;
    U32 version;
    U32 sectionCount;
    U32 instanceCount;
    U32 materialCount;
    U32 textureCount;
    U32 vertexCount;
    U32 indexCount;
    F32 boundsCenter[3];
    F32 boundsRadius;
};

struct AssetModelSection {
    U32 firstIndex;
    U32 indexCount;
    U32 baseVertex;
    U32 pad0;
    F32 boundsCenter[3];
    F32 boundsExtents[3];
};

struct AssetModelInstance {
    U32 sectionIndex;
    U32 materialIndex;
    F32 transform[16];
};

struct AssetModelMaterial {
    F32 baseColor[4];
    U32 textureIndex; // model-local, ASSET_MODEL_NO_TEXTURE when untextured
    U32 pad0;
    U32 pad1;
    U32 pad2;
};

// Cooked audio (UAUD): PCM normalized at cook time to 48 kHz interleaved
// stereo F32 so the runtime mixer is copy-accumulate — no resampling, no
// format branches on the audio thread. Loop points are frame indices;
// loopEnd == 0 means the whole clip. File layout after the header:
//   frameCount * 2 F32 samples
struct AssetAudioHeader {
    U32 magic;
    U32 version;
    U32 frameCount;
    U32 sampleRate;
    U32 channelCount;
    U32 loopBegin;
    U32 loopEnd;
    U32 pad0;
};

// Mip data offsets are from the start of the file.
struct AssetTextureHeader {
    U32 magic;
    U32 version;
    U32 width;
    U32 height;
    U32 mipCount;
    U32 format;
    U32 mipOffsets[ASSET_TEXTURE_MAX_MIPS];
    U32 mipSizes[ASSET_TEXTURE_MAX_MIPS];
};
