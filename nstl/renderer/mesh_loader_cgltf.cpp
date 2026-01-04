//
// Created by André Leite on 29/12/2025.
//

#define CGLTF_IMPLEMENTATION

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#include "cgltf.h"
#pragma clang diagnostic pop

// ////////////////////////
// Helper Functions

static void compute_bounds_from_vertices(const Vertex* vertices, U32 startVtx, U32 count, Bounds* outBounds) {
    if (count == 0) {
        *outBounds = {};
        return;
    }
    
    Vec3F32 minPos = vertices[startVtx].position;
    Vec3F32 maxPos = vertices[startVtx].position;
    
    for (U32 i = startVtx; i < startVtx + count; ++i) {
        Vec3F32 p = vertices[i].position;
        if (p.x < minPos.x) { minPos.x = p.x; }
        if (p.y < minPos.y) { minPos.y = p.y; }
        if (p.z < minPos.z) { minPos.z = p.z; }
        if (p.x > maxPos.x) { maxPos.x = p.x; }
        if (p.y > maxPos.y) { maxPos.y = p.y; }
        if (p.z > maxPos.z) { maxPos.z = p.z; }
    }
    
    outBounds->origin.x = (minPos.x + maxPos.x) * 0.5f;
    outBounds->origin.y = (minPos.y + maxPos.y) * 0.5f;
    outBounds->origin.z = (minPos.z + maxPos.z) * 0.5f;
    outBounds->extents.x = (maxPos.x - minPos.x) * 0.5f;
    outBounds->extents.y = (maxPos.y - minPos.y) * 0.5f;
    outBounds->extents.z = (maxPos.z - minPos.z) * 0.5f;
    
    F32 ex = outBounds->extents.x;
    F32 ey = outBounds->extents.y;
    F32 ez = outBounds->extents.z;
    outBounds->sphereRadius = SQRT_F32(ex * ex + ey * ey + ez * ez);
}

static void load_mesh_primitives(Arena* arena, cgltf_mesh* mesh, MeshAssetData* asset) {
    if (mesh->primitives_count == 0) {
        return;
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
        return;
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
        U32 surfaceStartVtx = vertexOffset;
        
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
            } else {
                v->uvX = v->position.x * 0.5f; 
                v->uvY = v->position.y * 0.5f;
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
        asset->surfaces[p].materialIndex = 0;

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
        
        compute_bounds_from_vertices(asset->data.vertices, surfaceStartVtx, primVertexCount, &asset->surfaces[p].bounds);

        vertexOffset += primVertexCount;
        indexOffset += primIndexCount;
    }

    asset->data.vertexCount = vertexOffset;
    asset->data.indexCount = indexOffset;
}

