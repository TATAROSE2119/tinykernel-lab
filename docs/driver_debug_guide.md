# IMX6ULL 驱动调试工具开发指南

本文档描述如何在 **IMX6ULL 开发板 (Linux 4.1.15)** 上构建实用的驱动调试工具，用于分析本项目中的各类驱动模块。

---

## 项目背景

| 项目 | 说明 |
|------|------|
| 目标平台 | 正点原子 IMX6ULL 开发板 |
| 目标内核 | Linux 4.1.15 |
| 目标架构 | ARM Cortex-A7 (armv7l) |
| 开发主机 | Ubuntu 16.04 x86_64 |

---

## 调试工具选型

Linux 4.1.15 内核支持以下调试技术，可直接在目标板上运行：

| 工具 | 支持情况 | 特点 | 适用场景 |
|------|----------|------|----------|
| **ftrace** | 内核原生 | 零依赖、低开销 | 函数跟踪、延迟分析 |
| **kprobes** | 内核原生 | 动态探针、无需重编译 | 任意内核函数插桩 |
| **perf** | 内核原生 | 硬件性能计数器 | CPU热点、缓存分析 |
| **trace-cmd** | 用户态工具 | ftrace易用前端 | 快速抓取和分析 |
| **LTTng** | 需编译安装 | 专业级、低开销 | 长时间跟踪、复杂分析 |
| **debugfs** | 内核原生 | 自定义调试接口 | 驱动内部状态导出 |
| **printk** | 内核原生 | 最简单 | 快速定位、临时调试 |

---

## 调试架构

```
┌─────────────────────────────────────────────────────────────────┐
│                    IMX6ULL 开发板                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                  Linux 4.1.15 内核                       │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐     │   │
│  │  │   ftrace    │  │   kprobes   │  │    perf     │     │   │
│  │  │  tracepoint │  │  动态探针    │  │  性能计数器  │     │   │
│  │  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘     │   │
│  │         │                │                │             │   │
│  │         └────────────────┼────────────────┘             │   │
│  │                          ▼                              │   │
│  │  ┌─────────────────────────────────────────────────┐   │   │
│  │  │              项目驱动模块                         │   │   │
│  │  │  LED | INPUT_KEY | AP3216C | ICM20608 | ramdisk │   │   │
│  │  └─────────────────────────────────────────────────┘   │   │
│  └─────────────────────────────────────────────────────────┘   │
│                              │                                  │
│                              ▼                                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │              用户态调试工具                              │   │
│  │  trace-cmd | perf | 自定义分析脚本 | debugfs接口        │   │
│  └─────────────────────────────────────────────────────────┘   │
└───────────────────────────────┬─────────────────────────────────┘
                                │ NFS / 串口 / SSH
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Ubuntu 开发主机                               │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  trace数据分析 | 可视化脚本 | kernelshark | 报告生成     │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

---

## 1. 内核配置要求

### 1.1 检查/启用调试选项

确保目标板内核开启以下配置：

```bash
# 在内核源码目录检查 .config
# 或在目标板: zcat /proc/config.gz | grep CONFIG_xxx

# ftrace 相关 (必须)
CONFIG_FTRACE=y
CONFIG_FUNCTION_TRACER=y
CONFIG_FUNCTION_GRAPH_TRACER=y
CONFIG_DYNAMIC_FTRACE=y
CONFIG_STACK_TRACER=y
CONFIG_IRQSOFF_TRACER=y
CONFIG_PREEMPT_TRACER=y
CONFIG_SCHED_TRACER=y

# kprobes 相关 (必须)
CONFIG_KPROBES=y
CONFIG_KPROBE_EVENTS=y
CONFIG_KRETPROBES=y

# perf 相关 (推荐)
CONFIG_PERF_EVENTS=y
CONFIG_HW_PERF_EVENTS=y

# debugfs (必须)
CONFIG_DEBUG_FS=y

# 符号信息 (推荐，便于分析)
CONFIG_KALLSYMS=y
CONFIG_KALLSYMS_ALL=y
```

### 1.2 重新编译内核 (如需要)

```bash
cd /path/to/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek

# 修改配置
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- menuconfig
# Kernel hacking → Tracers → 启用上述选项

# 编译
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -j$(nproc)

# 部署到开发板
cp arch/arm/boot/zImage /path/to/tftp/
```

---

## 2. 工具目录结构

```
debug_tools/
├── ftrace/
│   ├── trace_spi.sh           # SPI通信跟踪
│   ├── trace_i2c.sh           # I2C通信跟踪
│   ├── trace_irq.sh           # 中断延迟分析
│   ├── trace_blk.sh           # 块设备I/O跟踪
│   ├── trace_gpio.sh          # GPIO操作跟踪
│   └── trace_func.sh          # 通用函数跟踪
├── kprobe/
│   ├── kprobe_spi.sh          # SPI动态探针
│   ├── kprobe_latency.sh      # 延迟测量探针
│   └── README.md
├── perf/
│   ├── perf_record.sh         # 性能采样
│   └── perf_stat.sh           # 性能统计
├── debugfs/
│   ├── drv_debug.h            # debugfs辅助宏
│   └── example_usage.c        # 使用示例
├── analysis/
│   ├── parse_ftrace.py        # ftrace数据解析
│   ├── plot_latency.py        # 延迟图表生成
│   └── report_gen.py          # 报告生成
├── install_tools.sh           # 目标板工具安装
└── README.md
```

---

## 3. ftrace 调试工具

ftrace 是 Linux 内核内置的跟踪框架，无需安装额外软件。

### 3.1 基础操作

```bash
# 挂载 debugfs (如未挂载)
mount -t debugfs none /sys/kernel/debug

