# 服务器配置

#### 硬件环境

- 两颗 32 核 Intel Xeon GOLD 6430 CPU (主频 2.10GHz)
- 256GB 内存
- 以及一块支持CXL的Supermicro x13dei 主板。
- CXL-DSM 启用了超线程，但根据先前工作的建议，禁用了CPUC-states、动态 CPU 频率调整和透明大页。

#### 软件环境

- Linux Kernel >= 6.9.0-smdk
- OS version >= Ubuntu 22.04.5 LTS
- CMake >= 3.22.1
- Jemalloc >= 5.2.1-4ubuntu
- Intel TBB: tbb >= 2021.5.0-7ubuntu2, tbb-devel >= 2021.5.0-7ubuntu2
- gcc >= 11.4.0

# Kmeans数据集——**Twitter** **follower** **network-2010**

- 容量：压缩包5.1GB，解压后为24.35GB，利用特征工程转换成KMeans需要的格式后为1.38GB。
- 来源：https://snap.stanford.edu/data/twitter-2010.html
- 格式：文本文档
- 内容摘要：

```Bash
# hzfu @ cxl-server in ~/tpp_codes/datasets [14:55:43] 
$ head -n 10 twitter-2010.txt ##原始数据
1 0
3 2
2 3
5 4
4 5
5 6
13679000 6
8 7
9 7
36019486 7
# hzfu @ cxl-server in ~/tpp_codes/datasets [14:55:54] 
$ head -n 10 kmeans_features_duckdb.txt #特征工程后可用于kmeans的数据
-0.01415563 -0.09658127 -0.00645752
-0.01415563 -0.09940091 -0.00486893
-0.01374236 -0.09658127 -0.00486893
-0.01374236 -0.09658127 -0.00486893
-0.01374236 -0.09658127 -0.00486893
-0.01374236 -0.09658127 -0.00486893
-0.01415563 -0.09376162 -0.00698705
-0.01332909 -0.09094198 -0.00566322
-0.01415563 -0.09658127 -0.00645752
-0.01415563 -0.09658127 -0.00645752
```

- 数据解释：
  - twitter-2010.txt：原始数据，一行有两个int类型数据，表示两个人的id号，前一个id关注了后一个id。
  - kmeans_features_duckdb.txt：经过特征工程（changeTxtToKmeans.py）处理后的数据，一行有三个浮点数，分别代表id，被关注数，关注数，这几个数据都是被归一化的。

# Kmeans使用方法

1. 下载数据集，由于文件较大（5.1GB），推荐使用aria2c下载器，wget也可以。

```C++
aria2c -c -x 16 -s 16 "https://snap.stanford.edu/data/twitter-2010.txt.gz"
```

或者

```C++
wget -c "https://snap.stanford.edu/data/twitter-2010.txt.gz"
```

2. 解压

```C++
gunzip twitter-2010.txt.gz
```

3. 特征工程

解压后的数据集（twitter-2010.txt）不能直接使用，需要进行特征工程，成功后会得到kmeans_features_duckdb.txt，这个文件可以作为Kmeans项目的数据源文件。

```C++
pip install -i https://pypi.tuna.tsinghua.edu.cn/simple duckdb
pip install -i https://pypi.tuna.tsinghua.edu.cn/simple scikit-learn
python3 changeTxtToKmeans.py
$ head -n 10 twitter-2010.txt ##原始数据
1 0
3 2
2 3
5 4
4 5
5 6
13679000 6
8 7
9 7
36019486 7
$ head -n 10 kmeans_features_duckdb.txt #特征工程后可用于kmeans的数据
-0.01415563 -0.09658127 -0.00645752
-0.01415563 -0.09940091 -0.00486893
-0.01374236 -0.09658127 -0.00486893
-0.01374236 -0.09658127 -0.00486893
-0.01374236 -0.09658127 -0.00486893
-0.01374236 -0.09658127 -0.00486893
-0.01415563 -0.09376162 -0.00698705
-0.01332909 -0.09094198 -0.00566322
-0.01415563 -0.09658127 -0.00645752
-0.01415563 -0.09658127 -0.00645752
```

4. 编译

```C++
g++ -std=c++17 -Wall -O2 main.cpp -o kmeans_runner -pthread
```

5. 运行

一个完整的运行命令需要遵循以下格式：

```
./<可执行文件名> <map任务数> <reduce任务数> <聚类中心数> <数据文件路径> <数据维度>
```

假设您有一个名为 `sample_data.txt` 的数据文件，文件中的数据是3维的，并且您想用4个`Map`线程、2个`Reduce`线程来计算5个聚类中心。

那么，您应该在终端中输入以下命令：

```Plain
./kmeans_runner 4 2 5 ./sample_data.txt 3
```

