//
// Created by lenovo on 2025/8/4.
//

#ifndef UNTITLED25_STANDARDALLOCATOR_H
#define UNTITLED25_STANDARDALLOCATOR_H
// standard_allocator.h

#include "BaseAllocator.h"
#include <cstdlib>

class StandardAllocator : public BaseAllocator {
public:
    void* allocate(size_t size, size_t alignment = 0) override {
        if (alignment > 0) {
            // C++17 aligned_alloc
            // return aligned_alloc(alignment, size);
            // 或者使用 posix_memalign for compatibility
            void* ptr;
            if (posix_memalign(&ptr, alignment, size) != 0) {
                return nullptr;
            }
            return ptr;
        }
        return malloc(size);
    }

    void deallocate(void* ptr) override {
        free(ptr);
    }

    const char* get_name() const override {
        return "StandardMalloc";
    }
};
#endif //UNTITLED25_STANDARDALLOCATOR_H