# ftrace 控制目录
TRACE=/sys/kernel/debug/tracing

# 查看可用跟踪器
cat $TRACE/available_tracers
# 输出: function_graph function nop

# 查看可用跟踪事件
cat $TRACE/available_events | head -50
```

### 3.2 SPI 通信跟踪 (trace_spi.sh)

```bash
#!/bin/bash
# trace_spi.sh - 跟踪 SPI 总线通信
# 用于调试 ICM-20608 驱动

TRACE=/sys/kernel/debug/tracing
OUTPUT=/tmp/spi_trace.txt

usage() {
    echo "用法: $0 [start|stop|show|save]"
    echo "  start  - 开始跟踪"
    echo "  stop   - 停止跟踪"
    echo "  show   - 显示跟踪结果"
    echo "  save   - 保存到文件"
}

start_trace() {
    echo "配置 SPI 跟踪..."

    # 清空之前的跟踪
    echo > $TRACE/trace

    # 使用 function_graph 跟踪器 (显示调用关系和耗时)
    echo function_graph > $TRACE/current_tracer

    # 设置跟踪深度
    echo 10 > $TRACE/max_graph_depth

    # 过滤 SPI 相关函数
    echo "spi_sync" > $TRACE/set_graph_function
    echo "spi_transfer_one_message" >> $TRACE/set_graph_function
    echo "spi_pump_messages" >> $TRACE/set_graph_function

    # 可选: 跟踪特定模块的函数
    # echo ":mod:spi_imx" > $TRACE/set_ftrace_filter

    # 开启跟踪
    echo 1 > $TRACE/tracing_on

    echo "SPI 跟踪已启动"
    echo "请运行测试程序，然后执行: $0 stop"
}

stop_trace() {
    echo 0 > $TRACE/tracing_on
    echo "跟踪已停止"
}

show_trace() {
    cat $TRACE/trace
}

save_trace() {
    cat $TRACE/trace > $OUTPUT
    echo "跟踪数据已保存到: $OUTPUT"

    # 显示统计信息
    echo ""
    echo "=== 统计信息 ==="
    echo "SPI 传输次数: $(grep -c 'spi_sync()' $OUTPUT)"
    echo "平均耗时统计:"
    grep -E '[0-9.]+ us.*spi_sync' $OUTPUT | \
        awk '{sum+=$1; count++} END {if(count>0) printf "  spi_sync: %.2f us (共 %d 次)\n", sum/count, count}'
}

case "$1" in
    start) start_trace ;;
    stop)  stop_trace ;;
    show)  show_trace ;;
    save)  save_trace ;;
    *)     usage ;;
esac
```

### 3.3 I2C 通信跟踪 (trace_i2c.sh)

```bash
#!/bin/bash
# trace_i2c.sh - 跟踪 I2C 总线通信
# 用于调试 AP3216C 驱动

TRACE=/sys/kernel/debug/tracing
OUTPUT=/tmp/i2c_trace.txt

start_trace() {
    echo "配置 I2C 跟踪..."

    echo > $TRACE/trace
    echo function_graph > $TRACE/current_tracer
    echo 8 > $TRACE/max_graph_depth

    # I2C 核心函数
    echo "i2c_transfer" > $TRACE/set_graph_function
    echo "i2c_smbus_xfer" >> $TRACE/set_graph_function
    echo "i2c_smbus_read_byte_data" >> $TRACE/set_graph_function
    echo "i2c_smbus_write_byte_data" >> $TRACE/set_graph_function

    # IMX I2C 控制器驱动函数 (可选)
    # echo "i2c_imx_xfer" >> $TRACE/set_graph_function

    echo 1 > $TRACE/tracing_on
    echo "I2C 跟踪已启动"
}

stop_trace() {
    echo 0 > $TRACE/tracing_on
    echo "跟踪已停止"
}

show_trace() {
    cat $TRACE/trace | head -200
}

save_trace() {
    cat $TRACE/trace > $OUTPUT
    echo "跟踪数据已保存到: $OUTPUT"

    # 分析 I2C 传输
    echo ""
    echo "=== I2C 传输统计 ==="
    echo "i2c_transfer 调用次数: $(grep -c 'i2c_transfer()' $OUTPUT)"
    grep -E '[0-9.]+ us.*i2c_transfer\(\)' $OUTPUT | \
        awk '{sum+=$1; count++} END {if(count>0) printf "平均耗时: %.2f us\n", sum/count}'
}

case "$1" in
    start) start_trace ;;
    stop)  stop_trace ;;
    show)  show_trace ;;
    save)  save_trace ;;
    *)     echo "用法: $0 [start|stop|show|save]" ;;
esac
```

### 3.4 中断延迟分析 (trace_irq.sh)

```bash
#!/bin/bash
# trace_irq.sh - 分析中断延迟
# 用于调试 INPUT_KEY 驱动的响应时间

TRACE=/sys/kernel/debug/tracing
OUTPUT=/tmp/irq_trace.txt

trace_irqsoff() {
    echo "跟踪中断关闭时间..."

    echo > $TRACE/trace

    # irqsoff: 跟踪中断被关闭的最长时间
    echo irqsoff > $TRACE/current_tracer

    # 设置阈值 (微秒)，只记录超过此值的
    echo 100 > $TRACE/tracing_thresh

    echo 1 > $TRACE/tracing_on
    echo "中断延迟跟踪已启动 (阈值: 100us)"
    echo "请触发按键中断，然后执行: $0 stop"
}

