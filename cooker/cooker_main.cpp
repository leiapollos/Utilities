//
// Asset cooker: glTF binary (.glb) in -> UMDL/UTEX; WAV in -> UAUD.
// Tool-side only; the runtime decodes the cooked formats and never sees
// interchange formats.
//

#include "nstl/base/base_include.hpp"
#include "nstl/base/base_spmd.hpp"
#include "nstl/os/os_include.hpp"

#include "nstl/os/os_include.cpp"
#include "nstl/base/base_include.cpp"

#include "engine/engine_assets.hpp"
#include "cooker_math.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "third_party/stb/stb_image.h"
#define STB_DXT_IMPLEMENTATION
#include "third_party/stb/stb_dxt.h"

#include <stdio.h>
#include <math.h>

#include "cooker_audio.hpp"

// ////////////////////////
// Minimal JSON (arena DOM, glTF subset: no surrogate escapes)

enum JsonKind {
    JsonKind_Null = 0,
    JsonKind_Bool,
    JsonKind_Number,
    JsonKind_String,
    JsonKind_Array,
    JsonKind_Object,
};

struct JsonValue;

struct JsonMember {
    StringU8 key;
    JsonValue* value;
    JsonMember* next;
};

struct JsonElement {
    JsonValue* value;
    JsonElement* next;
};

struct JsonValue {
    JsonKind kind;
    B32 boolean;
    F64 number;
    StringU8 string;
    JsonMember* members;
    JsonElement* elements;
    U32 count;
};

struct JsonParser {
    Arena* arena;
    const U8* at;
    const U8* end;
    B32 failed;
};

static void json_skip_space_(JsonParser* parser) {
    while (parser->at < parser->end) {
        U8 c = *parser->at;
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        parser->at += 1;
    }
}

static JsonValue* json_parse_value_(JsonParser* parser);

static JsonValue* json_new_(JsonParser* parser, JsonKind kind) {
    JsonValue* value = ARENA_PUSH_STRUCT(parser->arena, JsonValue);
    MEMSET(value, 0, sizeof(*value));
    value->kind = kind;
    return value;
}

static B32 json_consume_(JsonParser* parser, U8 expected) {
    json_skip_space_(parser);
    if (parser->at < parser->end && *parser->at == expected) {
        parser->at += 1;
        return 1;
    }
    parser->failed = 1;
    return 0;
}

static StringU8 json_parse_string_body_(JsonParser* parser) {
    StringU8 result = {};
    if (!json_consume_(parser, '"')) {
        return result;
    }
    U8* out = (U8*)arena_push(parser->arena, 0u, 1u);
    U64 size = 0u;
    while (parser->at < parser->end && *parser->at != '"') {
        U8 c = *parser->at;
        parser->at += 1;
        if (c == '\\' && parser->at < parser->end) {
            U8 escape = *parser->at;
            parser->at += 1;
            switch (escape) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u': {
                    parser->at += (parser->end - parser->at >= 4) ? 4 : (parser->end - parser->at);
                    c = '?';
                } break;
                default: c = escape; break;
            }
        }
        U8* slot = (U8*)arena_push(parser->arena, 1u, 1u);
        *slot = c;
        size += 1u;
    }
    if (parser->at < parser->end) {
        parser->at += 1;
    } else {
        parser->failed = 1;
    }
    result.data = out;
    result.size = size;
    return result;
}

