#!/bin/bash
set -e
cd "$(dirname "$0")"

NFS_ROOTFS=${NFS_ROOTFS:-/home/tatarose_laptop_wsl/rootfs}
KERNEL_VERSION=${KERNEL_VERSION:-4.1.15}
MODULE_DIR="${NFS_ROOTFS}/lib/modules/${KERNEL_VERSION}"
BIN_DIR="${NFS_ROOTFS}/usr/bin"

echo "TinyLinux IoT KernelLab 构建工具"
echo "NFS_ROOTFS=${NFS_ROOTFS}"
echo "1) 编译 + 部署 + 生成 compile_commands.json"
echo "2) 仅生成 compile_commands.json"
echo "3) 清理"
echo "4) 编译 + 部署"
read -p "选择 [1/2/3/4]: " choice

# 确保NFS目录存在
setup_nfs_dirs() {
  echo "确保NFS目录存在..."
  mkdir -p "${MODULE_DIR}" "${BIN_DIR}" 2>/dev/null || \
    sudo mkdir -p "${MODULE_DIR}" "${BIN_DIR}"
}

copy_artifact() {
  local src=$1
  local dst_dir=$2
  local name=$3

  if [ -f "${src}" ]; then
    cp "${src}" "${dst_dir}/" 2>/dev/null || sudo cp "${src}" "${dst_dir}/"
    echo "已拷贝 ${name} 到 ${dst_dir}"
  else
    echo "警告: ${name} 未找到"
  fi
}

# 拷贝生成的文件到NFS目录
deploy_files() {
  echo "拷贝文件到NFS目录..."

  # 拷贝内核模块
  copy_artifact "drivers/LED/leddriver.ko" "${MODULE_DIR}" "leddriver.ko"
  copy_artifact "drivers/INPUT_KEY/input_key.ko" "${MODULE_DIR}" "input_key.ko"
  copy_artifact "drivers/IIC_AP3216C/iic_ap3216c.ko" "${MODULE_DIR}" "iic_ap3216c.ko"
  copy_artifact "drivers/IIO_IIC_AP3216C/iio_ap3216c.ko" "${MODULE_DIR}" "iio_ap3216c.ko"
  copy_artifact "drivers/SPIICM-20608/spi_ICM20608.ko" "${MODULE_DIR}" "spi_ICM20608.ko"
  copy_artifact "drivers/BLOCK_DEV/ramdisk.ko" "${MODULE_DIR}" "ramdisk.ko"
  copy_artifact "drivers/IIO_SPI_ICM_20608/iio_icm_20608.ko" "${MODULE_DIR}" "iio_icm_20608.ko"

  # 拷贝用户应用程序
  copy_artifact "build/app/myctl" "${BIN_DIR}" "myctl"
  copy_artifact "build/app/imx6d" "${BIN_DIR}" "imx6d"
}

case $choice in
1)
  make clean
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
