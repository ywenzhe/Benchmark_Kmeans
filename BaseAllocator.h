//
// Created by lenovo on 2025/8/4.
//

#ifndef UNTITLED25_BASEALLOCATOR_H
#define UNTITLED25_BASEALLOCATOR_H
#include <cstddef>
#include <iostream>
// 抽象分配器基类
class BaseAllocator {
public:
    virtual ~BaseAllocator() {}

    // 分配内存
    virtual void* allocate(size_t size, size_t alignment = 0) = 0;

    // 释放内存
    virtual void deallocate(void* ptr) = 0;

    // 初始化（例如，用于共享内存的设置）
    virtual void init() {};

    // 关闭（例如，用于共享内存的清理）
    virtual void shutdown() {};

    // 获取分配器名称
    virtual const char* get_name() const = 0;
};
#endif //UNTITLED25_BASEALLOCATOR_H
