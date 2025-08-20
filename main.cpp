// shenango
extern "C" {
#include <runtime/runtime.h>
}
#include "thread.h"
#include "sync.h"

#include "deref_scope.hpp"
#include "device.hpp"
#include "manager.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <cstring>
#include <list>
#include <vector>
#include <thread>
#include <chrono>
#include <unordered_map>

using namespace far_memory;
using namespace std;

// FarMemory
constexpr size_t kCacheSize = 600 * Region::kSize;
constexpr size_t kFarMemSize = 20ULL << 30;
constexpr unsigned long kNumGCThreads = 12;
constexpr unsigned long long kNumConnections = 600;

// Threads
constexpr unsigned long kMapTasks = 10;
constexpr unsigned long kReduceTasks = 10;

// kMeans
constexpr size_t kNumMeans = 4;
constexpr size_t kDimension = 3;
constexpr size_t kIterations = 10;
constexpr size_t kNumPoints = 41652230;
constexpr size_t kPointStride = kDimension + 1;

// File
constexpr size_t kArrayBlockSize = 4096; // 最好是 dim + 1 的整数倍
constexpr unsigned long long kArrayBlockNum = (4 * kNumPoints / kArrayBlockSize) + 1;
constexpr const char* kFilePath = "./kmeans_features_duckdb.txt";

struct array_block {
    double data[kArrayBlockSize];
    int count;
};

struct map_parameter {
    Array<array_block, kArrayBlockNum>* data_array;
    int block_index;
    size_t block_num;
    int task_id;
};

struct reduce_parameter {
    int task_id;
};

const static int imm_data_size = 4096;

struct imm_data_block {
    char data[imm_data_size];
};

struct imm_data {
    UniquePtr<imm_data_block> data; // 用unique_ptr来管理内存
    int count;                      // 表示存了多少了
};

void generate_points_double(double* pts, int size, int dim, bool padding) {
    int dis = dim;
    if (padding) dis += 1;
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < dim; j++) {
            // 生成 0.0 到 1.0 之间的随机浮点数
            pts[dis * i + j] = (double)rand() / RAND_MAX;
        }
    }
}

class Kmeans {
    private:
    double* cluster_data;

    public:
    FarMemManager* manager;
    int map_num;
    int reduce_num;
    size_t num_points;
    size_t dim;
    size_t num_means;
    size_t row_dis;

    std::vector<std::list<imm_data>*>* vec;
    inline int get_vec_index(int map_id, int reduce_id) {
        return map_id * reduce_num + reduce_id;
    }

    Kmeans(int map_num_, int reduce_num_, size_t num_points_,
        size_t dim_, size_t num_means, FarMemManager* manager_) :
        map_num(map_num_), reduce_num(reduce_num_), num_points(num_points_),
        dim(dim_), num_means(num_means), manager(manager_) {

        row_dis = (num_means + reduce_num - 1) / reduce_num;

        // 按 double 类型计算大小
        size_t cluster_size = num_means * dim;

        cluster_data = new double[cluster_size];

        if (!cluster_data) {
            throw std::runtime_error("Failed to allocate memory for cluster data.");
        }

        generate_points_double(cluster_data, num_means, dim, false);

        vec = new std::vector<std::list<imm_data> *>();
        for (int i = 0; i < map_num * reduce_num; i++) {
            vec->push_back(new std::list<imm_data>());
        }
    }

    ~Kmeans() {
        reset_intermediate_data();
        delete vec;
        delete[] cluster_data;
    }

    template <uint64_t N, typename T>
    void map_func(int block_index, Array<T, N>* map_data, int task_id, size_t block_num) {

        for (size_t block_i = 0; block_i < block_num; block_i++) {
            int count;
            const double* buffer;
            {
                DerefScope scope;
                count = map_data->at(scope, block_index + block_i).count;
                buffer = map_data->at(scope, block_index + block_i).data;
            }

            size_t num_points_in_split = count / kPointStride;

            for (size_t i = 0; i < num_points_in_split; ++i) {
                const double* current_point = buffer + i * kPointStride;
                int best_cluster_id = -1;
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
                double cluster_id = (double)best_cluster_id;
                ((double*)current_point)[dim] = cluster_id;

                int reduce_id = best_cluster_id / row_dis;
                if (reduce_id >= reduce_num) reduce_id = reduce_num - 1;

                emit_intermediate(vec->at(get_vec_index(task_id, reduce_id)),
                    (char*)current_point, sizeof(double) * kPointStride);
            }
        }
    }


