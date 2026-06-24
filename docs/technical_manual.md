# TinyLinux-IoT-KernelLab 技术说明书

本文档面向代码评审、答辩展示、后续维护和二次开发，说明本项目的总体架构、构建部署方式、各类 Linux 驱动子系统的关键实现，以及用户态工具如何把这些驱动组合成一个可运行的嵌入式实验系统。

项目目标平台是正点原子阿尔法 i.MX6ULL 开发板，目标内核是 Linux 4.1.15。仓库实现了 GPIO LED、GPIO 按键 Input、I2C 传感器、SPI 传感器、IIO 传感器、RAM 块设备，以及两个用户态程序 `myctl` 和 `imx6d`。

---

## 1. 项目定位

本项目不是单一驱动示例，而是一个覆盖多个 Linux 内核设备模型的驱动实验集合。它的技术价值主要体现在以下几个方面：

1. 使用设备树和总线驱动模型把板级硬件描述与驱动代码解耦。
2. 对同一类硬件提供不同层次的 Linux 接口，例如 ICM-20608 同时提供传统字符设备实现和 IIO 子系统实现。
3. 覆盖字符设备、Input、I2C、SPI、IIO、Block 等多个内核子系统，便于横向比较它们的注册流程、数据通路和用户态接口。
4. 用户态同时提供简单 CLI 和周期调度型守护进程，展示驱动接口如何被真实程序消费。
5. 构建系统把用户态交叉编译、内核外部模块编译、NFS 部署和 `compile_commands.json` 生成串联起来，便于在 VS Code/clangd 环境下开发。

---

## 2. 运行环境

| 项目 | 内容 |
| --- | --- |
| 开发板 | 正点原子阿尔法 i.MX6ULL |
| CPU | ARM Cortex-A7 |
| 内核 | Linux 4.1.15，Alientek/NXP i.MX6ULL 移植内核 |
| 工具链 | `arm-linux-gnueabihf-`，Linaro GCC 4.9.4 |
| 用户态部署 | NFS rootfs |
| 内核模块编译方式 | Kbuild 外部模块，`make -C $(KERNELDIR) M=$(CURRENT_PATH) modules` |
| 用户态程序构建 | CMake 交叉编译 |

---

## 3. 总体架构

项目整体可以分为 4 层：

```text
┌──────────────────────────────────────────────────────────────┐
│ 用户态应用                                                     │
│ myctl: 交互式/命令式设备调试                                  │
│ imx6d: timerfd + epoll + thread pool 周期采集守护进程          │
└───────────────────────▲──────────────────────────────────────┘
                        │ read/write/sysfs/input_event
┌───────────────────────┴──────────────────────────────────────┐
│ Linux 标准用户态接口                                           │
│ /dev/led-dts-platform /dev/ap3216c /dev/icm20608              │
│ /dev/input/eventX /sys/bus/iio/devices/iio:deviceX /dev/ramdisk│
└───────────────────────▲──────────────────────────────────────┘
                        │ cdev/input/iio/block file operations
┌───────────────────────┴──────────────────────────────────────┐
│ 内核驱动层                                                     │
│ LED platform + GPIO                                            │
│ KEY GPIO IRQ + timer debounce + Input                          │
│ AP3216C I2C client + cdev                                      │
│ ICM20608 SPI client + cdev                                     │
│ ICM20608 SPI client + IIO                                      │
│ ramdisk block device + bio make_request                        │
└───────────────────────▲──────────────────────────────────────┘
                        │ GPIO/I2C/SPI/block memory
┌───────────────────────┴──────────────────────────────────────┐
│ 硬件和设备树                                                   │
│ i.MX6ULL GPIO/I2C/SPI 控制器、AP3216C、ICM-20608、按键、LED     │
└──────────────────────────────────────────────────────────────┘
```

这个架构的核心思想是：内核态只负责把硬件变成 Linux 标准设备，用户态负责业务调度、数据展示和组合控制。这样驱动本身保持可复用，用户态工具也能通过普通文件接口测试驱动。

---

## 4. 目录结构和模块职责

```text
tinykernel-lab/
├── CMakeLists.txt
├── Makefile
├── build.sh
├── app/
│   ├── myctl.c
│   └── imx6d.c
├── cmake/toolchains/
│   └── Toolchain-arm-linux-gnueabihf.cmake
├── drivers/
│   ├── LED/
│   │   └── leddriver.c
│   ├── INPUT_KEY/
│   │   └── input_key.c
│   ├── IIC_AP3216C/
│   │   ├── iic_ap3216c.c
│   │   └── ap3216creg.h
│   ├── SPIICM-20608/
│   │   ├── spi_ICM20608.c
│   │   └── icm20608reg.h
│   ├── IIO_SPI_ICM_20608/
│   │   └── iio_icm_20608.c
│   └── BLOCK_DEV/
│       └── ramdisk.c
└── docs/
```

| 模块 | 内核子系统 | 关键接口 | 技术点 |
| --- | --- | --- | --- |
| `drivers/LED` | platform、GPIO、cdev | `/dev/led-dts-platform` | 设备树匹配、GPIO 输出、字符设备注册 |
| `drivers/INPUT_KEY` | GPIO IRQ、timer、Input | `/dev/input/eventX` | 双边沿中断、定时器去抖、标准 input_event |
| `drivers/IIC_AP3216C` | I2C、cdev | `/dev/ap3216c` | I2C 两阶段寄存器读取、三类传感数据解析 |
| `drivers/SPIICM-20608` | SPI、cdev | `/dev/icm20608` | SPI 寄存器读写、批量读取 14 字节 IMU 数据 |
| `drivers/IIO_SPI_ICM_20608` | SPI、IIO | `/sys/bus/iio/devices/...` | IIO channel 建模、raw/scale 标准属性 |
| `drivers/BLOCK_DEV` | block | `/dev/ramdisk` | gendisk、request_queue、bio 分段拷贝 |
| `app/myctl.c` | 用户态 | CLI | 直接访问 cdev、input、IIO sysfs |
| `app/imx6d.c` | 用户态 | daemon | timerfd、epoll、线程池、周期任务队列 |

---

## 5. 构建与部署实现

### 5.1 顶层 CMake

