#!/bin/sh
# IMX6ULL/Linux 4.1.15 driver debugging helper.
#
# This script consolidates the ftrace, kprobe and perf examples from
# docs/driver_debug_guide.md.  Run it on the target board as root for commands
# which change kernel tracing state.

set -u

SCRIPT_NAME=${0##*/}
DEFAULT_TRACE_DIR=/sys/kernel/debug/tracing
TRACE_DIR=${DRIVER_DEBUG_TRACE_DIR:-$DEFAULT_TRACE_DIR}
OUTPUT_DIR=${DRIVER_DEBUG_OUTPUT_DIR:-/tmp}
PROFILE_FILE=${DRIVER_DEBUG_PROFILE_FILE:-/tmp/tinykernel_driver_debug.profile}
PERF_DATA=${DRIVER_DEBUG_PERF_DATA:-/tmp/perf.data}
GRAPH_DEPTH=${DRIVER_DEBUG_GRAPH_DEPTH:-10}
IRQ_THRESHOLD_US=${DRIVER_DEBUG_IRQ_THRESHOLD_US:-100}
LIST_LIMIT=${DRIVER_DEBUG_LIST_LIMIT:-50}

say()
{
	printf '%s\n' "$*"
}

warn()
{
	printf '警告: %s\n' "$*" >&2
}

die()
{
	printf '错误: %s\n' "$*" >&2
	exit 1
}

usage()
{
	cat <<EOF
用法: $SCRIPT_NAME <命令> [参数]

环境与查询:
  setup                         挂载 debugfs 并检查 tracing 目录
  check                         检查内核配置和调试能力
  list tracers                  列出可用 tracer
  list functions [正则]        列出可跟踪函数（默认显示前 $LIST_LIMIT 个）
  list events [正则]           列出 tracepoint（默认显示前 $LIST_LIMIT 个）

ftrace:
  start spi                     跟踪 SPI/ICM-20608 调用
  start i2c                     跟踪 I2C/AP3216C 调用
  start gpio                    跟踪 GPIO/LED 调用
  start irq-handler             跟踪中断处理函数
  start irq-events              跟踪 IRQ/softirq tracepoint
  start irqsoff [阈值us]        跟踪最长关中断延迟
  start block                   跟踪块设备 I/O tracepoint
  start function <函数,...>     跟踪指定内核函数
  start module <模块名>         跟踪指定模块的全部函数
  stop                          停止跟踪并关闭 tracepoint
  show [行数]                   显示 trace；指定行数时显示末尾内容
  save [文件]                   保存 trace（默认保存到 $OUTPUT_DIR）
  analyze <类型> [文件]         类型: spi|i2c|gpio|irq|block|kprobe
  clear                         清空 trace 缓冲区
  reset                         清除本脚本配置的 tracer/filter/kprobe

kprobe:
  kprobe add spi                探测 spi_sync 的入口、返回值和 r0 参数
  kprobe add i2c                探测 i2c_transfer 的入口、返回值和参数
  kprobe add function <函数>    探测指定函数的入口和返回值
  kprobe start|stop             开始或停止 kprobe 采集
  kprobe show|remove            显示或删除本脚本的 kprobe

perf:
  perf system [秒]              全系统采样（默认 10 秒）
  perf process <PID> [秒]       采样指定进程
  perf command <命令...>        采样指定命令
  perf stat <命令...>           统计 cycles/cache miss 等指标
  perf report                   查看 $PERF_DATA

示例:
  $SCRIPT_NAME setup
  $SCRIPT_NAME start spi
  /usr/bin/myctl icm20608
  $SCRIPT_NAME stop
  $SCRIPT_NAME analyze spi
  $SCRIPT_NAME save /tmp/spi_trace.txt
EOF
}

require_root()
{
	[ "$(id -u)" -eq 0 ] || die "该命令需要 root 权限"
}

require_trace_root()
{
	# A custom directory may be a writable tracefs mount or a test fixture.
	[ "$TRACE_DIR" != "$DEFAULT_TRACE_DIR" ] || require_root
}

is_positive_integer()
{
	case ${1:-} in
		''|*[!0-9]*|0) return 1 ;;
		*) return 0 ;;
	esac
}