static JsonValue* json_parse_value_(JsonParser* parser) {
    json_skip_space_(parser);
    if (parser->failed || parser->at >= parser->end) {
        parser->failed = 1;
        return json_new_(parser, JsonKind_Null);
    }

    U8 c = *parser->at;
    if (c == '{') {
        parser->at += 1;
        JsonValue* object = json_new_(parser, JsonKind_Object);
        JsonMember* tail = 0;
        json_skip_space_(parser);
        if (parser->at < parser->end && *parser->at == '}') {
            parser->at += 1;
            return object;
        }
        for (;;) {
            JsonMember* member = ARENA_PUSH_STRUCT(parser->arena, JsonMember);
            MEMSET(member, 0, sizeof(*member));
            member->key = json_parse_string_body_(parser);
            json_consume_(parser, ':');
            member->value = json_parse_value_(parser);
            if (parser->failed) {
                return object;
            }
            if (tail) {
                tail->next = member;
            } else {
                object->members = member;
            }
            tail = member;
            object->count += 1u;
            json_skip_space_(parser);
            if (parser->at < parser->end && *parser->at == ',') {
                parser->at += 1;
                continue;
            }
            json_consume_(parser, '}');
            return object;
        }
    }
    if (c == '[') {
        parser->at += 1;
        JsonValue* array = json_new_(parser, JsonKind_Array);
        JsonElement* tail = 0;
        json_skip_space_(parser);
        if (parser->at < parser->end && *parser->at == ']') {
            parser->at += 1;
            return array;
        }
        for (;;) {
            JsonElement* element = ARENA_PUSH_STRUCT(parser->arena, JsonElement);
            MEMSET(element, 0, sizeof(*element));
            element->value = json_parse_value_(parser);
            if (parser->failed) {
                return array;
            }
            if (tail) {
                tail->next = element;
            } else {
                array->elements = element;
            }
            tail = element;
            array->count += 1u;
            json_skip_space_(parser);
            if (parser->at < parser->end && *parser->at == ',') {
                parser->at += 1;
                continue;
            }
            json_consume_(parser, ']');
            return array;
        }
    }
    if (c == '"') {
        JsonValue* value = json_new_(parser, JsonKind_String);
        value->string = json_parse_string_body_(parser);
        return value;
    }
    if (c == 't') {
        parser->at += 4;
        JsonValue* value = json_new_(parser, JsonKind_Bool);
        value->boolean = 1;
        return value;
    }
    if (c == 'f') {
        parser->at += 5;
        return json_new_(parser, JsonKind_Bool);
    }
    if (c == 'n') {
        parser->at += 4;
        return json_new_(parser, JsonKind_Null);
    }

    B32 negative = 0;
    if (c == '-') {
        negative = 1;
        parser->at += 1;
    }
    F64 value = 0.0;
    while (parser->at < parser->end && *parser->at >= '0' && *parser->at <= '9') {
        value = value * 10.0 + (F64)(*parser->at - '0');
        parser->at += 1;
    }
    if (parser->at < parser->end && *parser->at == '.') {
        parser->at += 1;
        F64 scale = 0.1;
        while (parser->at < parser->end && *parser->at >= '0' && *parser->at <= '9') {
            value += (F64)(*parser->at - '0') * scale;
            scale *= 0.1;
            parser->at += 1;
        }
    }
    if (parser->at < parser->end && (*parser->at == 'e' || *parser->at == 'E')) {
        parser->at += 1;
        B32 expNegative = 0;
        if (parser->at < parser->end && (*parser->at == '+' || *parser->at == '-')) {
            expNegative = (*parser->at == '-');
            parser->at += 1;
        }
        F64 exponent = 0.0;
        while (parser->at < parser->end && *parser->at >= '0' && *parser->at <= '9') {
            exponent = exponent * 10.0 + (F64)(*parser->at - '0');
            parser->at += 1;
        }
        F64 power = 1.0;
        for (F64 e = 0.0; e < exponent; e += 1.0) {
            power *= 10.0;
        }
        value = expNegative ? (value / power) : (value * power);
    }
    JsonValue* number = json_new_(parser, JsonKind_Number);
    number->number = negative ? -value : value;
    return number;
}

static JsonValue* json_parse(Arena* arena, const U8* data, U64 size) {
    JsonParser parser = {};
    parser.arena = arena;
    parser.at = data;
    parser.end = data + size;
    JsonValue* root = json_parse_value_(&parser);
    return parser.failed ? 0 : root;
}

static JsonValue* json_get(JsonValue* object, const char* key) {
    if (!object || object->kind != JsonKind_Object) {
        return 0;
    }
    StringU8 keyStr = str8(key);
    for (JsonMember* member = object->members; member; member = member->next) {
        if (str8_equal(member->key, keyStr)) {
            return member->value;
        }
    }
    return 0;
}

static JsonValue* json_index(JsonValue* array, U32 index) {
    if (!array || array->kind != JsonKind_Array) {
        return 0;
    }
    U32 at = 0u;
    for (JsonElement* element = array->elements; element; element = element->next, ++at) {
        if (at == index) {
            return element->value;
        }
    }
    return 0;
}

static U32 json_u32(JsonValue* value, U32 fallback) {
    return (value && value->kind == JsonKind_Number) ? (U32)value->number : fallback;
}

// ////////////////////////
// GLB / glTF subset

struct GlbFile {
    JsonValue* json;
    const U8* binData;
    U64 binSize;
};

static B32 glb_parse(Arena* arena, const U8* data, U64 size, GlbFile* outGlb) {
    MEMSET(outGlb, 0, sizeof(*outGlb));
    if (size < 12u) {
        return 0;
    }
    const U32* header = (const U32*)data;
    if (header[0] != 0x46546C67u || header[1] != 2u) {
        return 0;
    }
    U64 at = 12u;
    while (at + 8u <= size) {
        U32 chunkLength = *(const U32*)(data + at);
        U32 chunkType = *(const U32*)(data + at + 4u);
        const U8* chunkData = data + at + 8u;
        if (at + 8u + chunkLength > size) {
            return 0;
        }
        if (chunkType == 0x4E4F534Au) {
            outGlb->json = json_parse(arena, chunkData, chunkLength);
        } else if (chunkType == 0x004E4942u) {
            outGlb->binData = chunkData;
            outGlb->binSize = chunkLength;
        }
        at += 8u + chunkLength;
    }
    return outGlb->json != 0 && outGlb->binData != 0;
}