顶层 `CMakeLists.txt` 完成三件事：

1. 定义 NFS rootfs 路径和内核源码路径。
2. 编译用户态程序 `app/`。
3. 进入每个驱动目录，通过子目录 `CMakeLists.txt` 触发外部内核模块构建。

内核模块没有直接用 CMake 编译 `.c` 文件，因为 Linux 内核模块必须走内核自己的 Kbuild 体系。项目采用 CMake 调度 Kbuild：

```text
CMake
  └── add_custom_command()
        └── make kernel_modules
              └── make -C $(KERNELDIR) M=$(CURRENT_PATH) modules
```

这种方式的好处是：

1. 顶层可以统一编译所有模块和用户态程序。
2. 每个驱动目录仍保留标准内核外部模块 Makefile。
3. clangd/VS Code 可以通过 CMake 和 bear 生成 `compile_commands.json`。
4. 未来可以继续新增驱动目录，只需要添加 `add_subdirectory()` 和对应 Makefile。

### 5.2 外部内核模块 Makefile

每个驱动目录都有类似结构：

```makefile
KERNELDIR := /home/tatarose/linux_for_imx6ull/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek
CURRENT_PATH := $(shell pwd)

ARCH := arm
CROSS_COMPILE := arm-linux-gnueabihf-

obj-m := module_name.o

kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
```

`obj-m` 告诉 Kbuild 当前目录要生成一个可加载模块。`M=$(CURRENT_PATH)` 表示这是外部模块目录，内核源码目录只提供构建规则、头文件、符号导出和目标内核配置。

### 5.3 交叉编译工具链

`cmake/toolchains/Toolchain-arm-linux-gnueabihf.cmake` 指定：

1. `CMAKE_SYSTEM_NAME Linux`
2. `CMAKE_SYSTEM_PROCESSOR arm`
3. `CMAKE_C_COMPILER arm-linux-gnueabihf-gcc`
4. `CMAKE_SYSROOT` 指向工具链自带 sysroot
5. 编译选项为 `-march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard`

这保证用户态程序和 i.MX6ULL 的 ARMv7-A/Cortex-A7 硬件 ABI 匹配。

### 5.4 `build.sh` 一键流程

`build.sh` 是面向日常开发的入口：

1. 清理旧构建。
2. 执行 `make build`。
3. 确保 NFS 目录存在。
4. 把 `.ko` 和用户态可执行文件复制到 rootfs。

部署路径目前是：

```text
/home/tatarose/linux_for_imx6ull/rootfs/lib/modules/4.1.15/
/home/tatarose/linux_for_imx6ull/rootfs/usr/bin/
```

开发板通过 NFS 挂载 rootfs 后，可以直接 `insmod` 模块并运行用户态程序。

---

## 6. LED 驱动实现

### 6.1 模块定位

LED 驱动位于 `drivers/LED/leddriver.c`，它组合了三类机制：

1. platform driver：通过设备树 compatible 匹配板级 LED 节点。
2. GPIO API：从设备树读取 GPIO 编号并设置输出电平。
3. 字符设备：创建 `/dev/led-dts-platform`，用户态通过 `write()` 控制 LED。

### 6.2 设备树匹配

驱动的匹配表为：

```c
static const struct of_device_id gpioled_of_match[] = {
        { .compatible = "gpio-leds" },
        {},
};
```

当设备树中存在 compatible 为 `gpio-leds` 的 platform device 时，内核 platform 总线会调用 `gpioled_probe()`。

`probe()` 内部调用 `led_init()` 完成字符设备和 GPIO 初始化。

### 6.3 字符设备注册流程

LED 驱动使用标准 cdev 流程：

```text
alloc_chrdev_region()
  └── cdev_init()
        └── cdev_add()
              └── class_create()
                    └── device_create()
                          └── /dev/led-dts-platform
```

关键数据结构是：

```c
struct gpioled_dev {
        dev_t devid;
        struct cdev cdev;
        struct class *class;
        struct device *device;
        int major;
        int minor;
        struct device_node *nd;
        int led_gpio_num;
};
```

这个结构把设备号、cdev、sysfs class、设备树节点和 GPIO 编号放到一个全局设备对象中，`open()` 时通过 `filp->private_data` 传给后续 `write()`。

### 6.4 GPIO 获取与控制

驱动从设备树路径 `/gpioled` 查找节点，再读取 `led-gpios` 属性：

```text
of_find_node_by_path("/gpioled")
  └── of_get_named_gpio(nd, "led-gpios", 0)
        └── gpio_request()
              └── gpio_direction_output()
```

用户态写入一个字节：

1. `0` 表示关灯命令。
2. `1` 表示开灯命令。

驱动内部存在板级电平取反逻辑：

```c
if (led_status == LED_OFF) {
        gpio_set_value(dev->led_gpio_num, LED_ON);
} else {
        gpio_set_value(dev->led_gpio_num, LED_OFF);
}
```

也就是说用户态语义是“开/关”，底层 GPIO 电平按板上 LED 的有效电平反向转换。这在嵌入式板卡上很常见，因为很多 LED 是低电平点亮。

### 6.5 用户态访问

`myctl` 中的 LED 控制路径为：

```text
myctl led 1
  └── open("/dev/led-dts-platform", O_RDWR)
        └── write(fd, {1}, 1)
              └── led_write()
                    └── gpio_set_value()
```

---

## 7. 按键 Input 驱动实现

### 7.1 模块定位

按键驱动位于 `drivers/INPUT_KEY/input_key.c`。它没有创建自定义 `/dev/key` 字符设备，而是接入 Linux Input 子系统。这样用户态可以通过标准 `/dev/input/eventX` 读取 `struct input_event`。

这比自定义字符设备更规范，因为按键、键盘、触摸屏、鼠标等输入设备都可以通过统一事件模型处理。

### 7.2 GPIO 中断模型

驱动从设备树 `/key` 节点读取 `key-gpio`：

```text
of_find_node_by_path("/key")
  └── of_get_named_gpio(nd, "key-gpio", i)
        └── gpio_request()
              └── gpio_direction_input()
                    └── gpio_to_irq()
                          └── request_irq()
```

中断触发方式为：

```c
IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING
```

