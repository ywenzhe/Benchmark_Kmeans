//
// Created by lenovo on 2025/8/4.
//

#ifndef UNTITLED25_MAPREDUCE_H
#define UNTITLED25_MAPREDUCE_H


#include <list>
#include <vector>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <pthread.h>
#include <algorithm> // 为了使用 std::max

#include "BaseAllocator.h"

// 线程同步屏障
extern pthread_barrier_t barrier_map, barrier_reduce;

// Map任务参数
struct map_parameter {
    void* map_data;
    size_t length;
    int task_id;
};

// Reduce任务参数
struct reduce_parameter {
    int task_id;
};

// 中间数据块结构
struct imm_data {
    void* data;
    int count;
};

const static int imm_data_size = 512; // 每个中间数据块的默认大小

class MapReduce {
protected:
    BaseAllocator* allocator;

public:
    int map_num;
    int reduce_num;
    std::vector<std::list<imm_data>*>* vec;

    inline int get_vec_index(int map_id, int reduce_id) {
        return map_id * reduce_num + reduce_id;
    }

    MapReduce(int map_num_, int reduce_num_, BaseAllocator* alloc)
            : map_num(map_num_), reduce_num(reduce_num_), allocator(alloc) {
        if (!allocator) {
            throw std::runtime_error("Allocator cannot be null.");
        }
        vec = new std::vector<std::list<imm_data>*>();
        for (int i = 0; i < map_num * reduce_num; i++) {
            vec->push_back(new std::list<imm_data>());
        }
        pthread_barrier_init(&barrier_map, NULL, map_num + reduce_num);
        pthread_barrier_init(&barrier_reduce, NULL, reduce_num);
    }

    virtual ~MapReduce() {
        // 析构函数可以重用重置逻辑来清理内存
        reset_intermediate_data();
        for (auto& list_ptr : *vec) {
            delete list_ptr;
        }
        delete vec;
        pthread_barrier_destroy(&barrier_map);
        pthread_barrier_destroy(&barrier_reduce);
    }

    // [新增功能] 重置中间数据以进行下一次迭代
    // 这是实现多次迭代所必需的核心修改
    void reset_intermediate_data() {
        for (auto& list_ptr : *vec) {
            // 释放由分配器分配的每个数据块的内存
            for(auto const& data_block : *list_ptr) {
                allocator->deallocate(data_block.data);
            }
            // 清空列表，为下一次迭代做准备
            list_ptr->clear();
        }
    }

    virtual void map_func(void* map_data, int task_id, size_t data_length) = 0;
    virtual void reduce_func(int task_id) = 0;
    virtual void splice(char** data_arr, size_t* data_dis, char* map_data, size_t data_length) = 0;


    void emit_intermediate(std::list<imm_data>* inter, char* data, int len) {
        if (!inter->empty() && (inter->back().count + len <= imm_data_size)) {
            memcpy((char*)inter->back().data + inter->back().count, data, len);
            inter->back().count += len;
            return;
        }

        size_t new_block_size = std::max((size_t)len, (size_t)imm_data_size);

        struct imm_data inter_en;
        inter_en.data = allocator->allocate(new_block_size);
        if (!inter_en.data) {
            throw std::runtime_error("Failed to allocate intermediate data block.");
        }

        memcpy(inter_en.data, data, len);
        inter_en.count = len;
        inter->push_back(inter_en);
    }

    void mapper(void* arg) {
        map_parameter* para = (map_parameter*)arg;
        map_func(para->map_data, para->task_id, para->length);
        pthread_barrier_wait(&barrier_map);
        delete para;
    }

    void reducer(void* arg) {
        reduce_parameter* para = (reduce_parameter*)arg;
        pthread_barrier_wait(&barrier_map);
        reduce_func(para->task_id);
        pthread_barrier_wait(&barrier_reduce);
        delete para;
    }

    // 将原来的 run_mr 稍作修改，使其专注于执行单轮迭代，而不包含计时和打印
    void run_mr_single_iteration(char* map_data, size_t data_length) {
        allocator->init();

        std::vector<std::thread*> map_threads, reduce_threads;

        char** map_data_arr = (char**)malloc(sizeof(char*) * map_num);
        size_t* map_data_dis = (size_t*)malloc(sizeof(size_t) * map_num);
        splice(map_data_arr, map_data_dis, map_data, data_length);

        for (int i = 0; i < map_num; i++) {
            map_parameter* mp = new map_parameter();
            mp->task_id = i;
            mp->length = map_data_dis[i];
            mp->map_data = map_data_arr[i];
            map_threads.push_back(new std::thread(&MapReduce::mapper, this, mp));
        }

        for (int i = 0; i < reduce_num; i++) {
            reduce_parameter* rp = new reduce_parameter();
            rp->task_id = i;
            reduce_threads.push_back(new std::thread(&MapReduce::reducer, this, rp));
        }

        for (auto th : map_threads) {
            th->join();
            delete th;
        }
        for (auto th : reduce_threads) {
            th->join();
            delete th;
        }

        free(map_data_arr);
        free(map_data_dis);
        allocator->shutdown();
    }
};

#endif //UNTITLED25_MAPREDUCE_H