trace_irq_handler() {
    echo "跟踪中断处理函数..."

    echo > $TRACE/trace
    echo function_graph > $TRACE/current_tracer

    # 跟踪中断相关函数
    echo "handle_irq_event" > $TRACE/set_graph_function
    echo "handle_edge_irq" >> $TRACE/set_graph_function
    echo "gpio_keys_gpio_isr" >> $TRACE/set_graph_function  # 通用GPIO按键

    # 如果知道具体的中断处理函数名
    # echo "key0_handler" >> $TRACE/set_graph_function

    echo 1 > $TRACE/tracing_on
    echo "中断处理跟踪已启动"
}

trace_events() {
    echo "使用 tracepoint 跟踪中断事件..."

    echo > $TRACE/trace
    echo nop > $TRACE/current_tracer

    # 启用中断相关 tracepoint
    echo 1 > $TRACE/events/irq/irq_handler_entry/enable
    echo 1 > $TRACE/events/irq/irq_handler_exit/enable
    echo 1 > $TRACE/events/irq/softirq_entry/enable
    echo 1 > $TRACE/events/irq/softirq_exit/enable

    echo 1 > $TRACE/tracing_on
    echo "中断事件跟踪已启动"
}

stop_trace() {
    echo 0 > $TRACE/tracing_on

    # 关闭 tracepoint
    echo 0 > $TRACE/events/irq/irq_handler_entry/enable 2>/dev/null
    echo 0 > $TRACE/events/irq/irq_handler_exit/enable 2>/dev/null
    echo 0 > $TRACE/events/irq/softirq_entry/enable 2>/dev/null
    echo 0 > $TRACE/events/irq/softirq_exit/enable 2>/dev/null

    echo "跟踪已停止"
}

analyze() {
    cat $TRACE/trace > $OUTPUT

    echo "=== 中断延迟分析 ==="

    # 提取中断处理时间
    if grep -q "irq_handler_entry" $OUTPUT; then
        echo "中断处理统计:"
        grep "irq_handler" $OUTPUT | head -20
    fi

    # 如果是 irqsoff 跟踪器
    if grep -q "latency" $OUTPUT; then
        echo "最大中断关闭延迟:"
        grep "latency" $OUTPUT | head -5
    fi
}

case "$1" in
    irqsoff) trace_irqsoff ;;
    handler) trace_irq_handler ;;
    events)  trace_events ;;
    stop)    stop_trace ;;
    show)    cat $TRACE/trace ;;
    analyze) analyze ;;
    *)
        echo "用法: $0 [irqsoff|handler|events|stop|show|analyze]"
        echo "  irqsoff - 跟踪中断关闭时间"
        echo "  handler - 跟踪中断处理函数"
        echo "  events  - 使用tracepoint跟踪"
        ;;
esac
```

### 3.5 块设备 I/O 跟踪 (trace_blk.sh)

```bash
#!/bin/bash
# trace_blk.sh - 跟踪块设备 I/O
# 用于调试 ramdisk 驱动

TRACE=/sys/kernel/debug/tracing
OUTPUT=/tmp/blk_trace.txt

start_trace() {
    echo "配置块设备跟踪..."

    echo > $TRACE/trace
    echo nop > $TRACE/current_tracer

    # 启用块设备 tracepoint
    echo 1 > $TRACE/events/block/block_rq_issue/enable
    echo 1 > $TRACE/events/block/block_rq_complete/enable
    echo 1 > $TRACE/events/block/block_bio_queue/enable
    echo 1 > $TRACE/events/block/block_bio_complete/enable

    # 可选: 过滤特定设备
    # echo 'dev == 259:0' > $TRACE/events/block/block_rq_issue/filter

    echo 1 > $TRACE/tracing_on
    echo "块设备跟踪已启动"
}

stop_trace() {
    echo 0 > $TRACE/tracing_on
    echo 0 > $TRACE/events/block/block_rq_issue/enable
    echo 0 > $TRACE/events/block/block_rq_complete/enable
    echo 0 > $TRACE/events/block/block_bio_queue/enable
    echo 0 > $TRACE/events/block/block_bio_complete/enable
    echo "跟踪已停止"
}

analyze() {
    cat $TRACE/trace > $OUTPUT

    echo "=== 块I/O统计 ==="
    echo "请求发出数: $(grep -c 'block_rq_issue' $OUTPUT)"
    echo "请求完成数: $(grep -c 'block_rq_complete' $OUTPUT)"

    echo ""
    echo "=== 最近的I/O请求 ==="
    grep -E 'block_rq_(issue|complete)' $OUTPUT | tail -20
}

case "$1" in
    start)   start_trace ;;
    stop)    stop_trace ;;
    show)    cat $TRACE/trace ;;
    analyze) analyze ;;
    *)       echo "用法: $0 [start|stop|show|analyze]" ;;
esac
```

### 3.6 GPIO 操作跟踪 (trace_gpio.sh)

```bash
#!/bin/bash
# trace_gpio.sh - 跟踪 GPIO 操作
# 用于调试 LED 驱动

TRACE=/sys/kernel/debug/tracing
OUTPUT=/tmp/gpio_trace.txt

start_trace() {
    echo "配置 GPIO 跟踪..."

    echo > $TRACE/trace
    echo function_graph > $TRACE/current_tracer

    # GPIO 操作函数
    echo "gpio_set_value" > $TRACE/set_graph_function
    echo "gpio_get_value" >> $TRACE/set_graph_function
    echo "gpio_direction_output" >> $TRACE/set_graph_function
    echo "gpio_direction_input" >> $TRACE/set_graph_function
    echo "gpiod_set_value" >> $TRACE/set_graph_function
    echo "gpiod_get_value" >> $TRACE/set_graph_function

    echo 1 > $TRACE/tracing_on
    echo "GPIO 跟踪已启动"
}

stop_trace() {
    echo 0 > $TRACE/tracing_on
    echo "跟踪已停止"
}