这表示按下和释放都会触发中断。按下时 GPIO 通常变成低电平，释放时回到高电平。

### 7.3 定时器去抖

机械按键按下或释放时会出现抖动。如果中断服务函数直接上报事件，用户态可能收到多次误触发。

本驱动采用“中断只启动定时器，定时器到期后再采样”的方式：

```text
GPIO 边沿中断
  └── key0_handler()
        └── mod_timer(timer, jiffies + msecs_to_jiffies(10))
              └── 10ms 后 input_key_timer_function()
                    └── gpio_get_value()
                          └── input_event()
                                └── input_sync()
```

关键点：

1. 中断处理函数保持短小，只记录当前按键编号并启动定时器。
2. 10ms 后再读取 GPIO 电平，避开物理抖动时间窗口。
3. 最终使用 `input_event()` 上报 `EV_KEY`，再使用 `input_sync()` 提交事件帧。

### 7.4 Input 设备注册

驱动使用：

```text
input_allocate_device()
  └── 设置 input_dev->name
  └── __set_bit(EV_KEY, evbit)
  └── __set_bit(KEY_0, keybit)
  └── input_register_device()
```

注册成功后，内核会在 `/dev/input/eventX` 下生成事件节点。

### 7.5 事件上报语义

定时器函数中根据 GPIO 电平上报：

```c
if (value == 0) {
        input_event(input_dev, EV_KEY, KEY_0, 1);
        input_sync(input_dev);
} else {
        input_event(input_dev, EV_KEY, KEY_0, 0);
        input_sync(input_dev);
}
```

含义：

| value | 含义 |
| --- | --- |
| `1` | 按键按下 |
| `0` | 按键释放 |

用户态 `myctl` 通过读取 `struct input_event` 打印 `type/code/value`，可以验证驱动是否正确上报。

---

## 8. AP3216C I2C 传感器驱动实现

### 8.1 模块定位

AP3216C 驱动位于 `drivers/IIC_AP3216C/iic_ap3216c.c`，它是一个 I2C client 驱动，同时暴露传统字符设备 `/dev/ap3216c`。

AP3216C 是三合一环境光传感器，驱动读取：

1. IR：红外数据。
2. ALS：环境光数据。
3. PS：接近检测数据。

### 8.2 I2C 驱动匹配

驱动定义了两套匹配信息：

```c
static struct i2c_device_id ap3216c_id[] = {
        { "lsc,ap3216c", 0 },
        {}
};

static struct of_device_id ap3216c_of_match[] = {
        { .compatible = "lsc,ap3216c" },
        {},
};
```

驱动注册入口：

```text
module_init()
  └── i2c_add_driver(&ap3216c_driver)
        └── 匹配设备树 I2C 子节点
              └── ap3216c_probe()
```

当设备树中的 I2C 设备节点 compatible 为 `lsc,ap3216c` 时，I2C 核心会创建 `struct i2c_client` 并调用 `probe()`。

### 8.3 字符设备桥接

AP3216C 的 `probe()` 中同样执行 cdev 注册：

```text
alloc_chrdev_region()
  └── cdev_init()
        └── cdev_add()
              └── class_create()
                    └── device_create()
                          └── /dev/ap3216c
```

和 LED 驱动不同的是，AP3216C 还需要保存 I2C 核心传入的 `i2c_client`：

```c
ap3216c_dev.private_data = client;
```

这样 `read()` 函数通过 `filp->private_data` 找到驱动私有结构，再通过私有结构找到 I2C client，最终发起 I2C 传输。

### 8.4 I2C 寄存器读写封装

AP3216C 的寄存器读取采用标准“两条 I2C 消息”模型：

```text
msg[0]: 写 1 字节寄存器地址
msg[1]: 读 len 字节寄存器数据
```

代码结构：

```c
msgs[0].addr = client->addr;
msgs[0].flags = 0;
msgs[0].buf = &reg;
msgs[0].len = 1;

msgs[1].addr = client->addr;
msgs[1].flags = I2C_M_RD;
msgs[1].buf = val;
msgs[1].len = len;

i2c_transfer(client->adapter, msgs, 2);
```

这种方式的意义是，很多 I2C 寄存器型设备要求先写入寄存器地址，再通过 repeated start 读取数据。使用 `i2c_transfer()` 可以精确表达这类时序。

写寄存器则把寄存器地址和数据拼到同一个 buffer：

```text
buffer[0] = reg
buffer[1..n] = payload
i2c_transfer(..., 1)
```

### 8.5 设备初始化

设备在 `open()` 时初始化：

```text
写 AP3216C_SYSTEMCONG = 0x4  -> 软件复位
mdelay(50)
写 AP3216C_SYSTEMCONG = 0x3  -> 使能 ALS + PS + IR
读回 AP3216C_SYSTEMCONG      -> 打印验证
```

把初始化放在 `open()` 的好处是设备只有被用户态实际使用时才复位和使能。缺点是多个进程反复打开设备时会重复初始化，后续可以根据需求迁移到 `probe()`。

### 8.6 数据读取与解析

AP3216C 数据寄存器从 `AP3216C_IRDATALOW` 开始连续 6 字节：

| 字节 | 含义 |
| --- | --- |
| `buf[0]` | IR low |
| `buf[1]` | IR high |
| `buf[2]` | ALS low |
| `buf[3]` | ALS high |
| `buf[4]` | PS low |
| `buf[5]` | PS high |

驱动逐字节读取后解析：

```c
if (buf[0] & 0x80) {
        ir = 0;
        ps = 0;
} else {
        ir = ((u16)buf[1] << 8) | buf[0];
        ps = (((u16)buf[5] & 0x3F) << 4) | (buf[4] & 0x0F);
}
als = ((u16)buf[3] << 8) | buf[2];
```

关键点：

1. IR 低字节最高位用于判断 IR/PS 数据有效性。
2. PS 不是简单 16 位拼接，而是高字节低 6 位与低字节低 4 位组合。
3. ALS 使用标准低字节 + 高字节拼接。

### 8.7 用户态接口契约

`read("/dev/ap3216c")` 返回 3 个 `unsigned short`：

```text
data[0] = ir
data[1] = als
data[2] = ps
```