- `./kmeans_runner`: 执行当前目录下的 `kmeans_runner` 程序。
- `4`: 这是第1个参数 (`argv[1]`)，对应 `map_tasks`。
- `2`: 这是第2个参数 (`argv[2]`)，对应 `reduce_tasks`。
- `5`: 这是第3个参数 (`argv[3]`)，对应 `num_means` (K值)。
- `./sample_data.txt`: 这是第4个参数 (`argv[4]`)，对应 `filepath`。
- `3`: 这是第5个参数 (`argv[5]`)，对应 `dim` (数据维度)。

如果使用kmeans_features_duckdb.txt数据，推荐使用命令：

```C++
./kmeans_runner 20 20 4 ./kmeans_features_duckdb.txt 3
```

6. （可选）如果想要进行TPP，Weighted Interleaving对Kmeans的评估，可以执行total_exp.sh脚本

```C++
sudo bash total_exp.sh
```

脚本执行过程图：

![img](https://jianmucloud.feishu.cn/space/api/box/stream/download/asynccode/?code=NGI2NmYyNGYwMjJhZGMwNWI4YTU5YmVmM2I4Yzc4Y2RfNUc3ZXQ4bnJwZXdmWGZPYkJ1dVJZN1hyZ1MxYmhTVGRfVG9rZW46RXI4M2JUM2VQb3h0OFN4N1lidWNUYkw4bnVkXzE3NTQ5NjQzNDQ6MTc1NDk2Nzk0NF9WNA)

脚本执行成功结果图：

![img](https://jianmucloud.feishu.cn/space/api/box/stream/download/asynccode/?code=YzkxMTNkNDRlMjNkNjE4NjgxNmNiZjdlOWY2OGYzODNfNUx4MjNvbzRtY1dLSVNoN0d3eFF2a1VvUWxXWlR6aTFfVG9rZW46VWRiTmJGQnQ3b1ZwZ054d1dFMGM4TjdkbkJkXzE3NTQ5NjQzNDQ6MTc1NDk2Nzk0NF9WNA)

# 代码结构及框架使用说明

## 一. 基本接口

### 1.1 内存分配器（BaseAllocator.h与StandardAllocator.h）

#### 1.1.1 简介

BaseAllocator.h为内存分配器的接口文件，一般无需改动。

如果使用此benchmark的应用API有涉及到与malloc，free不同的内存分配与回收方案时，需要继承此接口，来实现个性化的Allocator。

这里给出了一个样例StandardAllocator.h，其本质上是对标准库 `malloc` 和 `free` 的一层封装。

#### 1.1.2 实际使用

1. 继承BaseAllocator.h，写一个单独的exampleAllocator.h，格式参考StandardAllocator.h。
2. 修改main.cpp文件

![img](https://jianmucloud.feishu.cn/space/api/box/stream/download/asynccode/?code=NDllYWRiODI2ZWVkMTc2NmZhMDg5ZDUzMDJjMzU4NDdfYkpjR2Y2TXBWWlBrSHo4QWtQR0JQNXpGc1Npc3RtMk5fVG9rZW46RG16UGJXeVo4b0tHbXp4UkJ5bWN6MHZJbk1jXzE3NTQ5NjQ1MDQ6MTc1NDk2ODEwNF9WNA)

在这里加入一个else分支，让allocator初始化为自定义的exampleAllocator。

![img](https://jianmucloud.feishu.cn/space/api/box/stream/download/asynccode/?code=ODk3ZTQ1ZjQ3N2E1YWQxNTM4ZTU0YjVkN2I3Y2M1ZTlfUUpEMXN0dVBCbW1PS2hwOWxlVldCbVRVR3ZVQlFYWjlfVG9rZW46WFZvdmJsVnFxb0tyWXp4aW5jM2N1eWJ0bndmXzE3NTQ5NjQ1MDQ6MTc1NDk2ODEwNF9WNA)

为了美观，也可以修改上图main.cpp中的print_usage函数的第22行，加入用户自定义的Allocator名。

在执行可执行文件时，命令需要加上图[Benchmark——Kmeans，WordCount介绍](https://jianmucloud.feishu.cn/wiki/ZksiwMN1mikxLTk0JoVcNUOinXb?fromScene=spaceOverview#share-UYvkdMn9loXH08x1j5ccsOlAnNd)中写好的名称。

如原来的命令是

```Bash
./wordcount 4 2 standard file ./sample.txt
```

现在应该换成

```Bash
./wordcount 4 2 example file ./sample.txt
```

### 1.2 数据源管理器 (BaseDataManager.h, StandardDataManager.h, RondomGeneratedDataManager.h)

BaseDataManager.h是数据读写的基本接口，用来管理数据的加载。

这里给了两种继承BaseDataManager.h接口的样例，其中StandardDataManager.h为从文件读写的类，RondomGeneratedDataManager.h是随机生成数据的类。

本来这个接口是为了AIFM的Dataframe准备的，因为这个应用里实现了数据读写的API，但最后没有使用，但也保留了这个接口，一般应该不需要改动。

如果改动的话，需要在main.cpp中配合修改：

1. 首先在main.cpp中加入用户写好的头文件，如exmapleDataSource.h。
2. 在main.cpp创建数据源部分加入你的else if分支，将dataSource示例化为你创建的exmapleDataSource。

![img](https://jianmucloud.feishu.cn/space/api/box/stream/download/asynccode/?code=NTYwZmE3M2YxYzUxNjUyMmZmMTc4ODhmM2VmMGIxZWFfblYzUHNNV01LQktiYmxnNUNMMWhidUVlQjNab0JQTTJfVG9rZW46TlQzbmJQbWl4b3BPdER4bDByZGNyZmlMbjllXzE3NTQ5NjQ1MDQ6MTc1NDk2ODEwNF9WNA)

## 二. MapReduce接口(MapReduce.h)

### 2.1 简介

此接口是benchmark的核心业务接口，定义了`MapReduce` 基类，封装了所有通用的、与具体业务无关的逻辑，包括：

- 线程的创建、管理与同步（使用 `pthread_barrier`）。
- 启动 Mapper 和 Reducer 任务。
- 测量并报告总执行时间。
- 管理中间数据的存储结构。

### 2.2 执行流程（run_mr函数）

run_mr_single_iteration/run_mr函数是驱动单次MapReduce计算流程的核心。此函数的执行流程几乎等同于整个MapReduce任务的完整流程，其流程如下：

1. **初始化****内存****分配器 (****`allocator->init()`****)**:

默认是一个空操作，除非用户实现了自己的Allocator示例。

2. **数据分片 (****`splice(...)`****)**:

`splice` 函数的职责是将原始的、连续的输入数据块（`map_data`）分割成多个小的数据片段。

实际执行的是子类实现的 `splice` 纯虚函数。

3. **启动** **`Mapper`** **和** **`Reducer`** **线程**
4. **Map阶段：**

执行map_func任务，其中必要的一步是调用 `emit_intermediate` 函数，将处理得到的中间结果（键值对）存入共享的中间数据结构 `vec` 中。

每个 `Mapper` 线程完成其 `map_func` 后，会在 `pthread_barrier_wait(&barrier_map)` 处阻塞等待。

5. **Reduce阶段**:

通过第一次同步后，所有 `Reducer` 线程开始并发执行。每个线程调用由子类实现的 `reduce_func`。

`reduce_func` 会根据自身的 `task_id` 从中间数据结构 `vec` 中拉取（pull）所有`Mapper`为其生成的中间数据，并进行汇总计算。

每个 `Reducer` 线程完成其 `reduce_func` 后，会在 `pthread_barrier_wait(&barrier_reduce)` 处阻塞等待。

6. **线程汇合与资源清理 (****`join`** **and** **`free`****)：**

主线程通过调用每个 `std::thread` 对象的 `join()` 方法，等待所有 `Mapper` 和 `Reducer` 线程执行完毕。

释放为线程参数动态分配的内存（`map_parameter` 和 `reduce_parameter`）以及为数据分片分配的辅助数组（`map_data_arr` 和 `map_data_dis`）。

7. **关闭内存分配器 (****`allocator->shutdown()`****)**:

通知底层内存分配器本次计算已结束，可以进行资源回收等清理工作（例如，解除共享内存段的附加）。

## 三. MapReduce的示例任务一 ——Kmeans

KMeans（K-均值）是一种广泛应用的无监督机器学习算法，其目标是将一个数据集分割成K个不同的“簇”（cluster），使得每个数据点都属于离它最近的那个簇的中心。本项目利用MapReduce并行计算模型，高效地实现了KMeans算法的核心迭代过程，特别适用于处理大规模数据集。

### 3.1 Kmean.h ——Kmeans任务的核心实现类

`KMeans` 类是 `MapReduce` 抽象基类的具体实现，专门用于解决K-均值聚类问题。它继承了 `MapReduce` 类的所有通用并行框架逻辑，并在此基础上，通过重写三个核心的纯虚函数（`map_func`, `reduce_func`, `splice`），注入了K-均值算法的特定业务逻辑。这里简要介绍一下这几个重写函数的主要工作：

1. `splice`

将存储数据的double数组均分为map线程个数的块，将其分配到多个map线程中。

2. `map_func`

每个Mapper线程独立执行`KMeans::map_func` 函数，Mapper会找到计算该点到聚类中心的距离，记录距离最小的中心ID，通过emit_intermediate，将这个处理好的数据点发送给reducer，聚到同一类的点会由同一个reducer来处理。

3. `reduce_func`

每个Reducer线程在 `KMeans::reduce_func` 中执行。他会将自己分内的所有点进行平均，算出一个新的中心点。

### 3.2 main.cpp ——Kmeans任务的主驱动函数

main.cpp是专为Kmeans任务设计的驱动程序，主要依靠调用上层接口来完成整体任务，会将数据源、内存分配器、MapReduce任务组合在一起，并按照预定的逻辑顺序（加载数据 -> 运行计算 -> 输出结果）来驱动整个流程。所有实现MapReduce任务的main.cpp都可以参考本文件的基本流程：

1. 参数解析：

从命令行中读取了运行参数（如输入输出的文件路径），也可以自定义一些自己需要的参数，比如iterations（Kmeans算法中的中心点迭代次数），其中详细参数如下：

- **`<map_tasks>`** **(argv)**: `Map`任务的数量。这个整数值决定了将启动多少个`Mapper`线程来并行处理数据。它通过 `std::stoi(argv[1])` 被解析。
- **`<reduce_tasks>`** **(argv)**: `Reduce`任务的数量。这个整数值决定了将启动多少个`Reducer`线程来并行地更新聚类中心。它通过 `std::stoi(argv[2])` 被解析。
- **`<num_means>`** **(argv)**: 聚类中心的总数，即K-均值算法中的“K”值。这个数值决定了要将数据集划分成多少个簇。由于它可能是一个较大的数字，程序使用 `std::stoull(argv[3])` 将其解析为一个无符号长长整型 (`size_t`)。
- **`<data_file_path>`** **(argv)**: 包含输入数据点的文件的路径。这是一个字符串，程序直接读取 `argv[4]` 来获取。这个路径将被传递给数据源模块（`KMeansTextFileDataSource`）用于加载数据。
- **`<dimensions>`** **(argv)**: 数据集中每个数据点的维度。例如，对于二维空间中的点，这个值就是2。它同样被 `std::stoull(argv[5])` 解析为一个无符号长长整型 (`size_t`)。
- **`iterations`**: KMeans算法的迭代次数。

2. 设置组件 & 加载数据：

这里完成了allocator和dataSource的初始化，并从输入文件中加载了数据。

3. 创建KMeans MapReduce 任务

```C++
  auto mr_job = std::make_unique<KMeans>(
            map_num,
            reduce_num,
            num_points,
            dim,
            num_means,
            allocator.get()
        );
```

4. 执行任务并记录时间

MapReduce任务已集成到run_mr_single_iteration函数中，详见本文章2.2小节。在Kmeans任务中需要中心点的不断迭代，因此会将此函数进行循环，wordCount则不需要。

### 3.3 total_exp.sh ——评估TPP，Weighted Interleaving对Kmeans任务影响

前面的Kmeans主体文件是一个独立完整的Benchmark，这个脚本仅是为了评估TPP，Weighted Interleaving更改对Kmeans任务的影响而编写的，不适用于其他目的，也不适合移植到其他实验。

**如果同样想评估以上功能，**此脚本中的**超参数**应该是用户最应当关注和修改的部分，以适配不同的硬件环境和测试目标。

#### 3.3.1 主要功能

本实验旨在探索不同的TPP，Weighted Interleaving等变量设置下，会怎样影响页面调度以及程序的执行情况。详细实验目的，实验变量设置，实验数据，数据分析，注意事项等请参考以下笔记。

[TPP，Weighted Interleaving对WordCount的影响](https://jianmucloud.feishu.cn/wiki/UPX9wwpH1iHHikk4a61cisptnxc?from=from_copylink)

#### 3.3.2 超参数列举与说明 

- **`EXECUTABLE`**: 指定要运行的C++可执行程序的文件名。默认是`kmeans_runner`。
- **`K_VALUE`**: KMeans算法的“K”值，即聚类中心的数量。
- **`DIMENSIONS`**: 数据集中每个数据点的维度。
- **`FIXED_THREADS_FOR_RATIO_TEST`**: 在第一阶段（内存配比测试）中，固定的线程数量。
- **`THREAD_LIST_FOR_SCALING_TEST`**: 在第二阶段（线程扩展性测试）中，要遍历的线程数列表。
- **`CPU_NODE_BIND`**: 将程序的所有线程绑定到指定的CPU节点（NUMA Node）上。
- **`LOCAL_MEM_NODE`**: 定义哪个NUMA节点被视为“本地内存”（通常是DRAM所在的节点）。
- **`CXL_MEM_NODE`**: 定义哪个NUMA节点被视为“CXL内存”。