analyze() {
    cat $TRACE/trace > $OUTPUT

    echo "=== GPIO 操作统计 ==="
    echo "gpio_set_value 调用: $(grep -c 'gpio_set_value' $OUTPUT)"
    echo "gpio_get_value 调用: $(grep -c 'gpio_get_value' $OUTPUT)"

    echo ""
    echo "=== GPIO 操作耗时 ==="
    grep -E '[0-9.]+ us.*gpio_set_value' $OUTPUT | head -10
}

case "$1" in
    start)   start_trace ;;
    stop)    stop_trace ;;
    show)    cat $TRACE/trace ;;
    analyze) analyze ;;
    *)       echo "用法: $0 [start|stop|show|analyze]" ;;
esac
```

### 3.7 通用函数跟踪 (trace_func.sh)

```bash
#!/bin/bash
# trace_func.sh - 通用函数跟踪工具
# 支持跟踪任意内核函数

TRACE=/sys/kernel/debug/tracing

usage() {
    echo "用法: $0 <命令> [参数]"
    echo ""
    echo "命令:"
    echo "  list <pattern>      - 列出匹配的可跟踪函数"
    echo "  trace <func,...>    - 跟踪指定函数"
    echo "  module <mod_name>   - 跟踪整个模块"
    echo "  stop                - 停止跟踪"
    echo "  show                - 显示跟踪结果"
    echo "  clear               - 清空跟踪缓冲区"
    echo ""
    echo "示例:"
    echo "  $0 list 'spi_*'"
    echo "  $0 trace spi_sync,spi_transfer"
    echo "  $0 module spi_imx"
}

list_funcs() {
    local pattern=$1
    echo "可跟踪的函数 (匹配: $pattern):"
    cat $TRACE/available_filter_functions | grep -E "$pattern" | head -50
}

trace_funcs() {
    local funcs=$1

    echo > $TRACE/trace
    echo function_graph > $TRACE/current_tracer
    echo 10 > $TRACE/max_graph_depth

    # 清空过滤器
    echo > $TRACE/set_graph_function

    # 添加函数
    IFS=',' read -ra FUNC_ARRAY <<< "$funcs"
    for func in "${FUNC_ARRAY[@]}"; do
        func=$(echo $func | tr -d ' ')
        if grep -q "^${func}$" $TRACE/available_filter_functions; then
            echo "$func" >> $TRACE/set_graph_function
            echo "  添加: $func"
        else
            echo "  警告: $func 不可跟踪"
        fi
    done

    echo 1 > $TRACE/tracing_on
    echo "跟踪已启动"
}

trace_module() {
    local mod=$1

    echo > $TRACE/trace
    echo function > $TRACE/current_tracer

    # 跟踪模块中的所有函数
    echo ":mod:$mod" > $TRACE/set_ftrace_filter

    echo 1 > $TRACE/tracing_on
    echo "正在跟踪模块: $mod"
}

stop_trace() {
    echo 0 > $TRACE/tracing_on
    echo "跟踪已停止"
}

show_trace() {
    cat $TRACE/trace
}

clear_trace() {
    echo > $TRACE/trace
    echo "跟踪缓冲区已清空"
}

case "$1" in
    list)   list_funcs "$2" ;;
    trace)  trace_funcs "$2" ;;
    module) trace_module "$2" ;;
    stop)   stop_trace ;;
    show)   show_trace ;;
    clear)  clear_trace ;;
    *)      usage ;;
esac
```

---

## 4. kprobes 动态探针

kprobes 允许在任意内核函数入口/出口插入探针，无需重新编译内核。

### 4.1 通过 debugfs 使用 kprobe

```bash
#!/bin/bash
# kprobe_spi.sh - 使用 kprobe 跟踪 SPI 传输

TRACE=/sys/kernel/debug/tracing

# 添加 kprobe: spi_sync 入口
add_kprobe() {
    # 格式: p[:[GRP/]EVENT] [MOD:]SYMBOL[+offs] [FETCHARGS]
    # p = probe (入口), r = retprobe (返回)

    # 跟踪 spi_sync 入口，记录第一个参数 (spi_device 指针)
    echo 'p:spi_entry spi_sync dev=%x0' > $TRACE/kprobe_events

    # 跟踪 spi_sync 返回，记录返回值
    echo 'r:spi_exit spi_sync ret=$retval' >> $TRACE/kprobe_events

    # 启用探针
    echo 1 > $TRACE/events/kprobes/spi_entry/enable
    echo 1 > $TRACE/events/kprobes/spi_exit/enable

    echo "kprobe 已添加"
}

# 添加 kprobe: i2c_transfer
add_i2c_kprobe() {
    echo 'p:i2c_entry i2c_transfer adap=%x0 msgs=%x1 num=%x2' > $TRACE/kprobe_events
    echo 'r:i2c_exit i2c_transfer ret=$retval' >> $TRACE/kprobe_events

    echo 1 > $TRACE/events/kprobes/i2c_entry/enable
    echo 1 > $TRACE/events/kprobes/i2c_exit/enable

    echo "I2C kprobe 已添加"
}

# 删除所有 kprobe
remove_kprobes() {
    echo 0 > $TRACE/events/kprobes/enable 2>/dev/null
    echo > $TRACE/kprobe_events
    echo "所有 kprobe 已删除"
}

# 显示当前 kprobe
show_kprobes() {
    echo "=== 当前 kprobe 事件 ==="
    cat $TRACE/kprobe_events
    echo ""
    echo "=== kprobe 跟踪输出 ==="
    cat $TRACE/trace | grep -E 'spi_|i2c_' | tail -30
}

start_trace() {
    echo > $TRACE/trace
    echo 1 > $TRACE/tracing_on
    echo "跟踪已启动"
}