`myctl` 和 `imx6d` 都按这个二进制格式读取。这个接口非常简单，适合实验和验证；如果要形成稳定产品接口，建议增加版本、单位、错误码和结构体定义头文件。

---

## 9. ICM-20608 SPI 字符设备驱动实现

### 9.1 模块定位

传统 SPI 字符设备驱动位于 `drivers/SPIICM-20608/spi_ICM20608.c`。它把 ICM-20608 六轴 IMU 暴露为 `/dev/icm20608`。

读取一次设备会返回 7 个 `signed int`：

1. 加速度 X/Y/Z 原始值。
2. 温度原始值。
3. 陀螺仪 X/Y/Z 原始值。

### 9.2 SPI 驱动匹配

匹配表：

```c
static const struct of_device_id icm20608_of_match[] = {
        { .compatible = "invn,icm20608" },
        {}
};
```

驱动入口：

```text
module_init()
  └── spi_register_driver(&icm20608_driver)
        └── 匹配 SPI 设备节点
              └── icm20608_probe()
```

### 9.3 SPI 寄存器协议

ICM-20608 的 SPI 寄存器访问通过地址最高位区分读写：

| 操作 | 地址 bit7 | 发送格式 |
| --- | --- | --- |
| 读 | `1` | `reg | 0x80`，随后读取数据 |
| 写 | `0` | `reg & 0x7F`，随后发送数据 |

读寄存器使用两个 `spi_transfer`：

```text
transfer[0]: tx 1 字节读命令
transfer[1]: rx len 字节数据
spi_sync_transfer(spi, transfer, 2)
```

写寄存器使用一个 `spi_transfer`：

```text
tx[0] = reg & 0x7F
tx[1] = value
spi_sync_transfer(spi, &transfer, 1)
```

### 9.4 手动片选控制

驱动从 SPI 控制器父节点读取 `cs-gpios`：

```c
icm20608_dev.nd = of_get_parent(spi->dev.of_node);
icm20608_dev.cs_gpio = of_get_named_gpio(icm20608_dev.nd, "cs-gpios", 0);
```

读写前后手动拉低/拉高片选：

```c
gpio_set_value(cs_gpio, 0);
spi_sync_transfer(...);
gpio_set_value(cs_gpio, 1);
```

这是一种直接可控的方式，适合调试 SPI 时序。不过当前代码中 `gpio_request()` 和 `gpio_direction_output()` 被注释掉了，说明它依赖设备树或 SPI 控制器层已经正确管理 CS GPIO。若后续遇到片选异常，应优先检查这里。

### 9.5 芯片初始化

`spi_icm20608_reg_init()` 的初始化步骤：

```text
写 PWR_MGMT_1 = 0x80  -> 复位
mdelay(10)
写 PWR_MGMT_1 = 0x01  -> 唤醒并选择较优时钟源
mdelay(10)
读 WHO_AM_I           -> 期望 0xAF
写 SMPLRT_DIV = 0x00
写 GYRO_CONFIG = 0x18    -> 陀螺仪量程 ±2000 dps
写 ACCEL_CONFIG = 0x18   -> 加速度计量程配置
写 CONFIG = 0x04
写 ACCEL_CONFIG2 = 0x04
写 PWR_MGMT_2 = 0x00
写 LP_MODE_CFG = 0x00
写 FIFO_EN = 0x00
```

这个过程完成复位、唤醒、量程设置、低通滤波配置和 FIFO 关闭。驱动使用直接读取寄存器方式获取最新数据，不使用 FIFO 和中断。

### 9.6 批量读取 14 字节数据

ICM-20608 的数据寄存器从 `ACCEL_XOUT_H` 连续排列到 `GYRO_ZOUT_L`。驱动一次读取 14 字节：

```text
0-1   accel_x
2-3   accel_y
4-5   accel_z
6-7   temp
8-9   gyro_x
10-11 gyro_y
12-13 gyro_z
```

解析方式：

```c
accel_x = (s16)((data[0] << 8) | data[1]);
accel_y = (s16)((data[2] << 8) | data[3]);
accel_z = (s16)((data[4] << 8) | data[5]);
temp    = (s16)((data[6] << 8) | data[7]);
gyro_x  = (s16)((data[8] << 8) | data[9]);
gyro_y  = (s16)((data[10] << 8) | data[11]);
gyro_z  = (s16)((data[12] << 8) | data[13]);
```

批量读取比逐寄存器读取更有价值：

1. SPI 总线开销更小。
2. 一次事务内获取的数据时间一致性更好。
3. 用户态 `read()` 可以拿到一帧完整 IMU 数据。

### 9.7 用户态接口契约

`read("/dev/icm20608")` 返回：

```text
signed int data[7]
data[0] = accel_x_adc
data[1] = accel_y_adc
data[2] = accel_z_adc
data[3] = temp_adc
data[4] = gyro_x_adc
data[5] = gyro_y_adc
data[6] = gyro_z_adc
```

注意当前 `myctl` 对 ICM-20608 的主路径使用 IIO sysfs，而不是这个 `/dev/icm20608` 字符设备。这说明项目保留了两种实现方式，便于对比传统 cdev 与 IIO 的差异。

---

## 10. ICM-20608 IIO 驱动实现

### 10.1 为什么实现 IIO 版本

IIO，全称 Industrial I/O，是 Linux 中面向传感器、ADC、DAC、IMU 等工业输入输出设备的标准子系统。

传统字符设备虽然简单，但存在几个问题：

1. 每个驱动自定义二进制格式，用户态需要知道私有结构。
2. 单位、比例、通道含义无法通过标准方式暴露。
3. 不能自然接入通用 IIO 工具链。

IIO 版本将 ICM-20608 建模为标准传感器设备，通过 sysfs 暴露：

```text
/sys/bus/iio/devices/iio:deviceX/in_accel_x_raw
/sys/bus/iio/devices/iio:deviceX/in_accel_y_raw
/sys/bus/iio/devices/iio:deviceX/in_accel_z_raw
/sys/bus/iio/devices/iio:deviceX/in_anglvel_x_raw
/sys/bus/iio/devices/iio:deviceX/in_anglvel_y_raw
/sys/bus/iio/devices/iio:deviceX/in_anglvel_z_raw
/sys/bus/iio/devices/iio:deviceX/in_temp_raw
```