B32 scene_load_from_file(Arena* arena, const char* path, LoadedScene* outScene) {
    if (!arena || !path || !outScene) {
        return 0;
    }

    MEMSET(outScene, 0, sizeof(LoadedScene));

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

    outScene->materialCount = (U32)data->materials_count;
    if (outScene->materialCount > 0) {
        outScene->materials = ARENA_PUSH_ARRAY(arena, MaterialData, outScene->materialCount);
        for (U32 i = 0; i < outScene->materialCount; ++i) {
            cgltf_material* mat = &data->materials[i];
            MaterialData* md = &outScene->materials[i];
            
            md->colorFactor.r = mat->pbr_metallic_roughness.base_color_factor[0];
            md->colorFactor.g = mat->pbr_metallic_roughness.base_color_factor[1];
            md->colorFactor.b = mat->pbr_metallic_roughness.base_color_factor[2];
            md->colorFactor.a = mat->pbr_metallic_roughness.base_color_factor[3];
            md->metallicFactor = mat->pbr_metallic_roughness.metallic_factor;
            md->roughnessFactor = mat->pbr_metallic_roughness.roughness_factor;
            
            if (mat->pbr_metallic_roughness.base_color_texture.texture) {
                cgltf_texture* tex = mat->pbr_metallic_roughness.base_color_texture.texture;
                if (tex->image) {
                    md->colorTextureIndex = (U32)(tex->image - data->images);
                } else {
                    md->colorTextureIndex = MATERIAL_NO_TEXTURE;
                }
                if (tex->sampler) {
                    md->samplerIndex = (U32)(tex->sampler - data->samplers);
                } else {
                    md->samplerIndex = 0;
                }
            } else {
                md->colorTextureIndex = MATERIAL_NO_TEXTURE;
                md->samplerIndex = 0;
            }
            
            if (mat->pbr_metallic_roughness.metallic_roughness_texture.texture) {
                cgltf_texture* tex = mat->pbr_metallic_roughness.metallic_roughness_texture.texture;
                if (tex->image) {
                    md->metalRoughTextureIndex = (U32)(tex->image - data->images);
                } else {
                    md->metalRoughTextureIndex = MATERIAL_NO_TEXTURE;
                }
            } else {
                md->metalRoughTextureIndex = MATERIAL_NO_TEXTURE;
            }
        }
    }

    outScene->imageCount = (U32)data->images_count;
    if (outScene->imageCount > 0) {
        outScene->images = ARENA_PUSH_ARRAY(arena, LoadedImage, outScene->imageCount);
        MEMSET(outScene->images, 0, sizeof(LoadedImage) * outScene->imageCount);
        
        for (U32 i = 0; i < outScene->imageCount; ++i) {
            cgltf_image* img = &data->images[i];
            LoadedImage* loaded = &outScene->images[i];
            
            B32 success = 0;
            
            if (img->buffer_view) {
                cgltf_buffer_view* bv = img->buffer_view;
                U8* bufferData = (U8*)bv->buffer->data + bv->offset;
                success = image_load_from_memory(bufferData, bv->size, loaded);
            } else if (img->uri && img->uri[0] != '\0') {
                if (C_STR_LEN(img->uri) > 5 && 
                    img->uri[0] == 'd' && img->uri[1] == 'a' && 
                    img->uri[2] == 't' && img->uri[3] == 'a' && img->uri[4] == ':') {
                    LOG_WARNING("mesh_loader", "Base64 image data not yet supported");
                } else {
                    char fullPath[512];
                    const char* lastSlash = path;
                    for (const char* p = path; *p; ++p) {
                        if (*p == '/' || *p == '\\') {
                            lastSlash = p + 1;
                        }
                    }
                    U64 dirLen = (U64)(lastSlash - path);
                    if (dirLen + C_STR_LEN(img->uri) + 1 < sizeof(fullPath)) {
                        MEMMOVE(fullPath, path, dirLen);
                        MEMMOVE(fullPath + dirLen, img->uri, C_STR_LEN(img->uri) + 1);
                        success = image_load_from_file(fullPath, loaded);
                    }
                }
            }
            
            if (!success) {
                LOG_WARNING("mesh_loader", "Failed to load image {} from scene", i);
            }
        }
    }

    outScene->meshCount = (U32)data->meshes_count;
    if (outScene->meshCount > 0) {
        outScene->meshes = ARENA_PUSH_ARRAY(arena, MeshAssetData, outScene->meshCount);
        MEMSET(outScene->meshes, 0, sizeof(MeshAssetData) * outScene->meshCount);
        
        for (U32 m = 0; m < outScene->meshCount; ++m) {
            cgltf_mesh* mesh = &data->meshes[m];
            MeshAssetData* asset = &outScene->meshes[m];

            if (mesh->name) {
                U64 nameLen = C_STR_LEN(mesh->name);
                char* nameCopy = ARENA_PUSH_ARRAY(arena, char, nameLen + 1);
                if (nameCopy) {
                    MEMMOVE(nameCopy, mesh->name, nameLen);
                    nameCopy[nameLen] = 0;
                    asset->data.name = str8(nameCopy);
                }
            }

            load_mesh_primitives(arena, mesh, asset);
            
            for (U32 p = 0; p < asset->surfaceCount; ++p) {
                cgltf_primitive* prim = &mesh->primitives[p];
                if (prim->material) {
                    asset->surfaces[p].materialIndex = (U32)(prim->material - data->materials);
                } else {
                    asset->surfaces[p].materialIndex = 0;
                }
            }
        }
    }

    outScene->nodeCount = (U32)data->nodes_count;
    if (outScene->nodeCount > 0) {
        outScene->nodes = ARENA_PUSH_ARRAY(arena, SceneNode, outScene->nodeCount);
        MEMSET(outScene->nodes, 0, sizeof(SceneNode) * outScene->nodeCount);
        
        for (U32 n = 0; n < outScene->nodeCount; ++n) {
            cgltf_node* node = &data->nodes[n];
            SceneNode* sn = &outScene->nodes[n];
            
            if (node->name) {
                U64 nameLen = C_STR_LEN(node->name);
                char* nameCopy = ARENA_PUSH_ARRAY(arena, char, nameLen + 1);
                if (nameCopy) {
                    MEMMOVE(nameCopy, node->name, nameLen);
                    nameCopy[nameLen] = 0;
                    sn->name = str8(nameCopy);
                }
            }
            
            sn->parentIndex = -1;
            sn->meshIndex = node->mesh ? (S32)(node->mesh - data->meshes) : -1;
            
            if (node->has_matrix) {
                for (U32 i = 0; i < 4; ++i) {
                    for (U32 j = 0; j < 4; ++j) {
                        sn->localTransform.v[i][j] = node->matrix[i * 4 + j];
                    }
                }
            } else {
                Vec3F32 t = {{node->translation[0], node->translation[1], node->translation[2]}};
                QuatF32 r = {{node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]}};
                Vec3F32 s = {{node->scale[0], node->scale[1], node->scale[2]}};
                
                Mat4x4F32 tm = mat4_identity();
                tm.v[3][0] = t.x;
                tm.v[3][1] = t.y;
                tm.v[3][2] = t.z;
                
                Mat4x4F32 rm = quat_to_mat4(r);
                
                Mat4x4F32 sm = mat4_identity();
                sm.v[0][0] = s.x;
                sm.v[1][1] = s.y;
                sm.v[2][2] = s.z;
                
                sn->localTransform = tm * rm * sm;
            }
            
            sn->worldTransform = sn->localTransform;
            
            if (node->children_count > 0) {
                sn->childCount = (U32)node->children_count;
                sn->childIndices = ARENA_PUSH_ARRAY(arena, U32, sn->childCount);
                for (U32 c = 0; c < sn->childCount; ++c) {
                    sn->childIndices[c] = (U32)(node->children[c] - data->nodes);
                }
            }
        }
        
        for (U32 n = 0; n < outScene->nodeCount; ++n) {
            SceneNode* sn = &outScene->nodes[n];
            for (U32 c = 0; c < sn->childCount; ++c) {
                U32 childIdx = sn->childIndices[c];
                if (childIdx < outScene->nodeCount) {
                    outScene->nodes[childIdx].parentIndex = (S32)n;
                }
            }
        }
        
        U32 topNodeCount = 0;
        for (U32 n = 0; n < outScene->nodeCount; ++n) {
            if (outScene->nodes[n].parentIndex < 0) {
                topNodeCount++;
            }
        }
        
        outScene->topNodeCount = topNodeCount;
        outScene->topNodeIndices = ARENA_PUSH_ARRAY(arena, U32, topNodeCount);
        U32 topIdx = 0;
        for (U32 n = 0; n < outScene->nodeCount; ++n) {
            if (outScene->nodes[n].parentIndex < 0) {
                outScene->topNodeIndices[topIdx++] = n;
            }
        }
        
        for (U32 n = 0; n < outScene->nodeCount; ++n) {
            SceneNode* sn = &outScene->nodes[n];
            if (sn->parentIndex >= 0) {
                SceneNode* parent = &outScene->nodes[sn->parentIndex];
                sn->worldTransform = parent->worldTransform * sn->localTransform;
            }
        }
    }

    cgltf_free(data);

    LOG_INFO("mesh_loader", "Loaded scene from '{}': {} meshes, {} materials, {} nodes",
             path, outScene->meshCount, outScene->materialCount, outScene->nodeCount);
    return 1;
}

