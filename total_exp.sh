#!/bin/bash

# ==============================================================================
#           Definitive CXL Benchmark Automation Script for KMeans
# - Adapts the comprehensive CXL benchmarking framework for the KMeans workload.
# - Ensures logical, sorted output in the final CSV by defining a specific
#   execution order for memory policies, from CXL-heavy to DRAM-heavy.
# - Generates a CSV output with Promote/Demote statistics for TPP analysis.
# - Records both total execution time and core MapReduce computation time.
# ==============================================================================

# ==============================================================================
#                              CONFIGURATIONS
# ==============================================================================
# --- General Settings ---
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
EXECUTABLE="$SCRIPT_DIR/kmeans_runner" # 确保可执行文件名正确

DATASET_PATH="$1"
NUM_RUNS=1
OUTPUT_CSV="$2"

if [ "$#" -ne 2 ]; then
    echo "错误: 需要提供两个参数。" >&2
    echo "用法: $0 <数据集路径> <输出CSV文件名>" >&2
    exit 1
fi

# --- KMeans Specific Settings ---
K_VALUE=4
DIMENSIONS=3

# --- Threading and Test Phase Settings ---
FIXED_THREADS_FOR_RATIO_TEST=20
THREAD_LIST_FOR_SCALING_TEST="4 8 16 32 64"
FIXED_RATIO_FOR_SCALING_TEST="Interleave5050"

# --- NUMA/CXL Hardware Settings ---
CPU_NODE_BIND="7"
LOCAL_MEM_NODE=7
CXL_MEM_NODE=8
NUMACTL_CMD="numactl"

# ==============================================================================
#                          FUNCTION DEFINITIONS
# ==============================================================================

function get_vmstat_val() {
    local val=$(grep "$1" /proc/vmstat | awk '{print $2}'); echo "${val:-0}";
}

function set_scenario() {
    echo "Applying Scenario: demotion=$1, balancing=$2, reclaim=$3"
    if ! echo "$1" > "/sys/kernel/mm/numa/demotion_enabled" || ! echo "$2" > "/proc/sys/kernel/numa_balancing" || ! echo "$3" > "/proc/sys/vm/zone_reclaim_mode"; then
        echo "ERROR: Failed to apply scenario settings." >&2; exit 1;
    fi; sleep 1;
}

# The core function to run a single benchmark trial
function run_single_trial() {
    local scenario_name=$1 policy_name=$2 numactl_options=$3 threads=$4 run_id=$5 page_size=$6 policy_nodes=$7
    echo; echo "--> Trial: Scenario=[$scenario_name], Policy=[$policy_name], Threads=[$threads], Run=[$run_id], K=[$K_VALUE]"

    if [[ "$policy_name" == Weighted-* ]]; then
        if [ ! -d "/sys/kernel/mm/mempolicy/weighted_interleave" ]; then
            echo "    ERROR: Path for weighted interleave not found." >&2
            echo "$scenario_name,$policy_name,$CPU_NODE_BIND,\"$policy_nodes\",$threads,$run_id,ERROR,ERROR,0,0,0,0" >> "$OUTPUT_CSV"; return;
        fi;
        local weights=$(echo "$policy_name" | cut -d'-' -f2); local weight_local=$(echo "$weights" | cut -d':' -f1); local weight_cxl=$(echo "$weights" | cut -d':' -f2)
        echo "    Setting weights for $policy_name -> Node $LOCAL_MEM_NODE: $weight_local, Node $CXL_MEM_NODE: $weight_cxl"
        echo "$weight_local" > "/sys/kernel/mm/mempolicy/weighted_interleave/node$LOCAL_MEM_NODE"; echo "$weight_cxl" > "/sys/kernel/mm/mempolicy/weighted_interleave/node$CXL_MEM_NODE"
    fi

    # Get promotion/demotion stats before the run
    local promote_before=$(get_vmstat_val 'pgpromote_success')
    local demote_kswapd_before=$(get_vmstat_val 'pgdemote_kswapd')
    local demote_direct_before=$(get_vmstat_val 'pgdemote_direct')
    # [BUG FIX] 确保这里正确地将两个 'before' 值相加
    local demote_before=$((demote_kswapd_before + demote_direct_before))
    
    echo "    Flushing page cache..."; sync; echo 3 > /proc/sys/vm/drop_caches; sleep 1
    
    local map_tasks=$threads
    local reduce_tasks=$K_VALUE
    
    local full_command="$NUMACTL_CMD --cpunodebind=$CPU_NODE_BIND $numactl_options"
    
    echo "    Executing: stdbuf -o0 $full_command $EXECUTABLE $map_tasks $reduce_tasks $K_VALUE \"$DATASET_PATH\" $DIMENSIONS"
    
    local output_and_errors;
    output_and_errors=$(stdbuf -o0 $full_command $EXECUTABLE $map_tasks $reduce_tasks $K_VALUE "$DATASET_PATH" $DIMENSIONS 2>&1)
    
    # Get promotion/demotion stats after the run
    local promote_after=$(get_vmstat_val 'pgpromote_success')
    local demote_kswapd_after=$(get_vmstat_val 'pgdemote_kswapd')
    local demote_direct_after=$(get_vmstat_val 'pgdemote_direct')
    local demote_after=$((demote_kswapd_after + demote_direct_after))
    
    # Calculate deltas and convert to human-readable units
    local promotes_delta=$((promote_after - promote_before))
    local demotes_delta=$((demote_after - demote_before)) # 现在这个差值计算是正确的
    
    local pages_per_gb=$((1024 * 1024 * 1024 / page_size))
    local promotes_kb=$(echo "scale=2; ($promotes_delta * $page_size) / 1024" | bc)
    local demotes_gb="0.00"; if [ $pages_per_gb -gt 0 ]; then demotes_gb=$(echo "scale=2; $demotes_delta / $pages_per_gb" | bc); fi
    
    # Parse execution times from the program's output
    local mr_time_raw=$(echo "$output_and_errors" | grep '\[MAPREDUCE TIME\]' | awk '{print $3}')
    local total_time_raw=$(echo "$output_and_errors" | grep '\[TOTAL TIME\]' | awk '{print $3}')

    local mr_time="ERROR"; local total_time="ERROR"

    if [ -z "$total_time_raw" ] || [ -z "$mr_time_raw" ]; then
        echo "    ERROR: Failed to capture execution times." >&2; echo "    Program Output:" >&2; echo "$output_and_errors" >&2
    else
        mr_time=$(printf "%.4f" "$mr_time_raw")
        total_time=$(printf "%.4f" "$total_time_raw")
        echo "    SUCCESS: TotalTime=$total_time s, MapReduceTime=$mr_time s, Promotes=$promotes_delta, Demotes=$demotes_delta"
    fi

    # Append the final, formatted result to the CSV file
    echo "$scenario_name,$policy_name,$CPU_NODE_BIND,\"$policy_nodes\",$threads,$run_id,$total_time,$mr_time,$promotes_delta,$promotes_kb,$demotes_delta,$demotes_gb" >> "$OUTPUT_CSV"
}