stop_trace() {
    echo 0 > $TRACE/tracing_on
    echo "跟踪已停止"
}

case "$1" in
    spi)     add_kprobe ;;
    i2c)     add_i2c_kprobe ;;
    remove)  remove_kprobes ;;
    show)    show_kprobes ;;
    start)   start_trace ;;
    stop)    stop_trace ;;
    *)
        echo "用法: $0 [spi|i2c|remove|show|start|stop]"
        ;;
esac
```

### 4.2 测量函数延迟

```bash
#!/bin/bash
# kprobe_latency.sh - 使用 kprobe 测量函数延迟

TRACE=/sys/kernel/debug/tracing
FUNC=${1:-spi_sync}

setup_latency_probe() {
    # 清空
    echo > $TRACE/kprobe_events
    echo > $TRACE/trace

    # 记录入口时间戳
    echo "p:${FUNC}_entry ${FUNC}" > $TRACE/kprobe_events

    # 记录返回时间戳和返回值
    echo "r:${FUNC}_exit ${FUNC} ret=\$retval" >> $TRACE/kprobe_events

    # 启用
    echo 1 > $TRACE/events/kprobes/${FUNC}_entry/enable
    echo 1 > $TRACE/events/kprobes/${FUNC}_exit/enable

    echo 1 > $TRACE/tracing_on

    echo "延迟测量已启动: $FUNC"
    echo "运行测试后，执行: $0 analyze"
}

analyze_latency() {
    echo 0 > $TRACE/tracing_on

    echo "=== $FUNC 延迟分析 ==="

    # 提取时间戳并计算延迟
    cat $TRACE/trace | awk '
    /_entry:/ {
        split($0, a, ":");
        split(a[1], t, " ");
        entry_time = t[length(t)];
        gsub(/[^0-9.]/, "", entry_time);
    }
    /_exit:/ {
        split($0, a, ":");
        split(a[1], t, " ");
        exit_time = t[length(t)];
        gsub(/[^0-9.]/, "", exit_time);
        if (entry_time > 0) {
            latency = (exit_time - entry_time) * 1000000;  # 转微秒
            printf "延迟: %.3f us\n", latency;
            sum += latency;
            count++;
            if (latency > max) max = latency;
            if (min == 0 || latency < min) min = latency;
        }
    }
    END {
        if (count > 0) {
            printf "\n统计: 次数=%d, 平均=%.3f us, 最小=%.3f us, 最大=%.3f us\n",
                   count, sum/count, min, max;
        }
    }
    '
}

case "$1" in
    analyze) analyze_latency ;;
    *)       setup_latency_probe ;;
esac
```

---

## 5. perf 性能分析

perf 利用硬件性能计数器进行分析，适合找 CPU 热点。

### 5.1 安装 perf

```bash
# 交叉编译 perf (在开发主机)
cd /path/to/linux-4.1.15/tools/perf
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
# 复制到目标板
scp perf root@target:/usr/bin/
```

### 5.2 基本使用

```bash
#!/bin/bash
# perf_record.sh - 性能采样

# 采样整个系统 10 秒
perf_system() {
    perf record -a -g -o /tmp/perf.data -- sleep 10
    echo "采样完成，使用 'perf report' 查看"
}

# 采样特定进程
perf_process() {
    local pid=$1
    perf record -p $pid -g -o /tmp/perf.data -- sleep 10
}

# 采样特定命令
perf_command() {
    local cmd="$@"
    perf record -g -o /tmp/perf.data -- $cmd
}

# 查看报告
perf_report() {
    perf report -i /tmp/perf.data
}

# CPU 周期统计
perf_stat() {
    local cmd="$@"
    perf stat -e cycles,instructions,cache-misses,branch-misses -- $cmd
}

case "$1" in
    system)  perf_system ;;
    process) perf_process $2 ;;
    command) shift; perf_command "$@" ;;
    report)  perf_report ;;
    stat)    shift; perf_stat "$@" ;;
    *)
        echo "用法:"
        echo "  $0 system              - 采样整个系统"
        echo "  $0 process <pid>       - 采样指定进程"
        echo "  $0 command <cmd>       - 采样指定命令"
        echo "  $0 report              - 查看报告"
        echo "  $0 stat <cmd>          - CPU统计"
        ;;
esac
```

### 5.3 分析驱动性能

```bash
# 分析 myctl 读取传感器的性能热点
perf record -g -- /usr/bin/myctl icm20608
perf report

# 输出示例:
# Samples: 1K of event 'cycles'
# Overhead  Command  Shared Object     Symbol
#   25.30%  myctl    [kernel.kallsyms] [k] spi_sync
#   18.45%  myctl    [kernel.kallsyms] [k] __spi_pump_messages
#   12.20%  myctl    [kernel.kallsyms] [k] spi_imx_transfer
#    8.15%  myctl    libc.so.6         [.] read
```

---

## 6. trace-cmd 工具

trace-cmd 是 ftrace 的用户态前端，使用更方便。

### 6.1 安装 trace-cmd

```bash
# 在开发主机交叉编译
git clone https://git.kernel.org/pub/scm/utils/trace-cmd/trace-cmd.git
cd trace-cmd
make CROSS_COMPILE=arm-linux-gnueabihf-

# 复制到目标板
scp trace-cmd root@target:/usr/bin/
```

### 6.2 使用示例

```bash
# 跟踪 SPI 函数
trace-cmd record -p function_graph -g spi_sync -g spi_transfer -- /usr/bin/myctl icm20608

# 跟踪 I2C 函数
trace-cmd record -p function_graph -g i2c_transfer -- /usr/bin/myctl ap3216c

# 跟踪事件
trace-cmd record -e block:* -- dd if=/dev/ramdisk of=/dev/null bs=4k count=100

