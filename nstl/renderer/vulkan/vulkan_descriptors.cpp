//
// Created by André Leite on 03/11/2025.
//

static B32 vulkan_create_descriptor_pool(VkDevice device,
                                         const VulkanDescriptorAllocator::PoolSizeRatio* poolRatios,
                                         U32 ratioCount,
                                         U32 maxSets,
                                         VkDescriptorPool* outPool) {
    if (!device || !outPool) return 0;

    Temp scratch = get_scratch(0, 0);
    DEFER_REF(temp_end(&scratch));

    VkDescriptorPoolSize* poolSizes = ARENA_PUSH_ARRAY(scratch.arena, VkDescriptorPoolSize, ratioCount);
    if (!poolSizes) return 0;

    for (U32 i = 0; i < ratioCount; ++i) {
        poolSizes[i].type = poolRatios[i].type;
        poolSizes[i].descriptorCount = (U32)(poolRatios[i].ratio * maxSets);
    }

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = maxSets;
    poolInfo.poolSizeCount = ratioCount;
    poolInfo.pPoolSizes = poolSizes;

    VkResult result = vkCreateDescriptorPool(device, &poolInfo, 0, outPool);
    
    if (result != VK_SUCCESS) {
        return 0;
    }
    return 1;
}

static B32 vulkan_descriptor_allocator_init(VulkanDevice* device, VulkanDescriptorAllocator* allocator,
                                            U32 initialMaxSets,
                                            const VulkanDescriptorAllocator::PoolSizeRatio* poolRatios,
                                            U32 ratioCount) {
    if (!device || !allocator) return 0;

    allocator->currentPool = VK_NULL_HANDLE;
    // We could store ratios to create new pools if needed
    // For now simplified: just create one big pool or fail
    
    return vulkan_create_descriptor_pool(device->device, poolRatios, ratioCount, initialMaxSets, &allocator->currentPool);
}

static void vulkan_descriptor_allocator_destroy(VulkanDevice* device, VulkanDescriptorAllocator* allocator) {
    if (!device || !allocator) return;
    
    if (allocator->currentPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device->device, allocator->currentPool, 0);
        allocator->currentPool = VK_NULL_HANDLE;
    }
}

static B32 vulkan_descriptor_allocator_allocate(VulkanDevice* device, VulkanDescriptorAllocator* allocator,
                                                VkDescriptorSetLayout layout, VkDescriptorSet* outSet) {
    if (!device || !allocator || allocator->currentPool == VK_NULL_HANDLE) return 0;

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.pNext = 0;
    allocInfo.descriptorPool = allocator->currentPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkResult result = vkAllocateDescriptorSets(device->device, &allocInfo, outSet);
    
    // If out of pool memory, we should create a new pool.
    // For this simplified implementation, we just fail.
    // To implement dynamic growth:
    // 1. Check error.
    // 2. If out of memory, create new pool, add old pool to fullPools list.
    // 3. Retry.
    
    if (result != VK_SUCCESS) {
        return 0;
    }

    return 1;
}

static void vulkan_descriptor_allocator_reset(VulkanDevice* device, VulkanDescriptorAllocator* allocator) {
     if (!device || !allocator || allocator->currentPool == VK_NULL_HANDLE) return;
     vkResetDescriptorPool(device->device, allocator->currentPool, 0);
}

// ////////////////////////
// Descriptor Writer Helper

struct VulkanDescriptorWriter {
    // Max 16 writes per batch for simple usage
    VkWriteDescriptorSet writes[16];
    VkDescriptorImageInfo imageInfos[16];
    VkDescriptorBufferInfo bufferInfos[16];
    U32 writeCount;
};

static void vulkan_descriptor_writer_write_image(VulkanDescriptorWriter* writer,
                                                 U32 binding,
                                                 VkImageView imageView,
                                                 VkSampler sampler,
                                                 VkImageLayout layout,
                                                 VkDescriptorType type) {
    if (writer->writeCount >= ARRAY_COUNT(writer->writes)) return;

    U32 idx = writer->writeCount;
    
    writer->imageInfos[idx].sampler = sampler;
    writer->imageInfos[idx].imageView = imageView;
    writer->imageInfos[idx].imageLayout = layout;

    VkWriteDescriptorSet* write = &writer->writes[idx];
    write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write->pNext = 0;
    write->dstBinding = binding;
    write->dstSet = VK_NULL_HANDLE; // Filled in update
    write->descriptorCount = 1;
    write->descriptorType = type;
    write->pImageInfo = &writer->imageInfos[idx];

    writer->writeCount++;
}

static void vulkan_descriptor_writer_update_set(VulkanDevice* device, VulkanDescriptorWriter* writer, VkDescriptorSet set) {
    if (!device || !writer) return;

    for (U32 i = 0; i < writer->writeCount; ++i) {
        writer->writes[i].dstSet = set;
    }

    vkUpdateDescriptorSets(device->device, writer->writeCount, writer->writes, 0, 0);
}

