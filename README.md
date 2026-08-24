# TinyLinux-IoT-KernelLab

面向正点原子阿尔法 **i.MX6ULL** 开发板的 Linux 驱动学习与实验仓库，目标内核 **4.1.15** (NXP `linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek`)。仓库内涵盖字符设备、平台驱动、Input 子系统、I²C/SPI 总线驱动、IIO 子系统、块设备等多种驱动子系统的实战示例，以及配套的用户态 CLI 与多线程守护进程。

---

## 硬件 / 软件环境

| 项目 | 取值 |
| --- | --- |
| 开发板 | 正点原子阿尔法 i.MX6ULL |
| 主机 | Ubuntu 16.04 (虚拟机) |
| 内核 | Linux 4.1.15 (Alientek 移植版) |
| 交叉编译工具链 | `gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf` |
| 编译选项 | `-march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard` |
| 部署方式 | NFS 挂载 rootfs |
| 参考教程 | 《【正点原子】I.MX6U 嵌入式 Linux 驱动开发指南 V1.81》 |

---

## 目录结构

```
tinykernel-lab/
├── CMakeLists.txt              # 顶层 CMake：聚合 app + 所有驱动
├── Makefile                    # 顶层 Make：cmake-config / build / clean / compile_db
├── build.sh                    # 交互式脚本：编译 + 部署到 NFS
├── cmake/toolchains/
│   └── Toolchain-arm-linux-gnueabihf.cmake
├── app/                        # 用户态程序
│   ├── myctl.c                 # 统一 CLI：操控 LED / KEY / AP3216C / ICM20608
│   └── imx6d.c                 # 多线程守护进程（线程池 + 任务队列）
├── drivers/                    # 内核模块（每个子目录一个 .ko）
│   ├── LED/                    # platform_driver + device tree + GPIO
│   ├── INPUT_KEY/              # GPIO 中断 → input 子系统 (EV_KEY)
│   ├── IIC_AP3216C/            # I²C client：三合一光/接近传感器
│   ├── SPIICM-20608/           # SPI char dev：六轴陀螺/加速度计
│   ├── IIO_SPI_ICM_20608/      # 同一传感器的 IIO 子系统实现
│   ├── IIO_IIC_AP3216C/        # AP3216C 的 IIO 子系统实现
│   └── BLOCK_DEV/              # ramdisk 块设备驱动
├── docs/
│   ├── develop_blog.md         # 开发笔记 / 设计思路（主文档）
│   └── driver_debug_guide.md   # 驱动调试指南
└── .clang-format / .clangd     # 代码风格 & LSP 配置
```

---

## 驱动模块一览

| 目录 | 模块名 | 子系统 | 设备节点 | 说明 |
| --- | --- | --- | --- | --- |
| `drivers/LED` | `leddriver.ko` | platform / cdev | `/dev/led-dts-platform` | DTS 匹配 + GPIO 控制 LED |
| `drivers/INPUT_KEY` | `input_key.ko` | input | `/dev/input/eventX` | GPIO 中断 + 去抖，上报 `EV_KEY` |
| `drivers/IIC_AP3216C` | `iic_ap3216c.ko` | i2c | `/dev/ap3216c` | 环境光 / 接近 / IR 读取 |
| `drivers/IIO_IIC_AP3216C` | `iio_ap3216c.ko` | i2c / iio | `/sys/bus/iio/devices/...` | AP3216C 的 IIO raw 通道 |
| `drivers/SPIICM-20608` | `spi_ICM20608.ko` | spi / cdev | `/dev/icm20608` | 传统字符设备方式 |
| `drivers/IIO_SPI_ICM_20608` | `iio_icm_20608.ko` | spi / iio | `/sys/bus/iio/devices/...` | 通过 IIO 框架暴露 |
| `drivers/BLOCK_DEV` | `ramdisk.ko` | block | `/dev/ramdiskX` | 内存块设备示例 |

> 同一颗 ICM-20608 提供了 **传统 cdev** 与 **IIO 子系统** 两种实现，便于对比学习。

---

## 用户态应用

* **`myctl`** — 统一的命令行工具，封装对各驱动的 `open/read/write/ioctl`，例如：
  ```bash
  myctl led on
  myctl i2c read accel
  ```