# 查看结果
trace-cmd report

# 导出给主机分析 (生成 trace.dat)
trace-cmd record -o /tmp/trace.dat -p function_graph -g spi_sync
# 在主机用 kernelshark 可视化查看
```

---

## 7. 驱动内嵌 debugfs 调试接口

在驱动中添加 debugfs 接口，导出内部状态。

### 7.1 debugfs 辅助头文件 (drv_debug.h)

```c
/* drv_debug.h - debugfs 调试辅助宏 */
#ifndef __DRV_DEBUG_H__
#define __DRV_DEBUG_H__

#include <linux/debugfs.h>
#include <linux/seq_file.h>

/* 统计结构 */
struct drv_stats {
    u64 read_count;
    u64 write_count;
    u64 read_bytes;
    u64 write_bytes;
    u64 read_time_ns;   /* 累计读取耗时 */
    u64 write_time_ns;  /* 累计写入耗时 */
    u64 error_count;
    u64 last_error;
};

/* 时间测量宏 */
#define DRV_TIME_START() \
    u64 __start_time = ktime_get_ns()

#define DRV_TIME_END(stats, field) \
    do { (stats)->field += ktime_get_ns() - __start_time; } while(0)

/* 简单计数器 debugfs 文件 */
#define DEFINE_DEBUGFS_U64(name) \
    static int name##_show(struct seq_file *s, void *unused) { \
        seq_printf(s, "%llu\n", *(u64 *)s->private); \
        return 0; \
    } \
    static int name##_open(struct inode *inode, struct file *file) { \
        return single_open(file, name##_show, inode->i_private); \
    } \
    static const struct file_operations name##_fops = { \
        .open = name##_open, \
        .read = seq_read, \
        .llseek = seq_lseek, \
        .release = single_release, \
    }

#endif /* __DRV_DEBUG_H__ */
```

### 7.2 在驱动中使用 (示例)

```c
/* 以 SPI ICM20608 驱动为例，添加 debugfs 调试接口 */

#include <linux/debugfs.h>
#include "drv_debug.h"

struct icm20608_dev {
    /* ... 原有字段 ... */

    /* debugfs */
    struct dentry *debug_dir;
    struct drv_stats stats;
};

/* 统计信息显示 */
static int icm20608_stats_show(struct seq_file *s, void *unused)
{
    struct icm20608_dev *dev = s->private;
    u64 avg_read_us = 0;

    if (dev->stats.read_count > 0)
        avg_read_us = dev->stats.read_time_ns / dev->stats.read_count / 1000;

    seq_printf(s, "=== ICM20608 统计信息 ===\n");
    seq_printf(s, "读取次数:     %llu\n", dev->stats.read_count);
    seq_printf(s, "读取字节:     %llu\n", dev->stats.read_bytes);
    seq_printf(s, "平均读取耗时: %llu us\n", avg_read_us);
    seq_printf(s, "错误次数:     %llu\n", dev->stats.error_count);
    seq_printf(s, "最后错误码:   %lld\n", (s64)dev->stats.last_error);

    return 0;
}

static int icm20608_stats_open(struct inode *inode, struct file *file)
{
    return single_open(file, icm20608_stats_show, inode->i_private);
}

