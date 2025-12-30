//
// Created by André Leite on 29/12/2025.
//

#define CGLTF_IMPLEMENTATION

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#include "cgltf.h"
#pragma clang diagnostic pop

U32 mesh_load_from_file(Arena* arena, const char* path, MeshAssetData** outMeshes) {
    if (!arena || !path || !outMeshes) {
        return 0;
    }

    *outMeshes = 0;

    cgltf_options options = {};
    cgltf_data* data = 0;
    cgltf_result result = cgltf_parse_file(&options, path, &data);
    if (result != cgltf_result_success) {
        LOG_ERROR("mesh_loader", "Failed to parse glTF file: {}", path);
        return 0;
    }

    result = cgltf_load_buffers(&options, data, path);
    if (result != cgltf_result_success) {
        LOG_ERROR("mesh_loader", "Failed to load glTF buffers: {}", path);
        cgltf_free(data);
        return 0;
    }

    if (data->meshes_count == 0) {
        LOG_ERROR("mesh_loader", "glTF file has no meshes: {}", path);
        cgltf_free(data);
        return 0;
    }

    U32 meshCount = (U32)data->meshes_count;
    MeshAssetData* meshes = ARENA_PUSH_ARRAY(arena, MeshAssetData, meshCount);
    if (!meshes) {
        cgltf_free(data);
        return 0;
    }
    MEMSET(meshes, 0, sizeof(MeshAssetData) * meshCount);

    for (U32 m = 0; m < meshCount; ++m) {
        cgltf_mesh* mesh = &data->meshes[m];
        MeshAssetData* asset = &meshes[m];

        if (mesh->name) {
            U64 nameLen = C_STR_LEN(mesh->name);
            char* nameCopy = ARENA_PUSH_ARRAY(arena, char, nameLen + 1);
            if (nameCopy) {
                MEMMOVE(nameCopy, mesh->name, nameLen);
                nameCopy[nameLen] = 0;
                asset->data.name = str8(nameCopy);
            }
        }

        if (mesh->primitives_count == 0) {
            continue;
        }

        asset->surfaces = ARENA_PUSH_ARRAY(arena, MeshSurface, mesh->primitives_count);
        asset->surfaceCount = (U32)mesh->primitives_count;

        U32 totalVertices = 0;
        U32 totalIndices = 0;

        for (U64 p = 0; p < mesh->primitives_count; ++p) {
            cgltf_primitive* prim = &mesh->primitives[p];
            U32 primVertexCount = 0;
            for (U64 a = 0; a < prim->attributes_count; ++a) {
                if (prim->attributes[a].type == cgltf_attribute_type_position) {
                    primVertexCount = (U32)prim->attributes[a].data->count;
                    break;
                }
            }
            totalVertices += primVertexCount;
            if (prim->indices) {
                totalIndices += (U32)prim->indices->count;
            } else {
                totalIndices += primVertexCount;
            }
        }

        asset->data.vertices = ARENA_PUSH_ARRAY(arena, Vertex, totalVertices);
        asset->data.indices = ARENA_PUSH_ARRAY(arena, U32, totalIndices);
        if (!asset->data.vertices || !asset->data.indices) {
            continue;
        }

        U32 vertexOffset = 0;
        U32 indexOffset = 0;

        for (U64 p = 0; p < mesh->primitives_count; ++p) {
            cgltf_primitive* prim = &mesh->primitives[p];

            cgltf_accessor* posAccessor = 0;
            cgltf_accessor* normAccessor = 0;
            cgltf_accessor* uvAccessor = 0;
            cgltf_accessor* colorAccessor = 0;

            for (U64 a = 0; a < prim->attributes_count; ++a) {
                cgltf_attribute* attr = &prim->attributes[a];
                if (attr->type == cgltf_attribute_type_position) {
                    posAccessor = attr->data;
                } else if (attr->type == cgltf_attribute_type_normal) {
                    normAccessor = attr->data;
                } else if (attr->type == cgltf_attribute_type_texcoord) {
                    uvAccessor = attr->data;
                } else if (attr->type == cgltf_attribute_type_color) {
                    colorAccessor = attr->data;
                }
            }

            if (!posAccessor) {
                continue;
            }

            U32 primVertexCount = (U32)posAccessor->count;
            for (U32 i = 0; i < primVertexCount; ++i) {
                Vertex* v = &asset->data.vertices[vertexOffset + i];
                MEMSET(v, 0, sizeof(Vertex));

                cgltf_accessor_read_float(posAccessor, i, &v->position.x, 3);
                if (normAccessor) {
                    cgltf_accessor_read_float(normAccessor, i, &v->normal.x, 3);
                }
                if (uvAccessor) {
                    F32 uv[2] = {0};
                    cgltf_accessor_read_float(uvAccessor, i, uv, 2);
                    v->uvX = uv[0];
                    v->uvY = uv[1];
                }
                if (colorAccessor) {
                    cgltf_accessor_read_float(colorAccessor, i, &v->color.r, 4);
                } else {
                    v->color.r = 1.0f;
                    v->color.g = 1.0f;
                    v->color.b = 1.0f;
                    v->color.a = 1.0f;
                }
            }

            asset->surfaces[p].startIndex = indexOffset;

            U32 primIndexCount = 0;
            if (prim->indices) {
                primIndexCount = (U32)prim->indices->count;
                for (U32 i = 0; i < primIndexCount; ++i) {
                    asset->data.indices[indexOffset + i] = vertexOffset + (U32)cgltf_accessor_read_index(prim->indices, i);
                }
            } else {
                primIndexCount = primVertexCount;
                for (U32 i = 0; i < primIndexCount; ++i) {
                    asset->data.indices[indexOffset + i] = vertexOffset + i;
                }
            }

            asset->surfaces[p].count = primIndexCount;

            vertexOffset += primVertexCount;
            indexOffset += primIndexCount;
        }

        asset->data.vertexCount = vertexOffset;
        asset->data.indexCount = indexOffset;
    }

    cgltf_free(data);

    *outMeshes = meshes;
    LOG_INFO("mesh_loader", "Loaded {} meshes from '{}'", meshCount, path);
    return meshCount;
}