struct GltfAccessorView {
    const U8* data;
    U32 count;
    U32 componentType;
    U32 componentCount;
    U32 stride;
};

static B32 gltf_accessor_view(GlbFile* glb, JsonValue* root, U32 accessorIndex, GltfAccessorView* outView) {
    MEMSET(outView, 0, sizeof(*outView));
    JsonValue* accessor = json_index(json_get(root, "accessors"), accessorIndex);
    if (!accessor) {
        return 0;
    }
    JsonValue* bufferView = json_index(json_get(root, "bufferViews"), json_u32(json_get(accessor, "bufferView"), 0xFFFFFFFFu));
    if (!bufferView) {
        return 0;
    }

    U32 componentType = json_u32(json_get(accessor, "componentType"), 0u);
    JsonValue* typeValue = json_get(accessor, "type");
    U32 componentCount = 0u;
    if (typeValue && typeValue->kind == JsonKind_String) {
        if (str8_equal(typeValue->string, str8("SCALAR"))) componentCount = 1u;
        if (str8_equal(typeValue->string, str8("VEC2"))) componentCount = 2u;
        if (str8_equal(typeValue->string, str8("VEC3"))) componentCount = 3u;
        if (str8_equal(typeValue->string, str8("VEC4"))) componentCount = 4u;
    }
    U32 componentSize = 0u;
    switch (componentType) {
        case 5120: case 5121: componentSize = 1u; break;
        case 5122: case 5123: componentSize = 2u; break;
        case 5125: case 5126: componentSize = 4u; break;
        default: return 0;
    }
    if (componentCount == 0u) {
        return 0;
    }

    U64 viewOffset = json_u32(json_get(bufferView, "byteOffset"), 0u);
    U64 accessorOffset = json_u32(json_get(accessor, "byteOffset"), 0u);
    U32 declaredStride = json_u32(json_get(bufferView, "byteStride"), 0u);

    outView->data = glb->binData + viewOffset + accessorOffset;
    outView->count = json_u32(json_get(accessor, "count"), 0u);
    outView->componentType = componentType;
    outView->componentCount = componentCount;
    outView->stride = declaredStride ? declaredStride : componentSize * componentCount;
    return 1;
}

// ////////////////////////
// Model cooking: walk the default scene, flatten node transforms, dedupe
// primitives into sections and materials by value, emit one UMDL.

struct CookedVertex {
    F32 position[3];
    F32 normal[3];
    F32 uv[2];
};

#define COOK_MAX_SECTIONS 1024u
#define COOK_MAX_INSTANCES 4096u
#define COOK_MAX_MATERIALS 256u
#define COOK_MAX_TEXTURES 64u

static F32 json_f32(JsonValue* value, F32 fallback) {
    return (value && value->kind == JsonKind_Number) ? (F32)value->number : fallback;
}

struct CookModel {
    U32 sectionMesh[COOK_MAX_SECTIONS];
    U32 sectionPrim[COOK_MAX_SECTIONS];
    AssetModelSection sections[COOK_MAX_SECTIONS];
    U32 sectionCount;
    AssetModelInstance instances[COOK_MAX_INSTANCES];
    U32 instanceCount;
    AssetModelMaterial materials[COOK_MAX_MATERIALS];
    U32 materialCount;
    U32 textureImages[COOK_MAX_TEXTURES];
    U32 textureCount;
    B32 failed;
};

static U32 cook_model_section_(CookModel* model, U32 meshIndex, U32 primIndex) {
    for (U32 at = 0u; at < model->sectionCount; ++at) {
        if (model->sectionMesh[at] == meshIndex && model->sectionPrim[at] == primIndex) {
            return at;
        }
    }
    if (model->sectionCount >= COOK_MAX_SECTIONS) {
        fprintf(stderr, "cooker: section cap exceeded\n");
        model->failed = 1;
        return 0u;
    }
    U32 section = model->sectionCount;
    model->sectionCount += 1u;
    model->sectionMesh[section] = meshIndex;
    model->sectionPrim[section] = primIndex;
    return section;
}

