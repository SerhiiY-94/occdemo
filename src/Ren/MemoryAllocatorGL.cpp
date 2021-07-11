#include "MemoryAllocator.h"

void Ren::MemAllocation::Release() {}

Ren::MemoryAllocator::MemoryAllocator(const char name[32], ApiContext *api_ctx, const uint32_t initial_block_size,
                                      uint32_t mem_type_index, const float growth_factor)
    : api_ctx_(api_ctx), growth_factor_(growth_factor), mem_type_index_(mem_type_index) {
    strcpy(name_, name);

    assert(growth_factor_ > 1.0f);
    AllocateNewBlock(initial_block_size);
}

Ren::MemoryAllocator::~MemoryAllocator() = default;

bool Ren::MemoryAllocator::AllocateNewBlock(const uint32_t size) { return true; }

Ren::MemAllocation Ren::MemoryAllocator::Allocate(const uint32_t size, const uint32_t alignment, const char *tag) {
    return {};
}

void Ren::MemoryAllocator::Free(uint32_t block_ndx, uint32_t alloc_off) {
    assert(block_ndx < blocks_.size());
    const int node_ndx = blocks_[block_ndx].alloc.Find_r(0, alloc_off);
    assert(node_ndx != -1);
    blocks_[block_ndx].alloc.Free_Node(node_ndx);
}

void Ren::MemoryAllocators::Print(ILog *log) {}