# 服务器配置

#### 硬件环境

- 两颗 32 核 Intel Xeon GOLD 6430 CPU (主频 2.10GHz)
- 256GB 内存
- 以及一块支持CXL的Supermicro x13dei 主板。
- CXL-DSM 启用了超线程，但根据先前工作的建议，禁用了CPUC-states、动态 CPU 频率调整和透明大页。

#### 软件环境

- Linux Kernel >= 6.9.0-smdk
- OS version = Ubuntu 22.04.5 LTS
- CMake >= 3.22.1
- Jemalloc: 5.2.1-4ubuntu
- Intel TBB: tbb = 2021.5.0-7ubuntu2, tbb-devel = 2021.5.0-7ubuntu2
- gcc：11.4.0

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

1. 解压

```C++
gunzip twitter-2010.txt.gz
```

1. 特征工程

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

1. 编译

```C++
g++ -std=c++17 -Wall -O2 main.cpp -o kmeans_runner -pthread
```

1. 运行

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

1. （可选）如果想要进行TPP，Weighted Interleaving对Kmeans的评估，可以执行total_exp.sh脚本

```C++
sudo bash total_exp.sh
```

脚本执行过程图：

![img](https://jianmucloud.feishu.cn/space/api/box/stream/download/asynccode/?code=NGI2NmYyNGYwMjJhZGMwNWI4YTU5YmVmM2I4Yzc4Y2RfNUc3ZXQ4bnJwZXdmWGZPYkJ1dVJZN1hyZ1MxYmhTVGRfVG9rZW46RXI4M2JUM2VQb3h0OFN4N1lidWNUYkw4bnVkXzE3NTQ5NjQzNDQ6MTc1NDk2Nzk0NF9WNA)

脚本执行成功结果图：

![img](https://jianmucloud.feishu.cn/space/api/box/stream/download/asynccode/?code=YzkxMTNkNDRlMjNkNjE4NjgxNmNiZjdlOWY2OGYzODNfNUx4MjNvbzRtY1dLSVNoN0d3eFF2a1VvUWxXWlR6aTFfVG9rZW46VWRiTmJGQnQ3b1ZwZ054d1dFMGM4TjdkbkJkXzE3NTQ5NjQzNDQ6MTc1NDk2Nzk0NF9WNA)