* **`imx6d`** — 守护进程示例，内置线程池（互斥锁 + 条件变量 + 任务队列），用于并发驱动 LED、轮询 AP3216C / ICM20608。

---

## 构建

### 前置依赖

* 已编译好的 Alientek i.MX6ULL 内核源码树（用于 `M=` 外部模块编译）
* Linaro `arm-linux-gnueabihf-` 工具链
* `cmake >= 3.5`，`make`，`bear`（生成完整的 `compile_commands.json`，可选）

### 配置路径

构建路径都可以通过环境变量或 `make` 参数覆盖，默认按当前 WSL 工作区配置：

| 变量 | 默认值 |
| --- | --- | --- |
| `KERNELDIR` | `/home/tatarose_laptop_wsl/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek` |
| `NFS_ROOTFS` | `/home/tatarose_laptop_wsl/rootfs` |
| `KERNEL_VERSION` | `4.1.15` |
| `ARCH` | `arm` |
| `CROSS_COMPILE` | `arm-linux-gnueabihf-` |
| `TOOLCHAIN_PREFIX` | 空；设置后使用 `${TOOLCHAIN_PREFIX}/bin/arm-linux-gnueabihf-gcc` |

可以复制 `env.example` 后按本机路径调整：

```bash
cp env.example env.local
source env.local
```

也可以直接命令行覆盖：

```bash
make build KERNELDIR=/path/to/linux-imx NFS_ROOTFS=/path/to/rootfs TOOLCHAIN_PREFIX=/path/to/gcc-linaro
```

内核树需要先完成配置并生成外部模块编译所需文件。若 `include/generated/autoconf.h`
或 `Module.symvers` 缺失，先在内核源码目录执行：

```bash
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- oldconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- prepare scripts
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage modules
```

当前内核配置启用 `CONFIG_MODVERSIONS=y` 时，建议使用完整内核编译生成的
`Module.symvers`，否则外部模块可能出现符号版本问题。

### 一键构建 + 部署

```bash
./build.sh
# 1) 编译 + 部署 + 生成 compile_commands.json
# 2) 仅生成 compile_commands.json
# 3) 清理
# 4) 编译 + 部署
```

脚本会把所有 `.ko` 与 `myctl`、`imx6d` 拷贝到：
`$(NFS_ROOTFS)/lib/modules/$(KERNEL_VERSION)/` 和 `$(NFS_ROOTFS)/usr/bin/`

### 手动构建

```bash
make build           # = cmake-config + cmake-build + 合并 compile_commands.json
make compile_db      # 用 bear 拦截命令生成完整的 compile_commands.json
make clean           # 清理 build/、根 compile_commands.json、各驱动产物
```

也可以进入单个驱动目录单独编译：

```bash
cd drivers/LED && make
```

---

## 部署 & 测试（开发板侧）

NFS 挂载 rootfs 后：

```sh
cd /lib/modules/4.1.15/tinyLinux_IoT_kernellab
insmod leddriver.ko
insmod input_key.ko
insmod iic_ap3216c.ko
insmod spi_ICM20608.ko        # 或 iio_icm_20608.ko
insmod ramdisk.ko

./usr/bin/myctl                # 用户态 CLI
./usr/bin/imx6d                # 多线程守护进程
```

> 各驱动依赖对应的 Device Tree 节点（`gpio-leds`、`/key`、I²C/SPI client 等），请确保板上 DTB 已包含相应节点。

---

## 开发提示

* **clangd**：根 `Makefile` 的 `build` 目标会自动合并子模块的 `compile_commands.json` 到项目根目录，VS Code / clangd 可直接索引内核头文件。`c_cpp_properties.json` 已配置内核头路径。
* **代码风格**：见 `.clang-format`（LLVM 风格，缩进 8）。
* **调试指南**：`docs/driver_debug_guide.md` 收录了 printk、动态调试、oops 解读等技巧。
* **设计思路**：`docs/develop_blog.md` 是从设备树到子系统对接的完整开发笔记。

---

## License

驱动模块在源码中以 `MODULE_LICENSE("GPL")` 声明。仓库整体仅作学习交流使用。