validate_name()
{
	case ${1:-} in
		''|*[!A-Za-z0-9_.-]*) return 1 ;;
		*) return 0 ;;
	esac
}

mount_debugfs()
{
	# A custom trace directory is useful for tests and needs no mount handling.
	if [ "$TRACE_DIR" != "$DEFAULT_TRACE_DIR" ]; then
		[ -d "$TRACE_DIR" ] || die "tracing 目录不存在: $TRACE_DIR"
		return
	fi

	if [ ! -d "$TRACE_DIR" ]; then
		require_root
		if ! grep -qs '[[:space:]]/sys/kernel/debug[[:space:]][[:space:]]*debugfs' /proc/mounts; then
			mount -t debugfs none /sys/kernel/debug || die "无法挂载 debugfs"
		fi
	fi

	[ -d "$TRACE_DIR" ] || die "找不到 $TRACE_DIR；请确认 CONFIG_DEBUG_FS 和 CONFIG_FTRACE 已启用"
}

require_tracefs()
{
	mount_debugfs
	[ -r "$TRACE_DIR/available_tracers" ] || die "ftrace 不可用: $TRACE_DIR/available_tracers"
	[ -e "$TRACE_DIR/tracing_on" ] || die "缺少 tracing_on 控制文件"
}

write_control()
{
	value=$1
	file=$2
	printf '%s\n' "$value" > "$file" || die "写入失败: $file"
}

clear_control()
{
	file=$1
	printf '\n' > "$file" || die "清空失败: $file"
}

has_tracer()
{
	tr ' ' '\n' < "$TRACE_DIR/available_tracers" | grep -qx "$1"
}

has_function()
{
	[ -r "$TRACE_DIR/available_filter_functions" ] || return 1
	awk -v wanted="$1" '$1 == wanted { found = 1; exit } END { exit !found }' \
		"$TRACE_DIR/available_filter_functions"
}

has_event()
{
	[ -e "$TRACE_DIR/events/$1/enable" ]
}

remember_profile()
{
	profile=$1
	[ ! -L "$PROFILE_FILE" ] || die "状态文件不能是符号链接: $PROFILE_FILE"
	(umask 077; printf '%s\n' "$profile" > "$PROFILE_FILE") || \
		warn "无法记录当前 profile: $PROFILE_FILE"
}

current_profile()
{
	if [ -r "$PROFILE_FILE" ]; then
		IFS= read -r profile < "$PROFILE_FILE" || profile=trace
		case $profile in
			*[!A-Za-z0-9_.:-]*|'') profile=trace ;;
		esac
		printf '%s\n' "$profile"
	else
		printf '%s\n' trace
	fi
}

disable_events()
{
	[ -e "$TRACE_DIR/events/enable" ] && printf '0\n' > "$TRACE_DIR/events/enable" 2>/dev/null || true
}

prepare_trace()
{
	write_control 0 "$TRACE_DIR/tracing_on"
	disable_events
	[ -e "$TRACE_DIR/current_tracer" ] && write_control nop "$TRACE_DIR/current_tracer"
	[ -e "$TRACE_DIR/set_event" ] && clear_control "$TRACE_DIR/set_event"
	[ -e "$TRACE_DIR/set_ftrace_filter" ] && clear_control "$TRACE_DIR/set_ftrace_filter"
	[ -e "$TRACE_DIR/set_graph_function" ] && clear_control "$TRACE_DIR/set_graph_function"
	clear_control "$TRACE_DIR/trace"
}

add_graph_functions()
{
	functions=$1
	added=0
	[ -e "$TRACE_DIR/set_graph_function" ] || die "内核缺少 set_graph_function"
	clear_control "$TRACE_DIR/set_graph_function"

	for function_name in $functions; do
		if has_function "$function_name"; then
			printf '%s\n' "$function_name" >> "$TRACE_DIR/set_graph_function" || \
				die "无法设置函数过滤器: $function_name"
			say "  添加函数: $function_name"
			added=$((added + 1))
		else
			warn "函数不可跟踪，已跳过: $function_name"
		fi
	done

	[ "$added" -gt 0 ] || die "没有找到任何可跟踪函数；请先使用 list functions 查询"
}