并通过 scale 属性表达 raw 值到物理单位的比例。

### 10.2 IIO 设备分配

驱动使用 devm 托管资源：

```c
indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(struct icm20608_dev));
dev = iio_priv(indio_dev);
dev->spi = spi;
```

`devm_iio_device_alloc()` 分配一个 `struct iio_dev`，并附带驱动私有数据区。`iio_priv()` 可以取回私有数据。

这种方式的优点是资源生命周期绑定到 `spi->dev`，probe 失败或设备移除时内核自动释放，减少手工错误清理。

### 10.3 Channel 建模

IIO 的核心是 channel。驱动定义了 7 个通道：

| 通道类型 | 修饰符 | sysfs 属性 | 寄存器 |
| --- | --- | --- | --- |
| `IIO_ACCEL` | X | `in_accel_x_raw` | `ACCEL_XOUT_H` |
| `IIO_ACCEL` | Y | `in_accel_y_raw` | `ACCEL_YOUT_H` |
| `IIO_ACCEL` | Z | `in_accel_z_raw` | `ACCEL_ZOUT_H` |
| `IIO_ANGL_VEL` | X | `in_anglvel_x_raw` | `GYRO_XOUT_H` |
| `IIO_ANGL_VEL` | Y | `in_anglvel_y_raw` | `GYRO_YOUT_H` |
| `IIO_ANGL_VEL` | Z | `in_anglvel_z_raw` | `GYRO_ZOUT_H` |
| `IIO_TEMP` | 无 | `in_temp_raw` | `TEMP_OUT_H` |

每个 channel 设置：

```c
.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)
.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE)
.address = 对应高字节寄存器地址
```

含义：

1. `RAW` 是每个通道独立读取的。
2. `SCALE` 是同类型通道共享的，例如加速度 XYZ 使用同一个 scale。
3. `address` 被驱动用作读取寄存器的入口。

### 10.4 `read_raw()` 实现

IIO 核心在用户态读取 sysfs 属性时调用驱动的 `read_raw()`：

```text
cat in_accel_x_raw
  └── IIO core
        └── icm20608_read_raw(mask = IIO_CHAN_INFO_RAW)
              └── icm20608_read_reg(chan->address, 2 bytes)
                    └── spi_write_then_read()
```

对于 `IIO_CHAN_INFO_RAW`：

```c
ret = icm20608_read_reg(dev, chan->address, buf, 2);
*val = (s16)((buf[0] << 8) | buf[1]);
return IIO_VAL_INT;
```

对于 `IIO_CHAN_INFO_SCALE`：

```text
IIO_ACCEL    -> 0.000598
IIO_ANGL_VEL -> 0.001065
IIO_TEMP     -> 326.800000
```

返回类型 `IIO_VAL_INT_PLUS_MICRO` 表示 `*val + *val2 / 1000000`。

### 10.5 IIO 注册

probe 中设置：

```c
indio_dev->name = "icm20608";
indio_dev->info = &icm20608_info;
indio_dev->modes = INDIO_DIRECT_MODE;
indio_dev->channels = icm20608_channels;
indio_dev->num_channels = ARRAY_SIZE(icm20608_channels);
```

最后：

```c
devm_iio_device_register(&spi->dev, indio_dev);
```

`INDIO_DIRECT_MODE` 表示当前驱动支持直接读取，不实现触发缓冲区、FIFO 或 ring buffer。

### 10.6 IIO 版本的技术价值

IIO 版本比字符设备版本更标准，技术含量主要体现在：

1. 把 IMU 的 7 个物理量建模为标准 IIO channel。
2. 用 `raw + scale` 表达数据和单位换算关系。
3. 通过 sysfs 自动生成用户态接口，不需要自定义 `/dev` 协议。
4. 使用 devm 托管资源，减少 remove/error path 的复杂度。
5. 便于后续扩展 IIO buffer、trigger、采样频率、校准 offset 等能力。

---

## 11. RAM 块设备驱动实现

### 11.1 模块定位

`drivers/BLOCK_DEV/ramdisk.c` 实现了一个 2MB 内存块设备。它不依赖真实存储硬件，而是把一段内核内存模拟成磁盘。

这类驱动的价值在于理解 Linux block layer：

1. 块设备不是 `read/write` 字符流，而是扇区寻址。
2. 内核通过 `bio` 描述一次块 I/O。
3. `gendisk` 表示系统可见磁盘。
4. `request_queue` 连接上层块 I/O 和驱动底层处理函数。

### 11.2 关键数据结构

```c
struct ramdisk_dev {
        int major;
        unsigned char *ramdisk_buff;
        struct gendisk *gendisk;
        struct request_queue *queue;
        spinlock_t lock;
};
```

| 字段 | 作用 |
| --- | --- |
| `major` | 动态申请的块设备主设备号 |
| `ramdisk_buff` | 2MB 实际存储空间 |
| `gendisk` | Linux 磁盘抽象 |
| `queue` | 块设备请求队列 |
| `lock` | 队列相关同步预留 |

### 11.3 初始化流程

模块加载时：

```text
kzalloc(RAMDISK_SIZE)
  └── register_blkdev(0, "ramdisk")
        └── alloc_disk(RAMDISK_MINOR)
              └── blk_alloc_queue()
                    └── blk_queue_make_request(queue, ramdisk_make_request)
                          └── 填充 gendisk
                                └── set_capacity()
                                      └── add_disk()
```

关键点：

1. `register_blkdev(0, name)` 动态申请块设备主设备号。
2. `alloc_disk(RAMDISK_MINOR)` 分配磁盘对象。
3. `blk_queue_make_request()` 注册 bio 处理函数。
4. `set_capacity(gendisk, RAMDISK_SIZE / 512)` 按 512 字节扇区设置容量。
5. `add_disk()` 后，设备才对系统可见。

### 11.4 bio 处理路径

当前驱动使用 `make_request` 模型，不走老式 request 队列循环。核心函数是：

```c
static void ramdisk_make_request(struct request_queue *q, struct bio *bio)
```

处理流程：