static U32 cook_model_material_(CookModel* model, JsonValue* root, U32 gltfMaterial) {
    F32 factor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    U32 sourceImage = 0xFFFFFFFFu;
    JsonValue* material = json_index(json_get(root, "materials"), gltfMaterial);
    if (material) {
        JsonValue* pbr = json_get(material, "pbrMetallicRoughness");
        JsonValue* factorValue = json_get(pbr, "baseColorFactor");
        if (factorValue) {
            for (U32 at = 0u; at < 4u; ++at) {
                factor[at] = json_f32(json_index(factorValue, at), 1.0f);
            }
        }
        JsonValue* baseColorTexture = json_get(pbr, "baseColorTexture");
        if (baseColorTexture) {
            JsonValue* texture = json_index(json_get(root, "textures"),
                                            json_u32(json_get(baseColorTexture, "index"), 0xFFFFFFFFu));
            sourceImage = json_u32(json_get(texture, "source"), 0xFFFFFFFFu);
        }
    }

    U32 textureLocal = ASSET_MODEL_NO_TEXTURE;
    if (sourceImage != 0xFFFFFFFFu) {
        for (U32 at = 0u; at < model->textureCount; ++at) {
            if (model->textureImages[at] == sourceImage) {
                textureLocal = at;
                break;
            }
        }
        if (textureLocal == ASSET_MODEL_NO_TEXTURE) {
            if (model->textureCount >= COOK_MAX_TEXTURES) {
                fprintf(stderr, "cooker: texture cap exceeded\n");
                model->failed = 1;
                return 0u;
            }
            textureLocal = model->textureCount;
            model->textureImages[model->textureCount] = sourceImage;
            model->textureCount += 1u;
        }
    }

    for (U32 at = 0u; at < model->materialCount; ++at) {
        const AssetModelMaterial* existing = model->materials + at;
        if (existing->textureIndex == textureLocal &&
            existing->baseColor[0] == factor[0] && existing->baseColor[1] == factor[1] &&
            existing->baseColor[2] == factor[2] && existing->baseColor[3] == factor[3]) {
            return at;
        }
    }
    if (model->materialCount >= COOK_MAX_MATERIALS) {
        fprintf(stderr, "cooker: material cap exceeded\n");
        model->failed = 1;
        return 0u;
    }
    U32 result = model->materialCount;
    model->materialCount += 1u;
    AssetModelMaterial* cooked = model->materials + result;
    cooked->baseColor[0] = factor[0];
    cooked->baseColor[1] = factor[1];
    cooked->baseColor[2] = factor[2];
    cooked->baseColor[3] = factor[3];
    cooked->textureIndex = textureLocal;
    cooked->pad0 = cooked->pad1 = cooked->pad2 = 0u;
    return result;
}

static void cook_model_walk_(CookModel* model, JsonValue* root, U32 nodeIndex, const F32* parentTransform) {
    JsonValue* node = json_index(json_get(root, "nodes"), nodeIndex);
    if (!node || model->failed) {
        return;
    }

    F32 local[16];
    JsonValue* matrixValue = json_get(node, "matrix");
    if (matrixValue) {
        for (U32 at = 0u; at < 16u; ++at) {
            local[at] = json_f32(json_index(matrixValue, at), (at % 5u == 0u) ? 1.0f : 0.0f);
        }
    } else {
        F32 t[3] = {0.0f, 0.0f, 0.0f};
        F32 q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        F32 s[3] = {1.0f, 1.0f, 1.0f};
        JsonValue* translation = json_get(node, "translation");
        JsonValue* rotation = json_get(node, "rotation");
        JsonValue* scale = json_get(node, "scale");
        for (U32 at = 0u; at < 3u; ++at) {
            t[at] = json_f32(json_index(translation, at), 0.0f);
            s[at] = json_f32(json_index(scale, at), 1.0f);
        }
        for (U32 at = 0u; at < 4u; ++at) {
            q[at] = json_f32(json_index(rotation, at), (at == 3u) ? 1.0f : 0.0f);
        }
        asset_mat_from_trs(t, q, s, local);
    }
    F32 world[16];
    asset_mat_mul(local, parentTransform, world);

    U32 meshIndex = json_u32(json_get(node, "mesh"), 0xFFFFFFFFu);
    if (meshIndex != 0xFFFFFFFFu) {
        JsonValue* primitives = json_get(json_index(json_get(root, "meshes"), meshIndex), "primitives");
        for (U32 prim = 0u;; ++prim) {
            JsonValue* primitive = json_index(primitives, prim);
            if (!primitive) {
                break;
            }
            U32 section = cook_model_section_(model, meshIndex, prim);
            U32 material = cook_model_material_(model, root,
                                                json_u32(json_get(primitive, "material"), 0xFFFFFFFFu));
            if (model->failed) {
                return;
            }
            if (model->instanceCount >= COOK_MAX_INSTANCES) {
                fprintf(stderr, "cooker: instance cap exceeded\n");
                model->failed = 1;
                return;
            }
            AssetModelInstance* instance = model->instances + model->instanceCount;
            model->instanceCount += 1u;
            instance->sectionIndex = section;
            instance->materialIndex = material;
            MEMCPY(instance->transform, world, sizeof(instance->transform));
        }
    }

    JsonValue* children = json_get(node, "children");
    for (U32 child = 0u;; ++child) {
        JsonValue* childValue = json_index(children, child);
        if (!childValue) {
            break;
        }
        cook_model_walk_(model, root, json_u32(childValue, 0xFFFFFFFFu), world);
    }
}

