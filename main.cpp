// --- FILE: main.cpp ---

#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <chrono>

#include "KMeans.h"
#include "StandardAllocator.h"
#include "KMeansTextFileDataSource.h"

// 线程同步屏障的全局声明
pthread_barrier_t barrier_map;
pthread_barrier_t barrier_reduce;

// #define DEBUG_MODE

void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name
              << " <map_tasks> <reduce_tasks> <num_means> <data_file_path> <dimensions>" << std::endl;
}

int main(int argc, char **argv) {
    // [计时器1] 开始计时总时间
    auto total_start_time = std::chrono::high_resolution_clock::now();

    if (argc < 6) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        // --- 1. 参数解析 ---
        int map_num = std::stoi(argv[1]);
        int reduce_num = std::stoi(argv[2]);
        size_t num_means = std::stoull(argv[3]);
        std::string filepath = argv[4];
        size_t dim = std::stoull(argv[5]);
        const int iterations = 10;

        // --- 2. 设置组件 & 加载数据 ---
        auto allocator = std::make_unique<StandardAllocator>();
        auto dataSource = std::make_unique<KMeansTextFileDataSource>(filepath, dim, allocator.get());

        dataSource->load(); // 数据加载包含在总时间计时内

        size_t item_size = sizeof(double) * (dim + 1);
        size_t num_points = (dataSource->get_size() > 0) ? (dataSource->get_size() / item_size) : 0;

        if (num_points == 0) {
            throw std::runtime_error("Data source loaded 0 points. Cannot run KMeans.");
        }

        // 3. 创建KMeans MapReduce 任务
        auto mr_job = std::make_unique<KMeans>(
            map_num,
            reduce_num,
            num_points,
            dim,
            num_means,
            allocator.get()
        );

        // --- 4. 核心计算循环 ---
        
        // [计时器2] 开始计时MapReduce核心计算时间
        auto mapreduce_start_time = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; ++i) {
            mr_job->run_mr_single_iteration(static_cast<char*>(dataSource->get_data()), dataSource->get_size());
            if (i < iterations - 1) {
                 mr_job->reset_intermediate_data();
            }
        }
        
        // [计时器2] 结束计时MapReduce核心计算时间
        auto mapreduce_end_time = std::chrono::high_resolution_clock::now();

        // [计时器1] 结束计时总时间
        auto total_end_time = std::chrono::high_resolution_clock::now();

        // --- 5. 计算并打印时间 ---
        std::chrono::duration<double> mapreduce_duration = mapreduce_end_time - mapreduce_start_time;
        std::chrono::duration<double> total_duration = total_end_time - total_start_time;

        // [关键修改] 按固定格式输出两个时间，供脚本解析
        printf("[MAPREDUCE TIME] %f seconds\n", mapreduce_duration.count());
        printf("[TOTAL TIME] %f seconds\n", total_duration.count());

    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}