```text
bio->bi_iter.bi_sector << 9
  └── 得到字节偏移 offset
bio_for_each_segment(bvec, bio, iter)
  └── page_address(bvec.bv_page) + bvec.bv_offset
        └── 得到当前段的内核地址 ptr
if READ:
  memcpy(ptr, ramdisk_buff + offset, len)
else WRITE:
  memcpy(ramdisk_buff + offset, ptr, len)
offset += len
set_bit(BIO_UPTODATE, &bio->bi_flags)
bio_endio(bio, 0)
```

`bio` 可能由多个内存段组成，因此不能假设一次 I/O 对应一段连续内存。`bio_for_each_segment()` 是块设备驱动必须掌握的关键接口。

### 11.5 设备几何信息

`ramdisk_getgeo()` 填充：

```c
geo->heads = 2;
geo->cylinders = 32;
geo->sectors = RAMDISK_SIZE / (2 * 32 * 512);
```

这类几何信息主要用于兼容某些分区工具。现代 Linux 块层实际使用 LBA/扇区容量。

### 11.6 卸载流程

模块卸载时：

```text
del_gendisk()
  └── blk_cleanup_queue()
        └── put_disk()
              └── unregister_blkdev()
                    └── kfree(ramdisk_buff)
```

顺序很重要：先让磁盘从系统消失，再清理队列和内存，避免新 I/O 继续进入已释放资源。

---

## 12. 用户态 CLI `myctl`

### 12.1 模块定位

`app/myctl.c` 是一个统一调试入口，支持两种模式：

1. 无参数运行，进入交互式菜单。
2. 命令行运行，例如 `myctl led 1`。

它的价值是把多个驱动接口收敛到一个程序中，方便在开发板上快速验证。

### 12.2 LED 控制

路径：

```text
control_led()
  └── open("/dev/led-dts-platform", O_RDWR)
        └── write(fd, &databuf, 1)
              └── close(fd)
```

用户态只关心逻辑命令：

| 命令 | 含义 |
| --- | --- |
| `0` | 关闭 LED |
| `1` | 打开 LED |

底层有效电平由内核驱动处理。

### 12.3 Input 按键读取

路径：

```text
control_input_key()
  └── open("/dev/input/event1", O_RDWR)
        └── read(fd, &struct input_event, sizeof(...))
              └── 打印 type/code/value
```

这里直接读取标准 Linux input event，因此无需知道驱动内部 GPIO、中断和定时器细节。

### 12.4 AP3216C 读取

路径：

```text
control_ap3216c()
  └── open("/dev/ap3216c", O_RDONLY)
        └── read(fd, unsigned short data[3], sizeof(data))
              └── 打印 IR/ALS/PS
```

每 200ms 读取一次，用于观察环境光、接近和红外数据变化。

### 12.5 ICM-20608 IIO 读取和滑动平均滤波

`myctl` 的 ICM-20608 读取路径不是 `/dev/icm20608`，而是 IIO sysfs：

```text
/sys/bus/iio/devices/iio:device0/in_accel_x_raw
/sys/bus/iio/devices/iio:device0/in_anglvel_x_raw
/sys/bus/iio/devices/iio:device0/in_temp_raw
```

程序读取 raw 后做单位换算：

```text
accel_g = raw * 0.000598 / 9.8
gyro_rad_s = raw * 0.001065
temp_c = raw / 326.8 + 25.0
```

并使用 8 点滑动平均滤波：

```c
sum -= old_value;
buf[idx] = new_value;
sum += new_value;
idx = (idx + 1) % FILTER_WINDOW;
output = sum / count;
```

这个算法时间复杂度是 O(1)，不用每次重新遍历窗口，适合嵌入式周期采样。

### 12.6 终端可视化

`control_icm20608()` 使用 ANSI escape sequence 清屏和定位光标：

```text
\033[2J\033[H  清屏并移动到左上角
\033[5;0H      移动到第 5 行
```

再用字符进度条展示加速度和角速度。虽然这是轻量文本 UI，但足够用于串口终端或 SSH 调试。

---

## 13. 用户态守护进程 `imx6d`

### 13.1 模块定位

`app/imx6d.c` 是一个周期采集型守护进程雏形。它实现了：

1. 固定大小线程池。
2. 链表任务队列。
3. `timerfd` 周期定时器。
4. `epoll` 事件循环。
5. AP3216C 和 ICM-20608 周期采样任务。

整体架构：

```text
main thread
  ├── thread_pool_create(4)
  ├── timerfd_create(CLOCK_MONOTONIC)
  ├── epoll_create1()
  ├── epoll_ctl(ADD timerfd)
  └── epoll_wait()
        └── timer 到期
              ├── read(timerfd, &exp)
              ├── submit TASK_READ_AP3216C
              └── submit TASK_READ_ICM20608

worker threads
  └── pthread_cond_wait()
        └── 获取任务
              └── task->fn(task->param)
```

### 13.2 线程池实现

线程池核心结构：

```c
typedef struct {
        pthread_mutex_t lock;
        pthread_cond_t cond;
        task_node_t *head;
        task_node_t *tail;
        int shutdown;
        pthread_t *threads;
        int max_thread_num;
} thread_pool_t;
```

任务节点：

```c
typedef struct task_node {
        task_type_t type;
        task_fn fn;
        void *param;
        struct task_node *next;
} task_node_t;
```

这是典型的生产者-消费者模型：

1. 主线程是生产者，周期性提交任务。
2. 工作线程是消费者，等待条件变量。
3. 队列由互斥锁保护。
4. 提交任务后用 `pthread_cond_signal()` 唤醒一个工作线程。

### 13.3 worker 线程流程

```text
worker_thread()
  └── pthread_mutex_lock()
        └── while queue empty && !shutdown:
              └── pthread_cond_wait()
        └── if shutdown && queue empty:
              └── exit
        └── pop head task
  └── pthread_mutex_unlock()
  └── task->fn(task->param)
  └── free(task)
```

这里的关键细节是 `while` 而不是 `if`，用于处理条件变量虚假唤醒，也能保证多个线程竞争时状态判断正确。

### 13.4 周期定时器

定时器使用 `timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC)` 创建。`CLOCK_MONOTONIC` 不受系统时间调整影响，适合周期调度。

周期设置：

