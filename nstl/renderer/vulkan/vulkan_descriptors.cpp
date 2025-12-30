//
// Created by André Leite on 03/11/2025.
//

// ////////////////////////
// Descriptor Pool Creation

static B32 vulkan_create_descriptor_pool_(VkDevice device,
                                          const VkDescriptorType* types,
                                          const F32* ratios,
                                          U32 ratioCount,
                                          U32 maxSets,
                                          VkDescriptorPool* outPool) {
    if (!device || !outPool) {
        return 0;
    }

    Temp scratch = get_scratch(0, 0);
    DEFER_REF(temp_end(&scratch));

    VkDescriptorPoolSize* poolSizes = ARENA_PUSH_ARRAY(scratch.arena, VkDescriptorPoolSize, ratioCount);
    if (!poolSizes) {
        return 0;
    }

    for (U32 i = 0; i < ratioCount; ++i) {
        poolSizes[i].type = types[i];
        poolSizes[i].descriptorCount = (U32)(ratios[i] * (F32)maxSets);
        if (poolSizes[i].descriptorCount == 0) {
            poolSizes[i].descriptorCount = 1;
        }
    }

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = 0;
    poolInfo.maxSets = maxSets;
    poolInfo.poolSizeCount = ratioCount;
    poolInfo.pPoolSizes = poolSizes;

    VkResult result = vkCreateDescriptorPool(device, &poolInfo, 0, outPool);
    return (result == VK_SUCCESS) ? 1 : 0;
}

// ////////////////////////
// Descriptor Allocator

static B32 vulkan_descriptor_allocator_init(VulkanDevice* device,
                                            VulkanDescriptorAllocator* allocator,
                                            Arena* arena,
                                            U32 initialSetsPerPool,
                                            const VkDescriptorType* types,
                                            const F32* ratios,
                                            U32 ratioCount) {
    if (!device || !allocator || !arena) {
        return 0;
    }
    if (ratioCount > VULKAN_DESCRIPTOR_ALLOCATOR_MAX_RATIOS) {
        ratioCount = VULKAN_DESCRIPTOR_ALLOCATOR_MAX_RATIOS;
    }

    MEMSET(allocator, 0, sizeof(*allocator));
    allocator->arena = arena;
    allocator->setsPerPool = initialSetsPerPool;
    allocator->ratioCount = ratioCount;

    for (U32 i = 0; i < ratioCount; ++i) {
        allocator->types[i] = types[i];
        allocator->ratios[i] = ratios[i];
    }

    allocator->poolCapacity = 4;
    allocator->pools = ARENA_PUSH_ARRAY(arena, VkDescriptorPool, allocator->poolCapacity);
    if (!allocator->pools) {
        return 0;
    }

    VkDescriptorPool firstPool;
    if (!vulkan_create_descriptor_pool_(device->device, types, ratios, ratioCount, initialSetsPerPool, &firstPool)) {
        return 0;
    }

    allocator->pools[0] = firstPool;
    allocator->poolCount = 1;
    allocator->currentPoolIndex = 0;

    return 1;
}

static void vulkan_descriptor_allocator_destroy(VulkanDevice* device, VulkanDescriptorAllocator* allocator) {
    if (!device || !allocator) {
        return;
    }

    for (U32 i = 0; i < allocator->poolCount; ++i) {
        if (allocator->pools[i] != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device->device, allocator->pools[i], 0);
        }
    }

    allocator->poolCount = 0;
    allocator->currentPoolIndex = 0;
}

static void vulkan_descriptor_allocator_clear(VulkanDevice* device, VulkanDescriptorAllocator* allocator) {
    if (!device || !allocator) {
        return;
    }

    for (U32 i = 0; i < allocator->poolCount; ++i) {
        if (allocator->pools[i] != VK_NULL_HANDLE) {
            vkResetDescriptorPool(device->device, allocator->pools[i], 0);
        }
    }

    allocator->currentPoolIndex = 0;
}

static B32 vulkan_descriptor_allocator_grow_(VulkanDevice* device, VulkanDescriptorAllocator* allocator) {
    U32 newSetsPerPool = allocator->setsPerPool;
    if (newSetsPerPool < 4092) {
        newSetsPerPool = (U32)((F32)newSetsPerPool * 1.5f);
        if (newSetsPerPool > 4092) {
            newSetsPerPool = 4092;
        }
    }

    if (allocator->poolCount >= allocator->poolCapacity) {
        U32 newCapacity = allocator->poolCapacity * 2;
        VkDescriptorPool* newPools = ARENA_PUSH_ARRAY(allocator->arena, VkDescriptorPool, newCapacity);
        if (!newPools) {
            return 0;
        }
        MEMCPY(newPools, allocator->pools, sizeof(VkDescriptorPool) * allocator->poolCount);
        allocator->pools = newPools;
        allocator->poolCapacity = newCapacity;
    }

    VkDescriptorPool newPool;
    if (!vulkan_create_descriptor_pool_(device->device,
                                         allocator->types,
                                         allocator->ratios,
                                         allocator->ratioCount,
                                         newSetsPerPool,
                                         &newPool)) {
        return 0;
    }

    allocator->pools[allocator->poolCount] = newPool;
    allocator->currentPoolIndex = allocator->poolCount;
    allocator->poolCount++;
    allocator->setsPerPool = newSetsPerPool;

    return 1;
}