static const struct file_operations icm20608_stats_fops = {
    .open    = icm20608_stats_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

/* 寄存器转储 */
static int icm20608_regs_show(struct seq_file *s, void *unused)
{
    struct icm20608_dev *dev = s->private;
    u8 reg_val;
    int i;

    seq_printf(s, "=== ICM20608 寄存器 ===\n");
    seq_printf(s, "ADDR  VALUE\n");

    /* 读取关键寄存器 */
    for (i = 0; i < 0x80; i += 0x10) {
        reg_val = icm20608_read_reg(dev, i);
        seq_printf(s, "0x%02X: 0x%02X\n", i, reg_val);
    }

    return 0;
}

static int icm20608_regs_open(struct inode *inode, struct file *file)
{
    return single_open(file, icm20608_regs_show, inode->i_private);
}

static const struct file_operations icm20608_regs_fops = {
    .open    = icm20608_regs_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

/* 初始化 debugfs */
static int icm20608_debugfs_init(struct icm20608_dev *dev)
{
    dev->debug_dir = debugfs_create_dir("icm20608", NULL);
    if (!dev->debug_dir)
        return -ENOMEM;

    debugfs_create_file("stats", 0444, dev->debug_dir, dev, &icm20608_stats_fops);
    debugfs_create_file("registers", 0444, dev->debug_dir, dev, &icm20608_regs_fops);
    debugfs_create_u64("read_count", 0444, dev->debug_dir, &dev->stats.read_count);
    debugfs_create_u64("error_count", 0444, dev->debug_dir, &dev->stats.error_count);

    return 0;
}

/* 清理 debugfs */
static void icm20608_debugfs_exit(struct icm20608_dev *dev)
{
    debugfs_remove_recursive(dev->debug_dir);
}

/* 在 read 函数中添加统计 */
static ssize_t icm20608_read(struct file *filp, char __user *buf,
                             size_t cnt, loff_t *off)
{
    struct icm20608_dev *dev = filp->private_data;
    int ret;

    DRV_TIME_START();

    /* ... 原有读取逻辑 ... */
    ret = icm20608_read_sensor_data(dev);

    DRV_TIME_END(&dev->stats, read_time_ns);
    dev->stats.read_count++;
    dev->stats.read_bytes += cnt;

    if (ret < 0) {
        dev->stats.error_count++;
        dev->stats.last_error = ret;
    }

    return ret;
}
```

### 7.3 使用 debugfs 调试

```bash
# 挂载 debugfs
mount -t debugfs none /sys/kernel/debug

# 查看驱动统计
cat /sys/kernel/debug/icm20608/stats

# 输出:
# === ICM20608 统计信息 ===
# 读取次数:     1523
# 读取字节:     42644
# 平均读取耗时: 45 us
# 错误次数:     3
# 最后错误码:   -110

# 查看寄存器值
cat /sys/kernel/debug/icm20608/registers

# 实时监控读取次数
watch -n 1 cat /sys/kernel/debug/icm20608/read_count
```

---

## 8. 主机端分析工具

### 8.1 ftrace 数据解析 (parse_ftrace.py)

```python
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
parse_ftrace.py - 解析 ftrace 输出数据

用法:
  ./parse_ftrace.py <trace_file> [--func <function>] [--hist]
"""

import sys
import re
import argparse
from collections import defaultdict

def parse_function_graph(filename):
    """解析 function_graph 格式的跟踪数据"""
    functions = defaultdict(list)

    with open(filename, 'r') as f:
        for line in f:
            # 匹配格式: " 0)   1.234 us    |      func_name();"
            match = re.search(r'(\d+\.\d+)\s+us\s+\|.*?(\w+)\(\)', line)
            if match:
                duration = float(match.group(1))
                func_name = match.group(2)
                functions[func_name].append(duration)

    return functions

def parse_tracepoint(filename):
    """解析 tracepoint 格式的跟踪数据"""
    events = []

    with open(filename, 'r') as f:
        for line in f:
            # 跳过注释行
            if line.startswith('#'):
                continue

            # 解析事件行
            # 格式: <task>-<pid> [<cpu>] <timestamp>: <event>: <data>
            match = re.match(r'\s*(.+?)-(\d+)\s+\[(\d+)\]\s+(\d+\.\d+):\s+(\w+):\s+(.*)', line)
            if match:
                events.append({
                    'task': match.group(1),
                    'pid': int(match.group(2)),
                    'cpu': int(match.group(3)),
                    'timestamp': float(match.group(4)),
                    'event': match.group(5),
                    'data': match.group(6)
                })

    return events

def print_function_stats(functions, target_func=None):
    """打印函数统计信息"""
    print("%-40s %8s %10s %10s %10s" %
          ("FUNCTION", "COUNT", "AVG(us)", "MIN(us)", "MAX(us)"))
    print("-" * 80)

    for func, durations in sorted(functions.items()):
        if target_func and func != target_func:
            continue
        if durations:
            avg = sum(durations) / len(durations)
            print("%-40s %8d %10.2f %10.2f %10.2f" %
                  (func, len(durations), avg, min(durations), max(durations)))

def print_histogram(durations, func_name, bins=10):
    """打印延迟直方图"""
    if not durations:
        return

    min_val, max_val = min(durations), max(durations)
    bin_width = (max_val - min_val) / bins if max_val > min_val else 1

    histogram = defaultdict(int)
    for d in durations:
        bin_idx = int((d - min_val) / bin_width)
        if bin_idx >= bins:
            bin_idx = bins - 1
        histogram[bin_idx] += 1

    print(f"\n=== {func_name} 延迟分布 ===")
    max_count = max(histogram.values()) if histogram else 1

    for i in range(bins):
        low = min_val + i * bin_width
        high = min_val + (i + 1) * bin_width
        count = histogram[i]
        bar_len = int(count / max_count * 40)
        bar = '#' * bar_len
        print(f"{low:8.2f} - {high:8.2f} us: {bar} ({count})")

def main():
    parser = argparse.ArgumentParser(description='解析 ftrace 数据')
    parser.add_argument('file', help='ftrace 输出文件')
    parser.add_argument('--func', help='过滤特定函数')
    parser.add_argument('--hist', action='store_true', help='显示直方图')
    args = parser.parse_args()

    functions = parse_function_graph(args.file)

    if not functions:
        print("未找到函数跟踪数据，尝试解析 tracepoint...")
        events = parse_tracepoint(args.file)
        print(f"找到 {len(events)} 个事件")
        for e in events[:20]:
            print(f"  {e['timestamp']:.6f}: {e['event']} - {e['data']}")
        return

    print_function_stats(functions, args.func)

    if args.hist and args.func and args.func in functions:
        print_histogram(functions[args.func], args.func)

if __name__ == '__main__':
    main()
```

### 8.2 延迟图表生成 (plot_latency.py)

```python
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
plot_latency.py - 生成延迟分析图表

依赖: pip install matplotlib

用法:
  ./plot_latency.py <trace_file> --func <function> --output <image.png>
"""

import sys
import re
import argparse
from collections import defaultdict

def parse_ftrace(filename):
    """解析 ftrace 数据"""
    functions = defaultdict(list)

    with open(filename, 'r') as f:
        for line in f:
            match = re.search(r'(\d+\.\d+)\s+us\s+\|.*?(\w+)\(\)', line)
            if match:
                duration = float(match.group(1))
                func_name = match.group(2)
                functions[func_name].append(duration)

    return functions

def plot_latency(durations, func_name, output_file):
    """绘制延迟图表"""
    try:
        import matplotlib
        matplotlib.use('Agg')  # 无GUI后端
        import matplotlib.pyplot as plt
    except ImportError:
        print("错误: 需要安装 matplotlib")
        print("运行: pip install matplotlib")
        return

    fig, axes = plt.subplots(2, 1, figsize=(10, 8))

    # 时序图
    ax1 = axes[0]
    ax1.plot(durations, 'b-', linewidth=0.5)
    ax1.set_xlabel('Sample')
    ax1.set_ylabel('Latency (us)')
    ax1.set_title(f'{func_name} Latency Over Time')
    ax1.grid(True, alpha=0.3)

    # 添加统计线
    avg = sum(durations) / len(durations)
    ax1.axhline(y=avg, color='r', linestyle='--', label=f'Avg: {avg:.2f} us')
    ax1.axhline(y=max(durations), color='g', linestyle=':', label=f'Max: {max(durations):.2f} us')
    ax1.legend()

    # 直方图
    ax2 = axes[1]
    ax2.hist(durations, bins=50, edgecolor='black', alpha=0.7)
    ax2.set_xlabel('Latency (us)')
    ax2.set_ylabel('Count')
    ax2.set_title(f'{func_name} Latency Distribution')
    ax2.grid(True, alpha=0.3)

    # 添加统计信息
    stats_text = f'Count: {len(durations)}\nAvg: {avg:.2f} us\nMin: {min(durations):.2f} us\nMax: {max(durations):.2f} us'
    ax2.text(0.95, 0.95, stats_text, transform=ax2.transAxes,
             verticalalignment='top', horizontalalignment='right',
             bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))

    plt.tight_layout()
    plt.savefig(output_file, dpi=150)
    print(f"图表已保存到: {output_file}")

def main():
    parser = argparse.ArgumentParser(description='生成延迟图表')
    parser.add_argument('file', help='ftrace 输出文件')
    parser.add_argument('--func', required=True, help='函数名')
    parser.add_argument('--output', default='latency.png', help='输出图片文件')
    args = parser.parse_args()

    functions = parse_ftrace(args.file)

    if args.func not in functions:
        print(f"错误: 未找到函数 '{args.func}'")
        print(f"可用函数: {', '.join(functions.keys())}")
        return

    plot_latency(functions[args.func], args.func, args.output)

if __name__ == '__main__':
    main()
```

---

## 9. 完整调试工作流示例

### 9.1 调试 SPI 驱动 (ICM-20608)

```bash
# === 在目标板上 ===

# 1. 加载驱动
insmod /lib/modules/4.1.15/spi_ICM20608.ko

# 2. 开始跟踪
./trace_spi.sh start

# 3. 运行测试
/usr/bin/myctl icm20608

# 4. 停止并分析
./trace_spi.sh stop
./trace_spi.sh save

# 5. 传输数据到主机
scp /tmp/spi_trace.txt dev-host:/tmp/

# === 在开发主机上 ===

# 6. 分析数据
./parse_ftrace.py /tmp/spi_trace.txt --func spi_sync

# 7. 生成图表
./plot_latency.py /tmp/spi_trace.txt --func spi_sync --output spi_latency.png
```

### 9.2 调试中断响应 (INPUT_KEY)

```bash
# 1. 开始中断跟踪
./trace_irq.sh events

# 2. 按下/释放按键多次

# 3. 停止并分析
./trace_irq.sh stop
./trace_irq.sh analyze

# 输出示例:
# === 中断延迟分析 ===
# 中断处理统计:
# irq_handler_entry: irq=67 name=gpio-keys
# irq_handler_exit:  irq=67 ret=handled
```

### 9.3 调试块设备性能 (ramdisk)

```bash
# 1. 开始块I/O跟踪
./trace_blk.sh start

# 2. 运行I/O测试
dd if=/dev/zero of=/dev/ramdisk bs=4k count=1000
dd if=/dev/ramdisk of=/dev/null bs=4k count=1000

# 3. 分析
./trace_blk.sh analyze

# 4. 使用 perf 进一步分析
perf stat dd if=/dev/ramdisk of=/dev/null bs=4k count=1000
```

---

## 10. 常见问题排查

### 10.1 ftrace 不工作

```bash
# 检查 debugfs 是否挂载
mount | grep debugfs
# 如果没有:
mount -t debugfs none /sys/kernel/debug

# 检查 ftrace 是否可用
cat /sys/kernel/debug/tracing/available_tracers
# 如果输出为空或只有 "nop"，需要重新配置内核
```

### 10.2 找不到跟踪函数

```bash
# 列出所有可跟踪函数
cat /sys/kernel/debug/tracing/available_filter_functions | wc -l

# 搜索特定函数
cat /sys/kernel/debug/tracing/available_filter_functions | grep spi

# 如果函数不在列表中，可能是:
# 1. 函数被内联优化了
# 2. 函数在不支持 ftrace 的模块中
# 3. 函数名拼写错误
```

### 10.3 跟踪开销过大

```bash
# 减少跟踪范围
echo "spi_sync" > /sys/kernel/debug/tracing/set_ftrace_filter

# 降低跟踪深度
echo 3 > /sys/kernel/debug/tracing/max_graph_depth

# 使用更轻量的跟踪器
echo function > /sys/kernel/debug/tracing/current_tracer  # 而不是 function_graph

# 设置跟踪缓冲区大小
echo 1024 > /sys/kernel/debug/tracing/buffer_size_kb
```

### 10.4 kprobe 添加失败

```bash
# 检查函数是否可探测
cat /sys/kernel/debug/kprobes/blacklist | grep <func_name>
# 如果在黑名单中，无法添加 kprobe

# 检查 kprobe 是否启用
cat /proc/sys/debug/kprobes-optimization
```

---

## 11. 参考资料

- [Ftrace Documentation](https://www.kernel.org/doc/html/latest/trace/ftrace.html)
- [Kprobes Documentation](https://www.kernel.org/doc/html/latest/trace/kprobes.html)
- [Perf Wiki](https://perf.wiki.kernel.org/)
- [trace-cmd Documentation](https://trace-cmd.org/)
- [Debugging kernel and modules via debugfs](https://www.kernel.org/doc/html/latest/filesystems/debugfs.html)
- [Linux Tracing Technologies](https://www.brendangregg.com/linuxperf.html)