```c
its.it_interval.tv_sec = interval_ms / 1000;
its.it_value.tv_nsec = (interval_ms % 1000) * 1000000;
its.it_value = its.it_interval;
timerfd_settime(tfd, 0, &its, NULL);
```

当前调用 `creat_periodic_timer(200)`，即每 200ms 触发一次。

### 13.5 epoll 事件循环

主线程将 timerfd 加入 epoll：

```text
epoll_create1(0)
  └── epoll_ctl(EPOLL_CTL_ADD, tfd, EPOLLIN)
        └── epoll_wait()
```

定时器到期后：

```c
read(tfd, &exp, sizeof(exp));
thread_pool_submit(pool, TASK_READ_AP3216C, task_read_ap3216c, DEVICE_FILE_NAME_AP3216C);
thread_pool_submit(pool, TASK_READ_ICM20608, task_read_icm20608, DEVICE_FILE_NAME_ICM20608);
```

读取 `timerfd` 很重要，因为它会清除可读状态并获得过期次数。如果不读，epoll 会持续返回该事件。

### 13.6 任务函数

AP3216C 任务：

```text
open("/dev/ap3216c")
  └── read(unsigned short data[3])
        └── print IR/ALS/PS
```

ICM-20608 任务：

```text
open IIO sysfs raw 文件
  └── read raw
        └── atoi()
              └── 单位换算
                    └── 滑动平均滤波
                          └── print
```

LED 任务 `task_blink_led()` 也已实现，可以作为后续告警任务接入线程池。

### 13.7 并发模型的技术价值

`imx6d` 的技术价值在于它没有用简单 `while(1) + sleep()` 串行轮询，而是实现了更接近真实嵌入式守护进程的模型：

1. `timerfd` 把时间事件文件描述符化。
2. `epoll` 允许未来同时监听按键、socket、CAN、串口等 fd。
3. 线程池避免采样任务阻塞主事件循环。
4. 任务队列把调度和执行解耦。
5. 每种设备采集任务可以独立扩展错误处理、缓存、上报和告警策略。

---

## 14. 内核态与用户态接口总表

| 设备 | 内核模块 | 用户态路径 | 数据格式 | 主要消费者 |
| --- | --- | --- | --- | --- |
| LED | `leddriver.ko` | `/dev/led-dts-platform` | 写 1 字节，`0/1` | `myctl`、`imx6d` |
| KEY | `input_key.ko` | `/dev/input/eventX` | `struct input_event` | `myctl` |
| AP3216C | `iic_ap3216c.ko` | `/dev/ap3216c` | `unsigned short data[3]` | `myctl`、`imx6d` |
| ICM-20608 cdev | `spi_ICM20608.ko` | `/dev/icm20608` | `signed int data[7]` | 预留/对比 |
| ICM-20608 IIO | `iio_icm_20608.ko` | `/sys/bus/iio/devices/iio:deviceX/*` | 文本 raw/scale | `myctl`、`imx6d` |
| ramdisk | `ramdisk.ko` | `/dev/ramdisk` | 块 I/O | 文件系统/块工具 |

---

## 15. 设备树依赖

驱动依赖设备树中存在对应节点和属性。

### 15.1 LED

代码期望：

```text
路径: /gpioled
属性: led-gpios
匹配: compatible = "gpio-leds"
```

注意：驱动的 platform 匹配 compatible 是 `gpio-leds`，但实际获取节点时使用固定路径 `/gpioled`。如果设备树节点路径不同，即使 platform 匹配成功，`of_find_node_by_path("/gpioled")` 仍会失败。更稳妥的写法是使用 `pdev->dev.of_node`。

### 15.2 KEY

代码期望：

```text
路径: /key
属性: key-gpio
```

按键 GPIO 必须能转换为 IRQ，即 `gpio_to_irq()` 成功。

### 15.3 AP3216C

代码期望 I2C 子节点：

```text
compatible = "lsc,ap3216c"
reg = <I2C address>
```

具体总线号和地址由板级设备树决定。

### 15.4 ICM-20608

代码期望 SPI 子节点：

```text
compatible = "invn,icm20608"
```

SPI 字符设备版本还从父节点读取：

```text
cs-gpios
```

IIO 版本则主要依赖 SPI core 和设备树子节点完成片选管理。

---

## 16. 调试与验证方法

### 16.1 模块加载验证

开发板侧：

```sh
cd /lib/modules/4.1.15
insmod leddriver.ko
insmod input_key.ko
insmod iic_ap3216c.ko
insmod iio_icm_20608.ko
insmod ramdisk.ko
dmesg | tail -80
```

重点检查：

1. 设备号是否申请成功。
2. GPIO 编号是否读取成功。
3. I2C/SPI probe 是否执行。
4. ICM-20608 `WHO_AM_I` 是否为 `0xAF`。
5. IIO 设备是否注册。

### 16.2 LED 验证

```sh
myctl led 1
myctl led 0
```

也可以直接：

```sh
printf '\001' > /dev/led-dts-platform
printf '\000' > /dev/led-dts-platform
```

### 16.3 按键验证

```sh
cat /proc/bus/input/devices
hexdump /dev/input/event1
myctl
```

如果 event 编号变化，`myctl` 中硬编码的 `/dev/input/event1` 需要调整，或者后续改成按 input device name 自动查找。

### 16.4 AP3216C 验证

```sh
myctl
# 选择 i2c_ap3216c
```

或写一个最小程序读取 `/dev/ap3216c` 的 6 字节数据。

### 16.5 IIO 验证

```sh
ls /sys/bus/iio/devices/
cat /sys/bus/iio/devices/iio:device0/name
cat /sys/bus/iio/devices/iio:device0/in_accel_x_raw
cat /sys/bus/iio/devices/iio:device0/in_anglvel_x_raw
cat /sys/bus/iio/devices/iio:device0/in_temp_raw
```

如果 `iio:device0` 编号不是 0，需要修改用户态路径或改成运行时扫描 `name == icm20608` 的 IIO 设备。

### 16.6 ramdisk 验证

```sh
cat /proc/devices | grep ramdisk
ls -l /dev/ramdisk*
dd if=/dev/zero of=/dev/ramdisk bs=512 count=8
hexdump -C /dev/ramdisk | head
mkfs.ext2 /dev/ramdisk
mkdir -p /mnt/ramdisk
mount /dev/ramdisk /mnt/ramdisk
```