start_graph_trace()
{
	profile=$1
	depth=$2
	functions=$3

	prepare_trace
	has_tracer function_graph || die "内核未启用 function_graph tracer"
	write_control function_graph "$TRACE_DIR/current_tracer"
	[ -e "$TRACE_DIR/max_graph_depth" ] && write_control "$depth" "$TRACE_DIR/max_graph_depth"
	add_graph_functions "$functions"
	write_control 1 "$TRACE_DIR/tracing_on"
	remember_profile "$profile"
	say "$profile 跟踪已启动；运行测试负载后执行: $SCRIPT_NAME stop"
}

enable_event_set()
{
	profile=$1
	shift
	prepare_trace
	enabled=0
	for event_name in "$@"; do
		if has_event "$event_name"; then
			write_control 1 "$TRACE_DIR/events/$event_name/enable"
			say "  启用事件: $event_name"
			enabled=$((enabled + 1))
		else
			warn "事件不存在，已跳过: $event_name"
		fi
	done
	[ "$enabled" -gt 0 ] || die "没有可用事件；请使用 list events 查询"
	write_control 1 "$TRACE_DIR/tracing_on"
	remember_profile "$profile"
	say "$profile 跟踪已启动；运行测试负载后执行: $SCRIPT_NAME stop"
}

start_trace()
{
	profile=${1:-}
	shift 2>/dev/null || true
	require_trace_root
	require_tracefs

	case $profile in
		spi)
			start_graph_trace spi "$GRAPH_DEPTH" \
				"spi_sync spi_transfer_one_message spi_pump_messages __spi_pump_messages"
			;;
		i2c)
			start_graph_trace i2c 8 \
				"i2c_transfer i2c_smbus_xfer i2c_smbus_read_byte_data i2c_smbus_write_byte_data i2c_imx_xfer"
			;;
		gpio)
			start_graph_trace gpio "$GRAPH_DEPTH" \
				"gpio_set_value gpio_get_value gpio_direction_output gpio_direction_input gpiod_set_value gpiod_get_value"
			;;
		irq-handler)
			start_graph_trace irq-handler "$GRAPH_DEPTH" \
				"handle_irq_event handle_edge_irq gpio_keys_gpio_isr key0_handler"
			;;
		irq-events|irq)
			enable_event_set irq \
				irq/irq_handler_entry irq/irq_handler_exit \
				irq/softirq_entry irq/softirq_exit
			;;
		block|blk)
			enable_event_set block \
				block/block_rq_issue block/block_rq_complete \
				block/block_bio_queue block/block_bio_complete
			;;
		irqsoff)
			threshold=${1:-$IRQ_THRESHOLD_US}
			is_positive_integer "$threshold" || die "阈值必须是正整数（微秒）"
			prepare_trace
			has_tracer irqsoff || die "内核未启用 irqsoff tracer"
			write_control irqsoff "$TRACE_DIR/current_tracer"
			[ -e "$TRACE_DIR/tracing_thresh" ] && write_control "$threshold" "$TRACE_DIR/tracing_thresh"
			write_control 1 "$TRACE_DIR/tracing_on"
			remember_profile irq
			say "irqsoff 跟踪已启动，阈值 ${threshold}us"
			;;
		function|func)
			function_list=${1:-}
			[ -n "$function_list" ] || die "请提供函数名，多个函数用逗号分隔"
			function_list=$(printf '%s' "$function_list" | tr ',' ' ')
			for function_name in $function_list; do
				validate_name "$function_name" || die "非法函数名: $function_name"
			done
			start_graph_trace function "$GRAPH_DEPTH" "$function_list"
			;;
		module)
			module_name=${1:-}
			validate_name "$module_name" || die "请提供合法的模块名"
			prepare_trace
			has_tracer function || die "内核未启用 function tracer"
			write_control function "$TRACE_DIR/current_tracer"
			printf ':mod:%s\n' "$module_name" > "$TRACE_DIR/set_ftrace_filter" || \
				die "无法过滤模块 $module_name；请确认模块已加载且名称正确"
			write_control 1 "$TRACE_DIR/tracing_on"
			remember_profile module
			say "正在跟踪模块: $module_name"
			;;
		*)
			die "未知 trace 类型: ${profile:-<空>}；请执行 $SCRIPT_NAME help"
			;;
	esac
}

