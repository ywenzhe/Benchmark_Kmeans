#ifndef KMEANS_TEXT_FILE_DATA_SOURCE_H
#define KMEANS_TEXT_FILE_DATA_SOURCE_H

#include "BaseDataManager.h"
#include "BaseAllocator.h"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>

// 专用于从文本文件加载KMeans数据的数据源
// 文件格式应为每行代表一个点，维度由空格分隔
class KMeansTextFileDataSource : public IDataSource {
private:
    BaseAllocator* allocator;
    std::string filepath;
    double* buffer; // <-- 关键修改 #1: 指针类型改为 double*
    size_t buffer_size_bytes;
    size_t dim;

public:
    KMeansTextFileDataSource(const std::string& path, size_t dimension, BaseAllocator* alloc)
        : allocator(alloc), filepath(path), buffer(nullptr), buffer_size_bytes(0), dim(dimension) {
        if (!allocator) {
            throw std::runtime_error("Allocator cannot be null for KMeansTextFileDataSource.");
        }
        if (dim == 0) {
            throw std::runtime_error("Dimension cannot be zero.");
        }
    }

    ~KMeansTextFileDataSource() override {
        if (buffer) {
            allocator->deallocate(buffer);
        }
    }

    void perform_load() override {
        std::cout << "Reading and parsing KMeans data from file: " << filepath << "..." << std::endl;
        std::ifstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filepath);
        }

        // <-- 关键修改 #2: 临时向量的类型改为 double
        std::vector<std::vector<double>> points_vec;
        std::string line;

        while (std::getline(file, line)) {
            std::stringstream ss(line);
            std::vector<double> point; // <-- 关键修改 #3: 临时点的类型改为 double
            double value;              // <-- 关键修改 #4: 读取值的变量类型改为 double

            // 这个循环现在可以正确地从一行中读取多个由空格分隔的浮点数
            while (ss >> value) {
                point.push_back(value);
            }

            if (!point.empty()) {
                if (point.size() == dim) {
                    points_vec.push_back(point);
                } else {
                    // 这就是您看到的警告信息的来源
                    std::cerr << "Warning: Skipping line with incorrect dimension. Expected " << dim << ", got " << point.size() << ". Line: \"" << line << "\"" << std::endl;
                }
            }
        }
        file.close();

        if (points_vec.empty()) {
            // 如果所有行都被跳过，这里会报错
            throw std::runtime_error("No valid data points were loaded from the file. Please check the file format and data types.");
        }

        size_t num_points = points_vec.size();
        size_t point_stride = dim + 1;
        buffer_size_bytes = sizeof(double) * num_points * point_stride; // <-- 关键修改 #5: 大小计算用 sizeof(double)

        buffer = (double*)allocator->allocate(buffer_size_bytes); // <-- 关键修改 #6: 指针转换用 double*
        if (!buffer) {
            throw std::runtime_error("Failed to allocate buffer for KMeans data points.");
        }

        for (size_t i = 0; i < num_points; ++i) {
            for (size_t j = 0; j < dim; ++j) {
                buffer[i * point_stride + j] = points_vec[i][j];
            }
        }
    }

    void* get_data() override { return buffer; }
    size_t get_size() override { return buffer_size_bytes; }
};

#endif // KMEANS_TEXT_FILE_DATA_SOURCE_H