static B32 cook_model_extract_section_(Arena* arena, GlbFile* glb, CookModel* model, U32 section,
                                       CookedVertex** ioVertices, U32* ioVertexCount,
                                       U32** ioIndices, U32* ioIndexCount) {
    JsonValue* root = glb->json;
    JsonValue* primitive = json_index(json_get(json_index(json_get(root, "meshes"), model->sectionMesh[section]),
                                                "primitives"),
                                      model->sectionPrim[section]);
    JsonValue* attributes = json_get(primitive, "attributes");

    GltfAccessorView positions = {};
    GltfAccessorView normals = {};
    GltfAccessorView uvs = {};
    GltfAccessorView indices = {};
    if (!gltf_accessor_view(glb, root, json_u32(json_get(attributes, "POSITION"), 0xFFFFFFFFu), &positions) ||
        positions.componentType != 5126 || positions.componentCount != 3u) {
        fprintf(stderr, "cooker: POSITION accessor unsupported (section %u)\n", section);
        return 0;
    }
    if (!gltf_accessor_view(glb, root, json_u32(json_get(attributes, "NORMAL"), 0xFFFFFFFFu), &normals) ||
        normals.componentType != 5126 || normals.componentCount != 3u) {
        fprintf(stderr, "cooker: NORMAL accessor unsupported (section %u)\n", section);
        return 0;
    }
    B32 hasUv = gltf_accessor_view(glb, root, json_u32(json_get(attributes, "TEXCOORD_0"), 0xFFFFFFFFu), &uvs) &&
                uvs.componentType == 5126 && uvs.componentCount == 2u;
    if (!gltf_accessor_view(glb, root, json_u32(json_get(primitive, "indices"), 0xFFFFFFFFu), &indices) ||
        indices.componentCount != 1u ||
        (indices.componentType != 5123 && indices.componentType != 5125)) {
        fprintf(stderr, "cooker: index accessor unsupported (section %u)\n", section);
        return 0;
    }

    CookedVertex* vertices = ARENA_PUSH_ARRAY(arena, CookedVertex, positions.count);
    U32* cookedIndices = ARENA_PUSH_ARRAY(arena, U32, indices.count);
    if (!vertices || !cookedIndices) {
        return 0;
    }

    F32 boundsMin[3] = {1e30f, 1e30f, 1e30f};
    F32 boundsMax[3] = {-1e30f, -1e30f, -1e30f};
    for (U32 vertexIndex = 0u; vertexIndex < positions.count; ++vertexIndex) {
        const F32* position = (const F32*)(positions.data + (U64)vertexIndex * positions.stride);
        const F32* normal = (const F32*)(normals.data + (U64)vertexIndex * normals.stride);
        for (U32 axis = 0u; axis < 3u; ++axis) {
            vertices[vertexIndex].position[axis] = position[axis];
            vertices[vertexIndex].normal[axis] = normal[axis];
            boundsMin[axis] = MIN(boundsMin[axis], position[axis]);
            boundsMax[axis] = MAX(boundsMax[axis], position[axis]);
        }
        if (hasUv) {
            const F32* uv = (const F32*)(uvs.data + (U64)vertexIndex * uvs.stride);
            vertices[vertexIndex].uv[0] = uv[0];
            vertices[vertexIndex].uv[1] = uv[1];
        } else {
            vertices[vertexIndex].uv[0] = 0.0f;
            vertices[vertexIndex].uv[1] = 0.0f;
        }
    }
    for (U32 indexIndex = 0u; indexIndex < indices.count; ++indexIndex) {
        const U8* source = indices.data + (U64)indexIndex * indices.stride;
        cookedIndices[indexIndex] = (indices.componentType == 5123) ? (U32)(*(const U16*)source)
                                                                    : (*(const U32*)source);
    }

    AssetModelSection* cooked = model->sections + section;
    cooked->firstIndex = *ioIndexCount;
    cooked->indexCount = indices.count;
    cooked->baseVertex = *ioVertexCount;
    cooked->pad0 = 0u;
    for (U32 axis = 0u; axis < 3u; ++axis) {
        cooked->boundsCenter[axis] = (boundsMin[axis] + boundsMax[axis]) * 0.5f;
        cooked->boundsExtents[axis] = (boundsMax[axis] - boundsMin[axis]) * 0.5f;
    }

    *ioVertices = vertices;
    *ioVertexCount += positions.count;
    *ioIndices = cookedIndices;
    *ioIndexCount += indices.count;
    return 1;
}

// ////////////////////////
// Texture cooking

// Rows are padded to the gfx upload alignment so the runtime can hand mip
// memory straight to the staging copy.
#define COOK_TEXTURE_ROW_ALIGNMENT 256u