stop_trace()
{
	require_trace_root
	require_tracefs
	write_control 0 "$TRACE_DIR/tracing_on"
	disable_events
	say "跟踪已停止，缓冲区内容已保留"
}

show_trace()
{
	require_tracefs
	lines=${1:-}
	if [ -n "$lines" ]; then
		is_positive_integer "$lines" || die "行数必须是正整数"
		tail -n "$lines" "$TRACE_DIR/trace"
	else
		cat "$TRACE_DIR/trace"
	fi
}

save_trace()
{
	require_tracefs
	profile=$(current_profile)
	default_output="$OUTPUT_DIR/${profile}_trace_$(date +%Y%m%d_%H%M%S).txt"
	output=${1:-$default_output}
	[ -d "${output%/*}" ] || [ "${output%/*}" = "$output" ] || \
		die "输出目录不存在: ${output%/*}"
	cat "$TRACE_DIR/trace" > "$output" || die "保存失败: $output"
	say "跟踪数据已保存到: $output"
}

count_matches()
{
	pattern=$1
	input=$2
	grep -c -E "$pattern" "$input" 2>/dev/null || true
}

analyze_kprobe()
{
	input=$1
	awk '
	function timestamp_and_event(    i, value) {
		timestamp = ""; event = "";
		for (i = 1; i <= NF; i++) {
			if ($i ~ /^[0-9]+\.[0-9]+:$/) {
				value = $i; sub(/:$/, "", value);
				timestamp = value + 0; event = $(i + 1);
				return 1;
			}
		}
		return 0;
	}
	timestamp_and_event() && event ~ /^tkdbg_.*_entry:$/ {
		base = event; sub(/_entry:$/, "", base);
		key = $1 "|" base;
		depth[key]++;
		started[key, depth[key]] = timestamp;
		next;
	}
	timestamp_and_event() && event ~ /^tkdbg_.*_exit:$/ {
		base = event; sub(/_exit:$/, "", base);
		key = $1 "|" base;
		if (depth[key] > 0) {
			latency = (timestamp - started[key, depth[key]]) * 1000000;
			printf "延迟: %.3f us (%s)\n", latency, base;
			sum += latency; count++;
			if (count == 1 || latency < min) min = latency;
			if (count == 1 || latency > max) max = latency;
			delete started[key, depth[key]];
			depth[key]--;
		}
	}
	END {
		if (count)
			printf "统计: 次数=%d, 平均=%.3f us, 最小=%.3f us, 最大=%.3f us\n", count, sum/count, min, max;
		else
			print "未找到可配对的 kprobe 入口/返回事件";
	}' "$input"
}

analyze_trace()
{
	type=${1:-}
	[ -n "$type" ] || die "请指定分析类型: spi|i2c|gpio|irq|block|kprobe"
	if [ -n "${2:-}" ]; then
		input=$2
	else
		require_tracefs
		input=$TRACE_DIR/trace
	fi
	[ -r "$input" ] || die "无法读取跟踪文件: $input"

	case $type in
		spi)
			say "=== SPI 跟踪统计 ==="
			say "spi_sync 记录数: $(count_matches 'spi_sync' "$input")"
			grep -E '[0-9.]+ us.*spi_(sync|transfer|pump)|spi_(sync|transfer|pump)' "$input" | tail -20 || true
			;;
		i2c)
			say "=== I2C 跟踪统计 ==="
			say "i2c_transfer 记录数: $(count_matches 'i2c_transfer' "$input")"
			grep -E '[0-9.]+ us.*i2c_|i2c_(transfer|smbus|imx)' "$input" | tail -20 || true
			;;
		gpio)
			say "=== GPIO 跟踪统计 ==="
			say "GPIO set 记录数: $(count_matches 'gpio(d)?_set_value' "$input")"
			say "GPIO get 记录数: $(count_matches 'gpio(d)?_get_value' "$input")"
			grep -E 'gpio(d)?_(set|get|direction)' "$input" | tail -20 || true
			;;
		irq)
			say "=== 中断跟踪统计 ==="
			say "IRQ 入口数: $(count_matches 'irq_handler_entry' "$input")"
			say "IRQ 出口数: $(count_matches 'irq_handler_exit' "$input")"
			grep -E 'irq_handler_(entry|exit)|latency' "$input" | tail -20 || true
			;;
		block|blk)
			say "=== 块 I/O 跟踪统计 ==="
			say "请求发出数: $(count_matches 'block_rq_issue' "$input")"
			say "请求完成数: $(count_matches 'block_rq_complete' "$input")"
			grep -E 'block_(rq|bio)_(issue|complete|queue)' "$input" | tail -20 || true
			;;
		kprobe)
			say "=== kprobe 延迟统计 ==="
			analyze_kprobe "$input"
			;;
		*) die "未知分析类型: $type" ;;
	esac
}