    void mapper(void* arg) {
        struct map_parameter* para = (struct map_parameter*)arg;
        printf("[Mapper %d Start] block_index=%d, block_num=%zu\n", para->task_id, para->block_index, para->block_num);
        map_func(para->block_index, para->data_array, para->task_id, para->block_num);
    }

    void reduce_func(int task_id) {
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

        for (int map_id = 0; map_id < map_num; map_id++) {
            std::list<imm_data>* inter = vec->at(get_vec_index(map_id, task_id));

            for (auto& data_block : *inter) {
                DerefScope scope;
                const double* data_points = (double*)data_block.data.deref(scope)->data;
                int num_points_in_block = data_block.count / (sizeof(double) * kPointStride);

                for (int i = 0; i < num_points_in_block; ++i) {
                    const double* point = data_points + i * kPointStride;
                    // [修复] 将存储的簇ID从double转回int
                    int cluster_id = (int)point[dim];
                    size_t local_cluster_idx = cluster_id - start_mean_index;

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

    void reducer(void* arg) {
        reduce_parameter* para = (reduce_parameter*)arg;
        printf("[Reducer %d Start]\n", para->task_id);
        reduce_func(para->task_id);
    }

    void splice(int* data_arr, int* data_dis, size_t block_num) {
        size_t avg_block_per_map = block_num / map_num;
        int index = 0;
        for (int i = 0; i < map_num; ++i) {
            data_arr[i] = index;
            if (index + avg_block_per_map < block_num) {
                data_dis[i] = avg_block_per_map;
            } else {
                data_dis[i] = block_num - index; // 最后一个map处理剩余的所有block
            }
            index += data_dis[i];
        }
    }

    void reset_intermediate_data() {
        for (auto& list_ptr : *vec) {
            // check 是否要清空 UniquePtr

            // 清空列表，为下一次迭代做准备
            list_ptr->clear();
        }
    }

    void emit_intermediate(std::list<imm_data>* inter, const void* data, int len) {
        if (inter->empty() || inter->back().count + len + 1 > imm_data_size) {
            struct imm_data inter_en;
            inter_en.count = 0;
            inter_en.data = manager->allocate_unique_ptr<imm_data_block>(); // 用block 512 (unique ptr)来存
            // printf("SHIT\n");
            inter->push_back(std::move(inter_en));
        }
        {
            DerefScope scope;
            auto imm_data_block_ptr = inter->back().data.deref_mut(scope);
            memcpy(imm_data_block_ptr->data + inter->back().count, data, len);
        }
        inter->back().count += len;
        return;
    }

    void run_mr_single_iteration(Array<array_block, kArrayBlockNum>* data_array) {

        std::vector<rt::Thread> map_threads, reduce_threads;

        int* map_data_arr = (int*)malloc(sizeof(int) * map_num); // 表示从data_array中开始处理的index
        int* map_data_dis = (int*)malloc(sizeof(int) * map_num); // 表示每个map需要处理的array_block数量
        splice(map_data_arr, map_data_dis, kArrayBlockNum);

        for (int i = 0; i < map_num; i++) {
            map_parameter* mp = new map_parameter();
            mp->task_id = i;
            mp->block_num = map_data_dis[i];
            mp->data_array = data_array;
            mp->block_index = map_data_arr[i];

            map_threads.emplace_back(rt::Thread([this, mp]() {
                mapper(mp);
                delete mp; // 在线程完成后释放参数
                }
            ));
        }

        for (auto& th : map_threads) {
            th.Join();
        }

        for (int i = 0; i < reduce_num; i++) {
            reduce_parameter* rp = new reduce_parameter();
            rp->task_id = i;

            reduce_threads.emplace_back(rt::Thread([this, rp]()
                {
                    reducer(rp);
                    delete rp; // 在线程完成后释放参数
                }));
        }

        for (auto& th : reduce_threads) {
            th.Join();
        }

        free(map_data_arr);
        free(map_data_dis);
    }
};

void do_work(FarMemManager* manager) {
    // [计时器1] 开始计时总时间
    auto total_start_time = std::chrono::high_resolution_clock::now();

    // 加载数据
    cout << "Reading data from file: " << kFilePath << "..." << std::endl;
    ifstream file(kFilePath);
    if (!file.is_open()) {
        throw runtime_error(string("Failed to open file: ") + kFilePath);
    }

    vector<vector<double>> points_vec;
    string line;

    while (getline(file, line)) {
        stringstream ss(line);
        vector<double> point;
        double value;

        while (ss >> value) {
            point.push_back(value);
        }

        if (!point.empty()) {
            if (point.size() == kDimension) {
                points_vec.push_back(point);
            } else {
                cerr << "Warning: Skipping line with incorrect dimension. Expected "
                    << kDimension << ", got " << point.size() << ". Line: \"" << line
                    << "\"" << endl;
            }
        }
    }
    file.close();

    if (points_vec.empty()) {
        // 如果所有行都被跳过，这里会报错
        throw std::runtime_error("No valid data points were loaded from the file. Please check the file format and data types.");
    }

    size_t actual_points = points_vec.size();

    size_t length = actual_points * kPointStride;
    double* buffer = new double[length];

    // 将数据读到 buffer 中
    for (size_t i = 0; i < actual_points; ++i) {
        for (size_t j = 0; j < kDimension; ++j) {
            buffer[i * kPointStride + j] = points_vec[i][j];
        }
    }

    std::vector<std::vector<double>>().swap(points_vec); // 清空 vector

    // 创建 farmemory array 对象
    auto data_array = manager->allocate_array<array_block, kArrayBlockNum>();

    size_t total_read = 0;
    int block_index = 0;

    // 将 buffer 按 kArrayBlockSize 的块读入到 fm array 中
    while (total_read < length) {
        DerefScope scope;
        size_t to_read = min(kArrayBlockSize, length - total_read);

        memcpy(
            data_array.at_mut(scope, block_index).data,
            buffer + block_index * kArrayBlockSize,
            to_read * sizeof(double)
        );
        data_array.at_mut(scope, block_index).count = to_read;

        total_read += to_read;
        block_index++;

        if (total_read % 819200 == 0) {
            std::cout << "Read " << total_read << "/" << length << " doubles." << std::endl;
        } // 读取进度
    }
    delete[] buffer;

    auto map_reduce = std::make_unique<Kmeans>(
        kMapTasks,
        kReduceTasks,
        kNumPoints,
        kDimension,
        kNumMeans,
        manager
    );

    // --- 4. 核心计算循环 ---

    // [计时器2] 开始计时MapReduce核心计算时间
    auto mapreduce_start_time = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < kIterations; ++i) {
        map_reduce->run_mr_single_iteration(&data_array);
        if (i < kIterations - 1) {
            map_reduce->reset_intermediate_data();
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
}

int argc;
void _main(void* arg) {
    char** argv = (char**)arg;
    std::string ip_addr_port(argv[1]);

    auto manager = std::unique_ptr<FarMemManager>(
        FarMemManagerFactory::build(
            kCacheSize,
            kNumGCThreads,
            new TCPDevice(helpers::str_to_netaddr(ip_addr_port), kNumConnections, kFarMemSize)));
    do_work(manager.get());
}

int main(int _argc, char* argv[]) {
    int ret;

    if (_argc < 3) {
        std::cerr << "usage: [cfg_file] [ip_addr:port]" << std::endl;
        return -EINVAL;
    }

    char conf_path[strlen(argv[1]) + 1];
    strcpy(conf_path, argv[1]);
    for (int i = 2; i < _argc; i++) {
        argv[i - 1] = argv[i];
    }
    argc = _argc - 1;

    ret = runtime_init(conf_path, _main, argv);
    if (ret) {
        std::cerr << "failed to start runtime" << std::endl;
        return ret;
    }

    return 0;
}