static U32 cook_texture_mip_(Arena* arena, const U8* rgba, U32 width, U32 height, B32 hasAlpha,
                             U8** outBlocks) {
    U32 blocksX = (width + 3u) / 4u;
    U32 blocksY = (height + 3u) / 4u;
    U32 blockSize = hasAlpha ? 16u : 8u;
    U32 rowStride = ((blocksX * blockSize + COOK_TEXTURE_ROW_ALIGNMENT - 1u) / COOK_TEXTURE_ROW_ALIGNMENT)
                    * COOK_TEXTURE_ROW_ALIGNMENT;
    U32 totalSize = rowStride * blocksY;
    U8* blocks = ARENA_PUSH_ARRAY(arena, U8, totalSize);
    MEMSET(blocks, 0, totalSize);

    for (U32 blockY = 0u; blockY < blocksY; ++blockY) {
        for (U32 blockX = 0u; blockX < blocksX; ++blockX) {
            U8 blockPixels[16 * 4];
            for (U32 pixelY = 0u; pixelY < 4u; ++pixelY) {
                for (U32 pixelX = 0u; pixelX < 4u; ++pixelX) {
                    U32 sourceX = MIN(blockX * 4u + pixelX, width - 1u);
                    U32 sourceY = MIN(blockY * 4u + pixelY, height - 1u);
                    const U8* source = rgba + ((U64)sourceY * width + sourceX) * 4u;
                    U8* destination = blockPixels + (pixelY * 4u + pixelX) * 4u;
                    destination[0] = source[0];
                    destination[1] = source[1];
                    destination[2] = source[2];
                    destination[3] = source[3];
                }
            }
            stb_compress_dxt_block(blocks + (U64)blockY * rowStride + (U64)blockX * blockSize,
                                   blockPixels, hasAlpha ? 1 : 0, STB_DXT_HIGHQUAL);
        }
    }
    *outBlocks = blocks;
    return totalSize;
}

static B32 cook_texture_image(Arena* arena, GlbFile* glb, U32 imageIndex, StringU8 outPath) {
    JsonValue* root = glb->json;
    JsonValue* image = json_index(json_get(root, "images"), imageIndex);
    if (!image) {
        fprintf(stderr, "cooker: image %u missing\n", imageIndex);
        return 0;
    }
    JsonValue* bufferView = json_index(json_get(root, "bufferViews"), json_u32(json_get(image, "bufferView"), 0xFFFFFFFFu));
    if (!bufferView) {
        fprintf(stderr, "cooker: image bufferView missing\n");
        return 0;
    }
    const U8* pngData = glb->binData + json_u32(json_get(bufferView, "byteOffset"), 0u);
    U32 pngSize = json_u32(json_get(bufferView, "byteLength"), 0u);

    int width = 0;
    int height = 0;
    int channels = 0;
    U8* rgba = stbi_load_from_memory(pngData, (int)pngSize, &width, &height, &channels, 4);
    if (!rgba) {
        fprintf(stderr, "cooker: image decode failed\n");
        return 0;
    }

    B32 hasAlpha = 0;
    for (U64 pixelIndex = 0u; pixelIndex < (U64)width * (U64)height; ++pixelIndex) {
        if (rgba[pixelIndex * 4u + 3u] != 255u) {
            hasAlpha = 1;
            break;
        }
    }

    AssetTextureHeader textureHeader = {};
    textureHeader.magic = ASSET_TEXTURE_MAGIC;
    textureHeader.version = ASSET_TEXTURE_VERSION;
    textureHeader.width = (U32)width;
    textureHeader.height = (U32)height;
    textureHeader.format = hasAlpha ? ASSET_TEXTURE_FORMAT_BC3 : ASSET_TEXTURE_FORMAT_BC1;

    U8* mipBlocks[ASSET_TEXTURE_MAX_MIPS] = {};
    U32 mipSizes[ASSET_TEXTURE_MAX_MIPS] = {};
    U32 mipCount = 0u;

    U32 mipWidth = (U32)width;
    U32 mipHeight = (U32)height;
    U8* mipPixels = rgba;
    for (;;) {
        mipSizes[mipCount] = cook_texture_mip_(arena, mipPixels, mipWidth, mipHeight, hasAlpha,
                                               &mipBlocks[mipCount]);
        mipCount += 1u;
        if ((mipWidth == 1u && mipHeight == 1u) || mipCount >= ASSET_TEXTURE_MAX_MIPS) {
            break;
        }

        U32 nextWidth = MAX(mipWidth / 2u, 1u);
        U32 nextHeight = MAX(mipHeight / 2u, 1u);
        U8* nextPixels = ARENA_PUSH_ARRAY(arena, U8, (U64)nextWidth * nextHeight * 4u);
        for (U32 y = 0u; y < nextHeight; ++y) {
            for (U32 x = 0u; x < nextWidth; ++x) {
                U32 sourceX0 = MIN(x * 2u, mipWidth - 1u);
                U32 sourceX1 = MIN(x * 2u + 1u, mipWidth - 1u);
                U32 sourceY0 = MIN(y * 2u, mipHeight - 1u);
                U32 sourceY1 = MIN(y * 2u + 1u, mipHeight - 1u);
                for (U32 channel = 0u; channel < 4u; ++channel) {
                    U32 sum = mipPixels[((U64)sourceY0 * mipWidth + sourceX0) * 4u + channel]
                            + mipPixels[((U64)sourceY0 * mipWidth + sourceX1) * 4u + channel]
                            + mipPixels[((U64)sourceY1 * mipWidth + sourceX0) * 4u + channel]
                            + mipPixels[((U64)sourceY1 * mipWidth + sourceX1) * 4u + channel];
                    nextPixels[((U64)y * nextWidth + x) * 4u + channel] = (U8)(sum / 4u);
                }
            }
        }
        mipPixels = nextPixels;
        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }
    stbi_image_free(rgba);

    textureHeader.mipCount = mipCount;
    U32 runningOffset = (U32)sizeof(AssetTextureHeader);
    for (U32 mipIndex = 0u; mipIndex < mipCount; ++mipIndex) {
        textureHeader.mipOffsets[mipIndex] = runningOffset;
        textureHeader.mipSizes[mipIndex] = mipSizes[mipIndex];
        runningOffset += mipSizes[mipIndex];
    }

    char* pathCStr = ARENA_PUSH_ARRAY(arena, char, outPath.size + 1);
    MEMCPY(pathCStr, outPath.data, outPath.size);
    pathCStr[outPath.size] = '\0';
    OS_Handle file = OS_file_open(pathCStr, OS_FileOpenMode_Create);
    if (!file.handle) {
        fprintf(stderr, "cooker: cannot open output texture\n");
        return 0;
    }
    OS_file_write(file, sizeof(textureHeader), &textureHeader);
    for (U32 mipIndex = 0u; mipIndex < mipCount; ++mipIndex) {
        OS_file_write(file, mipSizes[mipIndex], mipBlocks[mipIndex]);
    }
    OS_file_close(file);

    printf("cooker: texture %ux%u mips %u format %s\n",
           (U32)width, (U32)height, mipCount, hasAlpha ? "BC3" : "BC1");
    return 1;
}