clear_trace()
{
	require_trace_root
	require_tracefs
	clear_control "$TRACE_DIR/trace"
	say "trace 缓冲区已清空"
}

remove_kprobes()
{
	[ -e "$TRACE_DIR/kprobe_events" ] || return 0
	found=0
	for enable_file in "$TRACE_DIR"/events/kprobes/tkdbg_*/enable; do
		[ -e "$enable_file" ] || continue
		found=1
		event_dir=${enable_file%/enable}
		event_name=${event_dir##*/}
		printf '0\n' > "$enable_file" 2>/dev/null || true
		printf '%s\n' "-:kprobes/$event_name" >> "$TRACE_DIR/kprobe_events" || \
			die "无法删除 kprobe: $event_name"
	done
	[ "$found" -eq 0 ] || return 0
}

reset_trace()
{
	require_trace_root
	require_tracefs
	prepare_trace
	remove_kprobes
	say "ftrace 过滤器、tracepoint、kprobe 和缓冲区已重置"
}

setup_kprobe()
{
	kind=$1
	function_name=$2
	event_stem=$(printf '%s' "$function_name" | sed 's/[^A-Za-z0-9_]/_/g')
	entry_event="tkdbg_${event_stem}_entry"
	exit_event="tkdbg_${event_stem}_exit"

	validate_name "$function_name" || die "非法函数名: $function_name"
	if [ -r /sys/kernel/debug/kprobes/blacklist ] && \
		awk -v wanted="$function_name" '$NF == wanted { found = 1 } END { exit !found }' \
			/sys/kernel/debug/kprobes/blacklist; then
		die "函数位于 kprobe 黑名单中: $function_name"
	fi

	prepare_trace
	remove_kprobes

	case $(uname -m) in
		arm|armv[4-8]*) reg0=%r0; reg1=%r1; reg2=%r2 ;;
		aarch64) reg0=%x0; reg1=%x1; reg2=%x2 ;;
		*) reg0=; reg1=; reg2=; warn "未知寄存器命名，仅记录入口和返回值" ;;
	esac

	case $kind in
		spi)
			if [ -n "$reg0" ]; then
				printf 'p:%s %s dev=%s\n' "$entry_event" "$function_name" "$reg0" > "$TRACE_DIR/kprobe_events" || \
					die "添加 SPI kprobe 失败"
			else
				printf 'p:%s %s\n' "$entry_event" "$function_name" > "$TRACE_DIR/kprobe_events" || \
					die "添加 SPI kprobe 失败"
			fi
			;;
		i2c)
			if [ -n "$reg0" ]; then
				printf 'p:%s %s adap=%s msgs=%s num=%s\n' \
					"$entry_event" "$function_name" "$reg0" "$reg1" "$reg2" > "$TRACE_DIR/kprobe_events" || \
					die "添加 I2C kprobe 失败"
			else
				printf 'p:%s %s\n' "$entry_event" "$function_name" > "$TRACE_DIR/kprobe_events" || \
					die "添加 I2C kprobe 失败"
			fi
			;;
		function)
			printf 'p:%s %s\n' "$entry_event" "$function_name" > "$TRACE_DIR/kprobe_events" || \
				die "添加 kprobe 失败: $function_name"
			;;
	esac
	printf 'r:%s %s ret=$retval\n' "$exit_event" "$function_name" >> "$TRACE_DIR/kprobe_events" || {
		remove_kprobes 2>/dev/null || true
		die "添加 kretprobe 失败: $function_name"
	}

	write_control 1 "$TRACE_DIR/events/kprobes/$entry_event/enable"
	write_control 1 "$TRACE_DIR/events/kprobes/$exit_event/enable"
	remember_profile "kprobe:$function_name"
	say "已配置 $function_name 的入口/返回探针；执行 $SCRIPT_NAME kprobe start 开始采集"
}