是否能格式化和挂载，是验证 block layer 对接是否完整的直接方法。

### 16.7 ftrace/perf

项目已有 `docs/driver_debug_guide.md`，可用于进一步跟踪：

1. SPI 调用链和耗时。
2. I2C transfer 调用。
3. GPIO 中断延迟。
4. 块设备 bio 处理路径。

---

## 17. 当前实现边界和可改进点

这部分不是否定当前实现，而是为了说明项目已经实现的范围，以及后续工程化时需要补强的地方。

### 17.1 资源释放和错误路径

部分驱动在 `probe()` 或 init 中间失败时，已经申请的 cdev、class、device、GPIO 没有完整回滚。工程化驱动应采用逐层 `goto err_xxx` 清理模式。

例如 cdev 添加成功后，如果 `class_create()` 失败，需要 `cdev_del()` 和 `unregister_chrdev_region()`。

### 17.2 全局单例设备

多个驱动使用全局静态设备对象，例如 `ap3216c_dev`、`icm20608_dev`。这对单板单设备实验足够，但不支持同型号多设备。

更通用的方式是：

1. 在 `probe()` 中为每个设备分配私有结构。
2. 使用 `i2c_set_clientdata()`、`spi_set_drvdata()` 保存。
3. `remove()` 中通过 `*_get_clientdata()` 取回。

### 17.3 硬编码设备路径

用户态目前硬编码：

```text
/dev/input/event1
/sys/bus/iio/devices/iio:device0
```

实际系统中 event 和 iio 编号可能随加载顺序变化。后续可以：

1. 扫描 `/proc/bus/input/devices` 或 `/sys/class/input`。
2. 扫描 `/sys/bus/iio/devices/iio:device*/name`。
3. 找到 name 匹配的设备后再读取对应路径。

### 17.4 IIO 温度 scale 语义

IIO 版本中温度换算由用户态写死：

```text
temp_c = raw / 326.8 + 25.0
```

当前 IIO 驱动只暴露了 raw 和 scale，没有暴露 offset。更规范的 IIO 温度表达通常需要 `RAW + OFFSET + SCALE` 或按 IIO 约定返回可被通用工具理解的属性。

### 17.5 SPI 字符设备片选 GPIO

`spi_ICM20608.c` 中手动控制 CS GPIO，但 `gpio_request()` 和 `gpio_direction_output()` 被注释。若底层没有替驱动配置好 GPIO，手动 `gpio_set_value()` 可能不可靠。

更稳妥的方案：

1. 让 SPI core 完全管理片选，驱动不手动拉 CS。
2. 或驱动显式 request/configure CS GPIO，并在 remove 中释放。

### 17.6 AP3216C I2C adapter 引用

`ap3216c_read_regs()` 中调用了 `i2c_get_adapter(client->adapter->nr)` 并重新赋给 `client->adapter`。通常 I2C client 的 adapter 已由内核管理，不需要在每次读寄存器时重新获取。若使用 `i2c_get_adapter()`，还应配套 `i2c_put_adapter()`。

实验环境下通常不会马上暴露问题，但工程化版本建议直接使用 `client->adapter`。

### 17.7 ramdisk 越界检查

`ramdisk_make_request()` 当前直接根据 bio sector 和 len 做 memcpy，没有检查 `offset + len` 是否超过 `RAMDISK_SIZE`。块层通常会按 capacity 限制 I/O，但驱动层增加防御性检查更稳妥。

---

## 18. 后续扩展方向

### 18.1 IIO buffer 和 trigger

当前 IIO 驱动是 `INDIO_DIRECT_MODE`，每次读取 sysfs 都触发一次 SPI 访问。后续可以实现：

1. IIO triggered buffer。
2. 定时采样。
3. FIFO 批量读取。
4. 中断触发数据就绪。

这样可以显著降低高频采样时的 sysfs 和 SPI 开销。

### 18.2 统一设备发现层

用户态可增加 `device_discovery.c`：

1. 按名称查找 input event。
2. 按 IIO name 查找 iio device。
3. 检查 `/dev` 节点是否存在。
4. 给出清晰错误提示。

### 18.3 守护进程服务化

`imx6d` 可以继续扩展：

1. 加入 UNIX domain socket 或 TCP 控制接口。
2. 加入配置文件。
3. 加入日志级别。
4. 加入设备掉线重试。
5. 加入阈值告警和 LED 闪烁任务。
6. 加入按键事件监听，把 input fd 加入 epoll。

### 18.4 驱动接口头文件化

AP3216C 和 ICM-20608 字符设备当前依赖隐式二进制数组格式。后续可以提供公共头文件：

```c
struct ap3216c_sample {
        uint16_t ir;
        uint16_t als;
        uint16_t ps;
};

struct icm20608_sample {
        int32_t accel[3];
        int32_t temp;
        int32_t gyro[3];
};
```

这样内核态和用户态共享 ABI 定义，减少格式误解。

---

## 19. 总结

TinyLinux-IoT-KernelLab 的核心价值是把 i.MX6ULL 上常见的驱动开发技术串成了一个完整实验体系：

1. LED 展示 platform + GPIO + cdev 的最小闭环。
2. 按键展示 GPIO 中断、定时器去抖和 Input 子系统。
3. AP3216C 展示 I2C client、寄存器协议和字符设备桥接。
4. ICM-20608 字符设备展示 SPI 寄存器访问、片选控制和批量 IMU 数据读取。
5. ICM-20608 IIO 版本展示传感器标准化建模、channel、raw/scale 和 sysfs 自动接口。
6. ramdisk 展示块设备注册、gendisk、request_queue 和 bio 分段处理。
7. `myctl` 展示最小用户态验证路径。
8. `imx6d` 展示基于 `timerfd + epoll + thread pool` 的周期采样服务模型。

从学习路径看，这个项目覆盖了从“点亮一个 GPIO”到“接入 Linux 标准子系统”的完整跨度；从工程角度看，它已经具备模块化扩展、统一构建、NFS 部署和调试文档基础，适合作为后续 IoT 网关、传感器采集终端或嵌入式 Linux 驱动实验平台继续演进。