// The whole default scene cooks into one UMDL plus one UTEX per unique
// baseColor image, named "<stem>_tex<N>.utex" — the runtime derives the
// texture paths from the model path by the same convention.
static B32 cook_model(Arena* arena, GlbFile* glb, StringU8 outputDir, StringU8 stem) {
    JsonValue* root = glb->json;
    CookModel* model = ARENA_PUSH_STRUCT(arena, CookModel);
    if (!model) {
        return 0;
    }
    MEMSET(model, 0, sizeof(*model));

    F32 identity[16];
    asset_mat_identity(identity);
    JsonValue* scene = json_index(json_get(root, "scenes"), json_u32(json_get(root, "scene"), 0u));
    JsonValue* roots = json_get(scene, "nodes");
    for (U32 rootAt = 0u;; ++rootAt) {
        JsonValue* rootValue = json_index(roots, rootAt);
        if (!rootValue) {
            break;
        }
        cook_model_walk_(model, root, json_u32(rootValue, 0xFFFFFFFFu), identity);
    }
    if (model->failed || model->instanceCount == 0u || model->sectionCount == 0u) {
        fprintf(stderr, "cooker: scene walk produced no instances\n");
        return 0;
    }

    CookedVertex* sectionVertices[COOK_MAX_SECTIONS];
    U32* sectionIndices[COOK_MAX_SECTIONS];
    U32 vertexTotal = 0u;
    U32 indexTotal = 0u;
    for (U32 section = 0u; section < model->sectionCount; ++section) {
        if (!cook_model_extract_section_(arena, glb, model, section,
                                         &sectionVertices[section], &vertexTotal,
                                         &sectionIndices[section], &indexTotal)) {
            return 0;
        }
    }

    // Model bounds: conservative box over every instance's transformed
    // section sphere.
    F32 modelMin[3] = {1e30f, 1e30f, 1e30f};
    F32 modelMax[3] = {-1e30f, -1e30f, -1e30f};
    for (U32 at = 0u; at < model->instanceCount; ++at) {
        const AssetModelInstance* instance = model->instances + at;
        const AssetModelSection* section = model->sections + instance->sectionIndex;
        F32 worldCenter[3];
        asset_mat_transform_point(instance->transform, section->boundsCenter, worldCenter);
        F32 maxScale = 0.0f;
        for (U32 row = 0u; row < 3u; ++row) {
            const F32* r = instance->transform + row * 4u;
            F32 lengthSq = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
            if (lengthSq > maxScale) {
                maxScale = lengthSq;
            }
        }
        maxScale = sqrtf(maxScale);
        F32 radius = sqrtf(section->boundsExtents[0] * section->boundsExtents[0] +
                           section->boundsExtents[1] * section->boundsExtents[1] +
                           section->boundsExtents[2] * section->boundsExtents[2]) * maxScale;
        for (U32 axis = 0u; axis < 3u; ++axis) {
            modelMin[axis] = MIN(modelMin[axis], worldCenter[axis] - radius);
            modelMax[axis] = MAX(modelMax[axis], worldCenter[axis] + radius);
        }
    }

    AssetModelHeader header = {};
    header.magic = ASSET_MODEL_MAGIC;
    header.version = ASSET_MODEL_VERSION;
    header.sectionCount = model->sectionCount;
    header.instanceCount = model->instanceCount;
    header.materialCount = model->materialCount;
    header.textureCount = model->textureCount;
    header.vertexCount = vertexTotal;
    header.indexCount = indexTotal;
    F32 halfDiagonal[3];
    for (U32 axis = 0u; axis < 3u; ++axis) {
        header.boundsCenter[axis] = (modelMin[axis] + modelMax[axis]) * 0.5f;
        halfDiagonal[axis] = (modelMax[axis] - modelMin[axis]) * 0.5f;
    }
    header.boundsRadius = sqrtf(halfDiagonal[0] * halfDiagonal[0] +
                                halfDiagonal[1] * halfDiagonal[1] +
                                halfDiagonal[2] * halfDiagonal[2]);

    StringU8 modelPath = str8_fmt(arena, "{}/{}.umdl", outputDir, stem);
    char* pathCStr = ARENA_PUSH_ARRAY(arena, char, modelPath.size + 1);
    MEMCPY(pathCStr, modelPath.data, modelPath.size);
    pathCStr[modelPath.size] = '\0';
    OS_Handle file = OS_file_open(pathCStr, OS_FileOpenMode_Create);
    if (!file.handle) {
        fprintf(stderr, "cooker: cannot open output model\n");
        return 0;
    }
    OS_file_write(file, sizeof(header), &header);
    OS_file_write(file, sizeof(AssetModelSection) * model->sectionCount, model->sections);
    OS_file_write(file, sizeof(AssetModelInstance) * model->instanceCount, model->instances);
    OS_file_write(file, sizeof(AssetModelMaterial) * model->materialCount, model->materials);
    for (U32 section = 0u; section < model->sectionCount; ++section) {
        U32 count = (section + 1u < model->sectionCount)
            ? model->sections[section + 1u].baseVertex - model->sections[section].baseVertex
            : vertexTotal - model->sections[section].baseVertex;
        OS_file_write(file, sizeof(CookedVertex) * count, sectionVertices[section]);
    }
    for (U32 section = 0u; section < model->sectionCount; ++section) {
        OS_file_write(file, sizeof(U32) * model->sections[section].indexCount, sectionIndices[section]);
    }
    OS_file_close(file);

    printf("cooker: model %u sections %u instances %u materials %u textures %u verts %u indices radius %.2f\n",
           model->sectionCount, model->instanceCount, model->materialCount, model->textureCount,
           vertexTotal, indexTotal, (F64)header.boundsRadius);

    for (U32 texture = 0u; texture < model->textureCount; ++texture) {
        StringU8 texturePath = asset_model_texture_path(arena, modelPath, texture);
        if (!cook_texture_image(arena, glb, model->textureImages[texture], texturePath)) {
            return 0;
        }
    }
    return 1;
}