kprobe_command()
{
	action=${1:-}
	shift 2>/dev/null || true
	require_trace_root
	require_tracefs
	[ -e "$TRACE_DIR/kprobe_events" ] || die "内核未启用 CONFIG_KPROBE_EVENTS"

	case $action in
		add)
			kind=${1:-}
			case $kind in
				spi) setup_kprobe spi spi_sync ;;
				i2c) setup_kprobe i2c i2c_transfer ;;
				function|func)
					[ -n "${2:-}" ] || die "请提供要探测的函数名"
					setup_kprobe function "$2"
					;;
				*) die "用法: $SCRIPT_NAME kprobe add spi|i2c|function <函数>" ;;
			esac
			;;
		start)
			[ -s "$TRACE_DIR/kprobe_events" ] || die "尚未配置 kprobe"
			clear_control "$TRACE_DIR/trace"
			write_control 1 "$TRACE_DIR/tracing_on"
			say "kprobe 采集已启动"
			;;
		stop)
			write_control 0 "$TRACE_DIR/tracing_on"
			say "kprobe 采集已停止；可执行 $SCRIPT_NAME analyze kprobe"
			;;
		show)
			say "=== 当前 kprobe ==="
			cat "$TRACE_DIR/kprobe_events"
			say "=== 最近事件 ==="
			grep -E 'tkdbg_' "$TRACE_DIR/trace" | tail -30 || true
			;;
		remove)
			write_control 0 "$TRACE_DIR/tracing_on"
			remove_kprobes
			say "kprobe 已删除"
			;;
		*) die "用法: $SCRIPT_NAME kprobe add|start|stop|show|remove" ;;
	esac
}

perf_command()
{
	action=${1:-}
	shift 2>/dev/null || true
	command -v perf >/dev/null 2>&1 || die "未找到 perf；请按文档第 5.1 节交叉编译并安装"

	case $action in
		system)
			seconds=${1:-10}
			is_positive_integer "$seconds" || die "采样秒数必须是正整数"
			require_root
			perf record -a -g -o "$PERF_DATA" -- sleep "$seconds"
			say "采样完成: $PERF_DATA"
			;;
		process)
			pid=${1:-}
			seconds=${2:-10}
			is_positive_integer "$pid" || die "PID 必须是正整数"
			is_positive_integer "$seconds" || die "采样秒数必须是正整数"
			perf record -p "$pid" -g -o "$PERF_DATA" -- sleep "$seconds"
			say "采样完成: $PERF_DATA"
			;;
		command)
			[ "$#" -gt 0 ] || die "请提供要采样的命令"
			perf record -g -o "$PERF_DATA" -- "$@"
			say "采样完成: $PERF_DATA"
			;;
		stat)
			[ "$#" -gt 0 ] || die "请提供要统计的命令"
			perf stat -e cycles,instructions,cache-misses,branch-misses -- "$@"
			;;
		report)
			[ -r "$PERF_DATA" ] || die "找不到 perf 数据: $PERF_DATA"
			perf report -i "$PERF_DATA"
			;;
		*) die "用法: $SCRIPT_NAME perf system|process|command|stat|report" ;;
	esac
}

list_capability()
{
	type=${1:-}
	pattern=${2:-.}
	require_tracefs
	case $type in
		tracers) cat "$TRACE_DIR/available_tracers" ;;
		functions|funcs)
			[ -r "$TRACE_DIR/available_filter_functions" ] || die "函数列表不可用"
			grep -E -e "$pattern" "$TRACE_DIR/available_filter_functions" | head -n "$LIST_LIMIT" || true
			;;
		events)
			[ -r "$TRACE_DIR/available_events" ] || die "事件列表不可用"
			grep -E -e "$pattern" "$TRACE_DIR/available_events" | head -n "$LIST_LIMIT" || true
			;;
		*) die "用法: $SCRIPT_NAME list tracers|functions|events [正则]" ;;
	esac
}