# ==============================================================================
#                           MAIN EXECUTION SCRIPT
# ==============================================================================

if [ "$(id -u)" -ne 0 ]; then echo "ERROR: Root privileges required." >&2; exit 1; fi
if [ ! -f "$EXECUTABLE" ]; then echo "ERROR: Executable not found: '$EXECUTABLE'." >&2; exit 1; fi
if ! command -v bc &> /dev/null; then echo "ERROR: 'bc' not installed." >&2; exit 1; fi
if ! $NUMACTL_CMD --help | grep -q "weighted-interleave"; then echo "WARNING: numactl lacks --weighted-interleave support."; fi

PAGE_SIZE=$(getconf PAGE_SIZE); echo "Detected System Page Size: $PAGE_SIZE Bytes"; echo

echo "Backing up original system settings...";
ORIGINAL_DEMOTION=$(cat "/sys/kernel/mm/numa/demotion_enabled" 2>/dev/null || echo "N/A"); ORIGINAL_BALANCING=$(cat "/proc/sys/kernel/numa_balancing" 2>/dev/null || echo "N/A"); ORIGINAL_RECLAIM=$(cat "/proc/sys/vm/zone_reclaim_mode" 2>/dev/null || echo "N/A");
echo "Backup complete."; echo

echo "Scenario,MemoryPolicy,CPU_Node,Mem_Policy_Node,Threads,RunID,TotalTime_s,MapReduceTime_s,Promotes,Promotes_KB,Demotes,Demotes_GB" > "$OUTPUT_CSV"

