#pragma once

//
// Cooked asset formats, shared by the cooker tool and the runtime decoders.
// The cooker writes these; the runtime never parses interchange formats.
//

#define ASSET_MESH_MAGIC 0x48534D55u
#define ASSET_MESH_VERSION 1u
#define ASSET_TEXTURE_MAGIC 0x58455455u
#define ASSET_TEXTURE_VERSION 1u
#define ASSET_TEXTURE_MAX_MIPS 14u

#define ASSET_TEXTURE_FORMAT_BC1 1u
#define ASSET_TEXTURE_FORMAT_BC3 2u

// Followed by vertexCount * 8 F32 words (position xyz, normal xyz, uv) then
// indexCount * U32 mesh-relative indices.
struct AssetMeshHeader {
    U32 magic;
    U32 version;
    U32 vertexCount;
    U32 indexCount;
    F32 boundsCenter[3];
    F32 boundsExtents[3];
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
