#!/bin/bash

source ../../shared.sh

# 确保之前的进程已经终止
sudo pkill -9 main

# 编译程序
make clean
make -j

# 重启IO内核和内存服务器
rerun_local_iokerneld
rerun_mem_server

# 运行程序
echo "启动Kmeans MapReduce程序..."
run_program ./main

# 完成后清理
kill_local_iokerneld
echo "程序执行完成"