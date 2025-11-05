#!/bin/bash
set -e
cd "$(dirname "$0")"

echo "TinyLinux IoT KernelLab 构建工具"
echo "1) 编译 + 部署 + 生成 compile_commands.json"
echo "2) 仅生成 compile_commands.json"
echo "3) 清理"
echo "4) 编译 + 部署"
read -p "选择 [1/2/3/4]: " choice

# 确保NFS目录存在
setup_nfs_dirs() {
    echo "确保NFS目录存在..."
    mkdir -p /home/py/linux/nfs/rootfs/lib/modules/4.1.15/tinyLinux_IoT_kernellab/usr/bin
}

# 拷贝生成的文件到NFS目录
deploy_files() {
    echo "拷贝文件到NFS目录..."
    
    # 拷贝内核模块
    if [ -f "drivers/LED/leddriver.ko" ]; then
        sudo cp drivers/LED/leddriver.ko /home/py/linux/nfs/rootfs/lib/modules/4.1.15/tinyLinux_IoT_kernellab/
        echo "已拷贝 leddrv.ko 到 NFS 目录"
    else
        echo "警告: leddrv.ko 未找到"
    fi
    
    # 拷贝用户应用程序
    if [ -f "build/app/myctl" ]; then
        sudo cp build/app/myctl /home/py/linux/nfs/rootfs/lib/modules/4.1.15/tinyLinux_IoT_kernellab/usr/bin/
        echo "已拷贝 myctl 到 NFS 目录"
    else
        echo "警告: myctl 未找到"
    fi
}

case $choice in
    1) 
        make build
        setup_nfs_dirs
        deploy_files
        ;;
    2) make compile_db ;;
    3) make clean ;;
    4) 
        make build
        setup_nfs_dirs
        deploy_files
        ;;
    *) echo "无效选择" ;;
esac