// ////////////////////////
// Entry

static StringU8 cooker_stem_(StringU8 path) {
    U64 start = 0u;
    for (U64 at = 0u; at < path.size; ++at) {
        if (path.data[at] == '/' || path.data[at] == '\\') {
            start = at + 1u;
        }
    }
    U64 end = path.size;
    for (U64 at = start; at < path.size; ++at) {
        if (path.data[at] == '.') {
            end = at;
        }
    }
    StringU8 result = {};
    result.data = path.data + start;
    result.size = end - start;
    return result;
}

int main(int argc, char** argv) {
    OS_init();
    if (argc < 3) {
        fprintf(stderr, "usage: cooker <input.glb|input.wav> <output-dir>\n");
        return 1;
    }
    Arena* arena = arena_alloc();

    StringU8 inputPath = str8(argv[1]);
    StringU8 outputDir = str8(argv[2]);

    OS_Handle inputFile = OS_file_open(argv[1], OS_FileOpenMode_Read);
    if (!inputFile.handle) {
        fprintf(stderr, "cooker: cannot open '%s'\n", argv[1]);
        return 1;
    }
    U64 inputSize = OS_file_size(inputFile);
    U8* inputData = ARENA_PUSH_ARRAY(arena, U8, inputSize);
    RangeU64 inputRange = {0ull, inputSize};
    OS_file_read(inputFile, inputRange, inputData);
    OS_file_close(inputFile);

    StringU8 stem = cooker_stem_(inputPath);
    B32 isWav = inputPath.size > 4u &&
                MEMCMP(inputPath.data + inputPath.size - 4u, ".wav", 4) == 0;
    if (isWav) {
        if (!cook_audio(arena, inputData, inputSize, outputDir, stem)) {
            return 1;
        }
    } else {
        GlbFile glb = {};
        if (!glb_parse(arena, inputData, inputSize, &glb)) {
            fprintf(stderr, "cooker: '%s' is not a glTF binary\n", argv[1]);
            return 1;
        }
        if (!cook_model(arena, &glb, outputDir, stem)) {
            return 1;
        }
    }
    printf("cooker: cooked '%.*s'\n", (int)inputPath.size, (const char*)inputPath.data);
    return 0;
}
