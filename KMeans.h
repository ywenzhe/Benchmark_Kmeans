// --- FILE: KMeans.h ---

#ifndef UNTITLED25_KMEANS_H
#define UNTITLED25_KMEANS_H

#include "MapReduce.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <limits>
#include <algorithm> // for std::min

// [修复] 辅助函数现在处理 double 类型
void generate_points_double(double *pts, int size, int dim, bool padding) {
   int dis = dim;
   if (padding) dis += 1;
   for (int i = 0; i < size; i++) {
      for (int j = 0; j < dim; j++) {
         // 生成 0.0 到 1.0 之间的随机浮点数
         pts[dis * i + j] = (double)rand() / RAND_MAX;
      }
   }
}


class KMeans: public MapReduce {
private:
    // [修复] 聚类中心现在是 double 类型
    double *cluster_data;

public:
    size_t num_points;
    size_t dim;
    size_t num_means;
    size_t row_dis;

    KMeans(int map_num, int reduce_num, size_t num_points, size_t dim, size_t num_means, BaseAllocator* alloc)
        : MapReduce(map_num, reduce_num, alloc),
          num_points(num_points), dim(dim), num_means(num_means) {

        row_dis = (num_means + reduce_num - 1) / reduce_num;

        // [修复] 按 double 类型计算大小
        size_t cluster_data_size = sizeof(double) * dim * num_means;
        cluster_data = (double*)allocator->allocate(cluster_data_size);
        if (!cluster_data) {
            throw std::runtime_error("Failed to allocate memory for cluster centers.");
        }
        
        // 使用新的浮点数生成函数
        generate_points_double(cluster_data, num_means, dim, false);
    }

    ~KMeans() {
        if (cluster_data) {
            allocator->deallocate(cluster_data);
        }
    }

    // Map功能：现在处理 double 类型的数据
    void map_func(void* map_data, int task_id, size_t data_length) override {
        double *points = (double*)map_data;
        size_t point_stride = dim + 1;
        // [修复] 按 double 类型计算大小
        size_t num_points_in_split = data_length / (sizeof(double) * point_stride);

        for(size_t i = 0; i < num_points_in_split; ++i) {
            double *current_point = points + i * point_stride;
            int best_cluster_id = -1;
            // [修复] 距离现在是 double 类型
            double min_distance_sq = std::numeric_limits<double>::max();

            for (size_t j = 0; j < num_means; ++j) {
                double current_distance_sq = 0;
                for (size_t k = 0; k < dim; ++k) {
                    // [修复] 使用 double 类型进行差值计算
                    double diff = current_point[k] - cluster_data[j * dim + k];
                    current_distance_sq += diff * diff;
                }

                if (current_distance_sq < min_distance_sq) {
                    min_distance_sq = current_distance_sq;
                    best_cluster_id = j;
                }
            }

            // [修复] 将簇ID (整数) 强制转换为 double 存入
            current_point[dim] = (double)best_cluster_id;

            int reduce_id = best_cluster_id / row_dis;
            if (reduce_id >= reduce_num) reduce_id = reduce_num - 1;

            emit_intermediate(vec->at(get_vec_index(task_id, reduce_id)),
                              (char*)current_point, sizeof(double) * point_stride);
        }
    }

    // Reduce功能：现在处理 double 类型的数据
    void reduce_func(int task_id) override {
        size_t start_mean_index = task_id * row_dis;
        if (start_mean_index >= num_means) {
            printf("[Reducer %d] No clusters to process, skipping.\n", task_id);
            return;
        }
        size_t end_mean_index = std::min((size_t)((task_id + 1) * row_dis), num_means);
        size_t reducer_means_count = end_mean_index - start_mean_index;
        
        std::vector<uint64_t> point_counts(reducer_means_count, 0);
        // [修复] sums 向量现在存储 double 类型
        std::vector<std::vector<double>> sums(reducer_means_count, std::vector<double>(dim, 0.0));

        size_t point_stride = dim + 1;

        for (int map_id = 0; map_id < map_num; map_id++) {
            std::list<imm_data>* inter = vec->at(get_vec_index(map_id, task_id));

            for (auto const& data_block : *inter) {
                double* data_points = (double*)data_block.data;
                int num_points_in_block = data_block.count / (sizeof(double) * point_stride);

                for (int i = 0; i < num_points_in_block; ++i) {
                    double* point = data_points + i * point_stride;
                    // [修复] 将存储的簇ID从double转回int
                    int cluster_id = (int)point[dim];
                    int local_cluster_idx = cluster_id - start_mean_index;

                    if (local_cluster_idx >= 0 && local_cluster_idx < reducer_means_count) {
                        point_counts[local_cluster_idx]++;
                        for (size_t j = 0; j < dim; ++j) {
                            sums[local_cluster_idx][j] += point[j];
                        }
                    }
                }
            }
        }

        for (size_t i = 0; i < reducer_means_count; ++i) {
            if (point_counts[i] > 0) {
                int global_cluster_id = start_mean_index + i;
                for (size_t j = 0; j < dim; ++j) {
                    cluster_data[global_cluster_id * dim + j] = sums[i][j] / point_counts[i];
                }
            }
        }
        printf("[Reducer %d] Processed its assigned clusters.\n", task_id);
    }

    // 数据分片逻辑：现在处理 double 类型的数据
    void splice(char** data_arr, size_t* data_dis, char* map_data, size_t data_length) override {
        // [修复] 按 double 类型计算大小
        size_t item_size = sizeof(double) * (dim + 1);
        size_t total_items = data_length / item_size;
        size_t items_per_map = total_items / map_num;
        size_t current_item_pos = 0;

        for (int i = 0; i < map_num; ++i) {
            size_t items_for_this_map;
            if (i == map_num - 1) {
                items_for_this_map = total_items - current_item_pos;
            } else {
                items_for_this_map = items_per_map;
            }
            data_arr[i] = map_data + current_item_pos * item_size;
            data_dis[i] = items_for_this_map * item_size;
            current_item_pos += items_for_this_map;
        }
    }
};

#endif //UNTITLED25_KMEANS_H