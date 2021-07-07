#include "MemoryAllocator.h"

namespace Ren {
uint32_t FindMemoryType(const VkPhysicalDeviceMemoryProperties *mem_properties, uint32_t mem_type_bits,
                        VkMemoryPropertyFlags desired_mem_flags) {
    for (uint32_t i = 0; i < 32; i++) {
        const VkMemoryType mem_type = mem_properties->memoryTypes[i];
        if (mem_type_bits & 1u) {
            if ((mem_type.propertyFlags & desired_mem_flags) == desired_mem_flags) {
                return i;
            }
        }
        mem_type_bits = (mem_type_bits >> 1u);
    }
    return 0xffffffff;
}
} // namespace Ren

void Ren::MemAllocation::Release() {
    if (owner) {
        owner->Free(block_ndx, alloc_off);
        owner = nullptr;
    }
}

Ren::MemoryAllocator::MemoryAllocator(const char name[32], ApiContext *api_ctx, const uint32_t initial_block_size,
                                      uint32_t mem_type_index, const float growth_factor)
    : api_ctx_(api_ctx), growth_factor_(growth_factor), mem_type_index_(mem_type_index) {
    strcpy(name_, name);

    assert(growth_factor_ > 1.0f);
    AllocateNewBlock(initial_block_size);
}

Ren::MemoryAllocator::~MemoryAllocator() {
    for (MemBlock &blk : blocks_) {
        vkFreeMemory(api_ctx_->device, blk.mem, nullptr);
    }
}

bool Ren::MemoryAllocator::AllocateNewBlock(const uint32_t size) {
    char buf_name[48];
    sprintf(buf_name, "%s block %i", name_, int(blocks_.size()));

    blocks_.emplace_back();
    MemBlock &new_block = blocks_.back();

    new_block.alloc = LinearAlloc{size};

    VkMemoryAllocateInfo buf_alloc_info = {};
    buf_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    buf_alloc_info.allocationSize = VkDeviceSize(size);
    buf_alloc_info.memoryTypeIndex = mem_type_index_;

    const VkResult res = vkAllocateMemory(api_ctx_->device, &buf_alloc_info, nullptr, &new_block.mem);
    return res == VK_SUCCESS;
}

Ren::MemAllocation Ren::MemoryAllocator::Allocate(const uint32_t size, const uint32_t alignment, const char *tag) {
    while (true) {
        for (uint32_t i = 0; i < uint32_t(blocks_.size()); ++i) {
            if (size > blocks_[i].alloc.size()) {
                // can skip entire buffer
                continue;
            }

            const int node_ndx = blocks_[i].alloc.Alloc_r(0, size + alignment, tag);
            if (node_ndx != -1) {
                // allocation succeded
                MemAllocation new_alloc = {};
                new_alloc.block_ndx = i;
                new_alloc.alloc_off = blocks_[i].alloc.node_off(node_ndx);
                new_alloc.owner = this;
                return new_alloc;
            }
        }

        // allocation failed, add new buffer
        do {
            AllocateNewBlock(uint32_t(blocks_.back().alloc.size() * growth_factor_));
        } while (blocks_.back().alloc.size() < size);
    }

    return {};
}

void Ren::MemoryAllocator::Free(uint32_t block_ndx, uint32_t alloc_off) {
    assert(block_ndx < blocks_.size());
    const int node_ndx = blocks_[block_ndx].alloc.Find_r(0, alloc_off);
    assert(node_ndx != -1);
    blocks_[block_ndx].alloc.Free_Node(node_ndx);
}