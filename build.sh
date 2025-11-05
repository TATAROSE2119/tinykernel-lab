#!/bin/bash
set -e
cd "$(dirname "$0")"

echo "TinyLinux IoT KernelLab 构建工具"
echo "1) 编译 + 部署 + 生成 compile_commands.json"
echo "2) 仅生成 compile_commands.json"
echo "3) 清理"
read -p "选择 [1/2/3]: " choice

case $choice in
    1) make build ;;
    2) make compile_db ;;
    3) make clean ;;
    *) echo "无效选择" ;;
esac