static B32 vulkan_descriptor_allocator_allocate(VulkanDevice* device,
                                                VulkanDescriptorAllocator* allocator,
                                                VkDescriptorSetLayout layout,
                                                VkDescriptorSet* outSet) {
    if (!device || !allocator || allocator->poolCount == 0) {
        return 0;
    }

    VkDescriptorPool poolToUse = allocator->pools[allocator->currentPoolIndex];

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = poolToUse;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkResult result = vkAllocateDescriptorSets(device->device, &allocInfo, outSet);

    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
        if (!vulkan_descriptor_allocator_grow_(device, allocator)) {
            return 0;
        }

        poolToUse = allocator->pools[allocator->currentPoolIndex];
        allocInfo.descriptorPool = poolToUse;
        result = vkAllocateDescriptorSets(device->device, &allocInfo, outSet);
    }

    return (result == VK_SUCCESS) ? 1 : 0;
}

// ////////////////////////
// Descriptor Writer

struct VulkanDescriptorWriter {
    VkWriteDescriptorSet writes[16];
    VkDescriptorImageInfo imageInfos[16];
    VkDescriptorBufferInfo bufferInfos[16];
    U32 writeCount;
    U32 imageInfoCount;
    U32 bufferInfoCount;
};

static void vulkan_descriptor_writer_clear(VulkanDescriptorWriter* writer) {
    if (!writer) {
        return;
    }
    writer->writeCount = 0;
    writer->imageInfoCount = 0;
    writer->bufferInfoCount = 0;
}

static void vulkan_descriptor_writer_write_image(VulkanDescriptorWriter* writer,
                                                 U32 binding,
                                                 VkImageView imageView,
                                                 VkSampler sampler,
                                                 VkImageLayout layout,
                                                 VkDescriptorType type) {
    if (!writer || writer->writeCount >= ARRAY_COUNT(writer->writes)) {
        return;
    }
    if (writer->imageInfoCount >= ARRAY_COUNT(writer->imageInfos)) {
        return;
    }

    U32 infoIdx = writer->imageInfoCount++;
    writer->imageInfos[infoIdx].sampler = sampler;
    writer->imageInfos[infoIdx].imageView = imageView;
    writer->imageInfos[infoIdx].imageLayout = layout;

    U32 writeIdx = writer->writeCount++;
    VkWriteDescriptorSet* write = &writer->writes[writeIdx];
    write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write->pNext = 0;
    write->dstBinding = binding;
    write->dstSet = VK_NULL_HANDLE;
    write->dstArrayElement = 0;
    write->descriptorCount = 1;
    write->descriptorType = type;
    write->pImageInfo = &writer->imageInfos[infoIdx];
    write->pBufferInfo = 0;
    write->pTexelBufferView = 0;
}

static void vulkan_descriptor_writer_write_buffer(VulkanDescriptorWriter* writer,
                                                  U32 binding,
                                                  VkBuffer buffer,
                                                  U64 size,
                                                  U64 offset,
                                                  VkDescriptorType type) {
    if (!writer || writer->writeCount >= ARRAY_COUNT(writer->writes)) {
        return;
    }
    if (writer->bufferInfoCount >= ARRAY_COUNT(writer->bufferInfos)) {
        return;
    }

    U32 infoIdx = writer->bufferInfoCount++;
    writer->bufferInfos[infoIdx].buffer = buffer;
    writer->bufferInfos[infoIdx].offset = offset;
    writer->bufferInfos[infoIdx].range = size;

    U32 writeIdx = writer->writeCount++;
    VkWriteDescriptorSet* write = &writer->writes[writeIdx];
    write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write->pNext = 0;
    write->dstBinding = binding;
    write->dstSet = VK_NULL_HANDLE;
    write->dstArrayElement = 0;
    write->descriptorCount = 1;
    write->descriptorType = type;
    write->pImageInfo = 0;
    write->pBufferInfo = &writer->bufferInfos[infoIdx];
    write->pTexelBufferView = 0;
}

static void vulkan_descriptor_writer_update_set(VulkanDevice* device,
                                                VulkanDescriptorWriter* writer,
                                                VkDescriptorSet set) {
    if (!device || !writer || writer->writeCount == 0) {
        return;
    }

    for (U32 i = 0; i < writer->writeCount; ++i) {
        writer->writes[i].dstSet = set;
    }

    vkUpdateDescriptorSets(device->device, writer->writeCount, writer->writes, 0, 0);
}