declare -A scenarios; scenarios["TPP_ON"]="true;2;1"; scenarios["TPP_OFF"]="false;0;0"
declare -A policies; declare -A policy_node_map
declare -a policy_order=( "CXLOnly" "PreferredCXL" "Weighted-1:5" "Weighted-1:4" "Weighted-1:3" "Weighted-1:2" "Interleave5050" "Weighted-1:1" "Weighted-2:1" "Weighted-3:1" "Weighted-4:1" "Weighted-5:1" "LocalOnly" )
policies["LocalOnly"]="--membind=$LOCAL_MEM_NODE"; policy_node_map["LocalOnly"]="$LOCAL_MEM_NODE"
policies["CXLOnly"]="--membind=$CXL_MEM_NODE"; policy_node_map["CXLOnly"]="$CXL_MEM_NODE"
policies["Interleave5050"]="--interleave=$LOCAL_MEM_NODE,$CXL_MEM_NODE"; policy_node_map["Interleave5050"]="$LOCAL_MEM_NODE,$CXL_MEM_NODE"
policies["PreferredCXL"]="--preferred=$CXL_MEM_NODE"; policy_node_map["PreferredCXL"]="$CXL_MEM_NODE (preferred)"
policies["Weighted-1:1"]="--weighted-interleave=$LOCAL_MEM_NODE,$CXL_MEM_NODE"; policy_node_map["Weighted-1:1"]="$LOCAL_MEM_NODE,$CXL_MEM_NODE"
policies["Weighted-1:2"]="--weighted-interleave=$LOCAL_MEM_NODE,$CXL_MEM_NODE"; policy_node_map["Weighted-1:2"]="$LOCAL_MEM_NODE,$CXL_MEM_NODE"
policies["Weighted-1:3"]="--weighted-interleave=$LOCAL_MEM_NODE,$CXL_MEM_NODE"; policy_node_map["Weighted-1:3"]="$LOCAL_MEM_NODE,$CXL_MEM_NODE"
policies["Weighted-1:4"]="--weighted-interleave=$LOCAL_MEM_NODE,$CXL_MEM_NODE"; policy_node_map["Weighted-1:4"]="$LOCAL_MEM_NODE,$CXL_MEM_NODE"
policies["Weighted-1:5"]="--weighted-interleave=$LOCAL_MEM_NODE,$CXL_MEM_NODE"; policy_node_map["Weighted-1:5"]="$LOCAL_MEM_NODE,$CXL_MEM_NODE"
policies["Weighted-2:1"]="--weighted-interleave=$LOCAL_MEM_NODE,$CXL_MEM_NODE"; policy_node_map["Weighted-2:1"]="$LOCAL_MEM_NODE,$CXL_MEM_NODE"
policies["Weighted-3:1"]="--weighted-interleave=$LOCAL_MEM_NODE,$CXL_MEM_NODE"; policy_node_map["Weighted-3:1"]="$LOCAL_MEM_NODE,$CXL_MEM_NODE"
policies["Weighted-4:1"]="--weighted-interleave=$LOCAL_MEM_NODE,$CXL_MEM_NODE"; policy_node_map["Weighted-4:1"]="$LOCAL_MEM_NODE,$CXL_MEM_NODE"
policies["Weighted-5:1"]="--weighted-interleave=$LOCAL_MEM_NODE,$CXL_MEM_NODE"; policy_node_map["Weighted-5:1"]="$LOCAL_MEM_NODE,$CXL_MEM_NODE"

# PHASE 1
echo "========================================================================"
echo "== PHASE 1: Testing Memory Ratios with Fixed Threads ($FIXED_THREADS_FOR_RATIO_TEST)"
echo "========================================================================"
for scenario_name in "${!scenarios[@]}"; do
    IFS=';' read -r demotion bal reclaim <<< "${scenarios[$scenario_name]}"; set_scenario "$demotion" "$bal" "$reclaim"
    for policy_name in "${policy_order[@]}"; do
        numactl_options=${policies[$policy_name]}; policy_nodes=${policy_node_map[$policy_name]}
        for i in $(seq 1 $NUM_RUNS); do
            run_single_trial "$scenario_name" "$policy_name" "$numactl_options" "$FIXED_THREADS_FOR_RATIO_TEST" "$i" "$PAGE_SIZE" "$policy_nodes"
        done
    done
done

# PHASE 2
echo "========================================================================"
echo "== PHASE 2: Testing Thread Scaling with Fixed Memory Policy ($FIXED_RATIO_FOR_SCALING_TEST)"
echo "========================================================================"
fixed_policy_name=$FIXED_RATIO_FOR_SCALING_TEST; numactl_options=${policies[$fixed_policy_name]}; policy_nodes=${policy_node_map[$fixed_policy_name]}
for scenario_name in "${!scenarios[@]}"; do
    IFS=';' read -r demotion bal reclaim <<< "${scenarios[$scenario_name]}"; set_scenario "$demotion" "$bal" "$reclaim"
    for threads in $THREAD_LIST_FOR_SCALING_TEST; do
        for i in $(seq 1 $NUM_RUNS); do
            run_single_trial "$scenario_name" "$fixed_policy_name" "$numactl_options" "$threads" "$i" "$PAGE_SIZE" "$policy_nodes"
        done
    done
done

# Cleanup
echo; echo "========================================================================"
echo "==  All tests complete. Restoring original system settings..."
if [ "$ORIGINAL_DEMOTION" != "N/A" ]; then echo "$ORIGINAL_DEMOTION" > "/sys/kernel/mm/numa/demotion_enabled"; fi
if [ "$ORIGINAL_BALANCING" != "N/A" ]; then echo "$ORIGINAL_BALANCING" > "/proc/sys/kernel/numa_balancing"; fi
if [ "$ORIGINAL_RECLAIM" != "N/A" ]; then echo "$ORIGINAL_RECLAIM" > "/proc/sys/vm/zone_reclaim_mode"; fi
echo "==  System settings restored."; echo "==  Detailed results saved to $OUTPUT_CSV"
echo "========================================================================"