check_config_option()
{
	option=$1
	importance=$2
	case $CONFIG_SOURCE in
		/proc/config.gz) value=$(zcat /proc/config.gz 2>/dev/null | grep -E "^${option}(=| )" | head -n 1) ;;
		*) value=$(grep -E "^${option}(=| )" "$CONFIG_SOURCE" 2>/dev/null | head -n 1) ;;
	esac
	case $value in
		"$option=y") printf '  [OK]   %s\n' "$option" ;;
		*) printf '  [%s] %s (%s)\n' "$importance" "$option" "${value:-未启用}" ;;
	esac
}

check_environment()
{
	say "=== 系统 ==="
	say "内核: $(uname -sr 2>/dev/null || printf unknown)"
	say "架构: $(uname -m 2>/dev/null || printf unknown)"
	if grep -qs '[[:space:]]/sys/kernel/debug[[:space:]][[:space:]]*debugfs' /proc/mounts; then
		say "debugfs: 已挂载"
	else
		warn "debugfs 未挂载（运行: $SCRIPT_NAME setup）"
	fi

	say ""
	say "=== ftrace ==="
	if [ -r "$TRACE_DIR/available_tracers" ]; then
		say "目录: $TRACE_DIR"
		say "tracer: $(cat "$TRACE_DIR/available_tracers")"
		[ -r "$TRACE_DIR/available_filter_functions" ] && \
			say "可跟踪函数数: $(wc -l < "$TRACE_DIR/available_filter_functions")"
	else
		warn "ftrace 控制目录不可用: $TRACE_DIR"
	fi

	CONFIG_SOURCE=
	if [ -r /proc/config.gz ] && command -v zcat >/dev/null 2>&1; then
		CONFIG_SOURCE=/proc/config.gz
	elif [ -r "/boot/config-$(uname -r)" ]; then
		CONFIG_SOURCE="/boot/config-$(uname -r)"
	fi

	say ""
	say "=== 内核配置 ==="
	if [ -n "$CONFIG_SOURCE" ]; then
		say "配置来源: $CONFIG_SOURCE"
		for option in CONFIG_FTRACE CONFIG_FUNCTION_TRACER CONFIG_FUNCTION_GRAPH_TRACER \
			CONFIG_DYNAMIC_FTRACE CONFIG_KPROBES CONFIG_KPROBE_EVENTS CONFIG_DEBUG_FS; do
			check_config_option "$option" REQUIRED
		done
		for option in CONFIG_STACK_TRACER CONFIG_IRQSOFF_TRACER CONFIG_PREEMPT_TRACER \
			CONFIG_SCHED_TRACER CONFIG_KRETPROBES CONFIG_PERF_EVENTS CONFIG_HW_PERF_EVENTS \
			CONFIG_KALLSYMS CONFIG_KALLSYMS_ALL; do
			check_config_option "$option" INFO
		done
	else
		warn "无法读取运行内核配置（/proc/config.gz 或 /boot/config-*）"
	fi

	say ""
	say "=== 用户态工具 ==="
	for tool in perf trace-cmd awk grep; do
		if command -v "$tool" >/dev/null 2>&1; then
			say "  [OK]   $tool: $(command -v "$tool")"
		else
			say "  [INFO] $tool: 未安装"
		fi
	done
}

main()
{
	command=${1:-help}
	shift 2>/dev/null || true

	case $command in
		help|-h|--help) usage ;;
		setup)
			require_trace_root
			mount_debugfs
			require_tracefs
			say "debugfs/ftrace 已就绪: $TRACE_DIR"
			;;
		check) check_environment ;;
		list) list_capability "$@" ;;
		start) start_trace "$@" ;;
		stop) stop_trace ;;
		show) show_trace "$@" ;;
		save) save_trace "$@" ;;
		analyze) analyze_trace "$@" ;;
		clear) clear_trace ;;
		reset) reset_trace ;;
		kprobe) kprobe_command "$@" ;;
		perf) perf_command "$@" ;;
		*) usage >&2; die "未知命令: $command" ;;
	esac
}

main "$@"
