#ifndef UNTITLED25_IDATASOURCE_H
#define UNTITLED25_IDATASOURCE_H

#include "BaseAllocator.h"
#include <cstddef>

// 数据源接口
class IDataSource {
protected:
    bool is_loaded = false;

public:
    virtual ~IDataSource() = default;

    // 获取数据缓冲区的指针
    virtual void* get_data() = 0;

    // 获取数据的大小（字节）
    virtual size_t get_size() = 0;

    // 执行实际的数据加载/准备工作（由子类实现）
    virtual void perform_load() = 0;

    // 公共加载接口，确保数据只被加载一次
    void load() {
        if (!is_loaded) {
            perform_load();
            is_loaded = true;
        }
    }
};

#endif //UNTITLED25_IDATASOURCE_H