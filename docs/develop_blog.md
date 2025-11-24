# TinyLinux-IoT-KernelLab: 从零实现一个可扩展 Linux 智能终端系统

虚拟机ubuntu 16.04
开发板：正点原子阿尔法imx6ull开发板
参考教程：正点原子【正点原子】I.MX6U嵌入式Linux驱动开发指南V1.81
## ①功能
### 1.设备驱动文件合集
| Module               | Skills                           |
| -------------------- | -------------------------------- |
| platfor LED 驱动          | platform_device / device tree 匹配 |
| 按键INPUT子系统               | INPUT子系统                  |
| PWM 驱动 + 用户态接口       | 控制背光               |
| I2C 传感器驱动 | AP3216C 三合一环境光传感器  |
| SPI 传感器驱动 | ICM-20608 六轴传感器 |
| UART            | tty driver wrapper               |
| CAN 收发框架（使用 flexcan） | netdev / socketCAN               |
| EEPROM 驱动 + sysfs    | file operations, sysfs node      |
| 多点触摸屏驱动               | 触摸屏驱动                  |
| 音频播放功能               | 音频播放功能WM8960                  |

用统一的CLI工具访问设备

```bash
myctl led on
myctl can send 01 02 03
myctl i2c read accel
```
### 2.加 LLM API → “语音/命令控制开发板”

---

## 一、内核态需要实现 / 提供的功能（Linux 4.1.15）

内核态的任务就两类：

1. **把硬件抽象成标准 Linux 设备/子系统**
2. **提供稳定的用户态接口（/dev、/sys、input、ALSA、socketCAN）**

### 1. 各驱动模块（你那张表里的全部）

#### 1.1 LED platform 驱动

* **实现内容**

  * platform_driver + device tree 匹配 (`of_match_table`)
  * GPIO/寄存器控制 LED 亮灭
  * 提供字符设备（或 sysfs）接口：`/dev/led0` 或 `/sys/class/leds/...`
* **用户态可见接口**

  * `write()` / `ioctl()` 控制亮灭
  * 可选：sysfs 亮度节点 `brightness`

#### 1.2 按键 INPUT 子系统驱动（只有一个 key）

* **实现内容**

  * GPIO 中断（上/下沿）
  * 去抖（定时器或延迟采样）
  * 向 input 子系统上报 `EV_KEY`
* **用户态可见接口**

  * `/dev/input/eventX` 产生标准 input_event

#### 1.3 PWM 驱动 + 背光控制

* **实现内容**

  * PWM 控制器/通道配置（dts + pwm framework）
  * 背光设备注册（如果你走 backlight 子系统更正规）
* **用户态可见接口**

  * `/sys/class/backlight/.../brightness`
  * 或 `/sys/class/pwm/...`

#### 1.4 I2C：AP3216C 传感器驱动

* **实现内容**

  * I2C client 驱动、探测、寄存器读写
  * 数据读取与转换（lux / proximity / ir）
  * 可选：中断模式（接近触发）
* **用户态可见接口**

  * 字符设备 `/dev/ap3216c`
  * 或 sysfs：`/sys/bus/i2c/devices/.../xxx`

#### 1.5 SPI：ICM-20608 六轴驱动

* **实现内容**

  * SPI client 驱动、寄存器配置
  * IMU 数据读取与单位换算
  * 可选：中断 / FIFO（你可以先不用）
* **用户态可见接口**

  * 字符设备 `/dev/icm20608`
  * 或 sysfs 节点

#### 1.6 UART tty wrapper

* **实现内容**

  * 串口本身内核已有驱动，你做的是**封装/配置层**或额外协议层（可选）
  * 主要确保设备树、波特率、端口可用
* **用户态可见接口**

  * `/dev/ttymxcX`（标准 tty 设备）

#### 1.7 CAN（flexcan + socketCAN）

* **实现内容**

  * flexcan 控制器驱动 + device tree
  * 注册 netdev
  * socketCAN 协议栈接入（内核已有）
* **用户态可见接口**

  * `can0` 网络接口（`ifconfig can0 up ...`）
  * PF_CAN socket 收发 `struct can_frame`

#### 1.8 EEPROM 驱动 + sysfs

* **实现内容**

  * I2C/SPI EEPROM 驱动（你按板子实际芯片来）
  * file_operations（read/write）
  * sysfs 节点导出关键字段（只读/可写）
* **用户态可见接口**

  * `/dev/eeprom`
  * `/sys/.../config_xxx`

#### 1.9 多点触摸屏驱动（无 UI 也照样有用）

* **实现内容**

  * I2C/SPI 触控芯片驱动
  * 上报 input 多点事件（`EV_ABS + ABS_MT_*`）
* **用户态可见接口**

  * `/dev/input/eventY` 触摸事件流

#### 1.10 WM8960 音频驱动

* **实现内容**

  * ASoC codec 驱动（WM8960）
  * I2S/SAI CPU DAI 配置连接
  * 声卡注册
* **用户态可见接口**

  * ALSA PCM 设备：`/dev/snd/pcmC*D*p`
  * `aplay/arecord` 可直接用

---

### 2. 内核态“不做什么”

* **不做业务逻辑**：不做阈值判断、告警策略、采样调度、CAN 网关转发。这些全放用户态。
* **不做复杂多线程/队列框架**：内核里避免自造轮子，除非必要。
* **不做 UI 或命令处理**：只负责把设备“变得可用”。

---

## 二、用户态需要实现 / 负责的功能

用户态是“把驱动拼成系统”的地方。核心是两个程序：

* 后台守护进程 `imx6d`
* 串口命令行工具 `imx6ctl`

### 1. `imx6d`（常驻后台）

#### 1.1 基础设施组件（对应 0voice）

* **线程池 thread_pool**

  * 并发采集传感器
  * 并发处理 CAN / 配置更新 / 命令请求
* **定时器管理 timer_mgr（小根堆/红黑树）**

  * 周期采集任务调度
  * LED 闪烁节奏
  * 超时与心跳检测
* **事件循环 reactor（epoll）**

  * 统一监听：

    * `/dev/input/event*`（按键、触摸）
    * CAN socket
    * 串口控制通道 / 本地 socket
    * 可选网络 socket
* **消息队列 ring_buffer / queue**

  * 采集线程 → 业务线程
  * IO 线程 → 解析线程
  * 告警事件 → 告警处理线程
* **ProtoBuf 编解码 proto_codec**

  * CLI/网络控制协议
  * 传感器数据上报格式
  * CAN 帧包装格式
* **异步日志 async_logger**

  * 后台批量写 `/var/log/imx6d.log`
  * 支持 CLI 查看/调级别

#### 1.2 业务服务模块

1. **sensor_service**

   * 通过 `/dev/ap3216c`、`/dev/icm20608` 读数据
   * 用 timer_mgr 定期触发采样
   * 采样任务丢线程池并发执行
   * 输出统一 `SensorReport`
2. **alarm_service**

   * 对 SensorReport 做阈值判断（光照/姿态/震动等）
   * 触发声光告警：

     * LED：写 `/dev/led0`，闪烁模式由 timer_mgr 控制
     * 音频：向 ALSA PCM 写告警音
   * 支持“静音/解除静音”
3. **can_gateway_service**

   * reactor 监听 CAN socket
   * CAN 帧 ↔ ProtoBuf 消息双向转换
   * 可扩展成 CAN↔以太网网关
4. **config_service**

   * 启动从 `/dev/eeprom` 读配置
   * 内存中维护 KV 配置表
   * CLI 改配置后**异步写回 EEPROM**
5. **input_service**

   * 监听按键 input：识别短按/长按/双击
   * 监听触摸 input：做“区域触摸快捷键”（无 UI 也能用）
   * 生成控制事件交给其他模块
6. **cli_service（控制通道）**

   * 监听本地 socket（或 127.0.0.1 TCP）
   * 接收 imx6ctl 的 ProtoBuf 命令
   * 调度到线程池执行并返回结果

---

### 2. `imx6ctl`（串口下跑的 CLI）

你通过串口登录后运行它，它负责：

* 连接 `imx6d`（本地 socket / loopback TCP）
* 发送 ProtoBuf 命令
* 把返回结果打印成人话

它需要实现的命令族大概是：

1. **状态类**

   * `status`：系统运行状态、CAN 状态、是否告警、是否静音
2. **传感器类**

   * `sensor read` / `sensor stream on/off`
3. **告警类**

   * `alarm mute/unmute`
   * `alarm test`
4. **CAN 类**

   * `can dump`
   * `can send <id> <data>`
5. **配置类**

   * `config get/set`
   * `config save/load`
6. **日志类**

   * `log tail`
   * `log level debug/info/warn`

---

## 三、两边如何对接（接口清单）

为了保证“分层干净”，用户态只能通过这些接口碰内核：

* **字符设备 / sysfs**

  * LED、PWM、AP3216C、ICM-20608、EEPROM
* **input 事件**

  * Key、Touch：`/dev/input/event*`
* **ALSA PCM**

  * WM8960：`/dev/snd/pcm*`
* **socketCAN**

  * can0：PF_CAN socket
* **tty**

  * `/dev/ttymxc*`（仅作为登录/调试通道）

除了这些，用户态不应该直接摸寄存器或搞内核内部结构。

---

## 四、最简“边界总结”

* **内核态 = 设备能力 + 标准接口**
  只负责：**让硬件可用、稳定、符合 Linux 子系统规范**。

* **用户态 = 系统整合 + 业务逻辑 + 0voice 的工程化组件**
  只负责：**线程池并发采集、epoll 事件驱动、ProtoBuf 协议、告警策略、网关转发、配置中心、CLI 管理**。

这样分工，你的项目就既“像个真正系统”，又能把 0voice 那套点名的东西自然落地。




## ②开发流程
### 2025.11.04 加入LED功能
#### 1. 在设备树种创建节点
在根节点`/`下添加节点，描述一个LED设备
```dts
	gpioled{
		compatible = "gpio-leds";
		pinctrl-names = "default";
		pinctrl-0 = <&pinctrl_led>;
		led-gpios   =  <&gpio1 3 GPIO_ACTIVE_LOW>;
		status = "okay";
	};
```
先编写一个基础的驱动框架来测试一下。
```c
#include <...>

MOUDULE_LICENSE("GPL");
MOUDULE_AUTHOR("TATAROSE");


static int __init led_init(void){
    return 0;
}
static void __exit led_exit(void){

}

module_init(led_init);
module_exit(led_exit);
```
配置一下Cmake和Makefile,项目结构为
```bash
.
├── app
│   ├── CMakeLists.txt
│   └── myctl.c
├── build.sh
├── cmake
│   └── toolchains
│       └── Toolchain-arm-linux-gnueabihf.cmake
├── CMakeLists.txt
├── drivers
│   └── LED
│       ├── CMakeLists.txt
│       ├── leddriver.c
│       ├── Makefile
│       └── modules.order
├── Makefile
├── scripts

```
`app`下存放总应用程序，`Cmake`用户态即可编译出应用程序。`drivers`下存放驱动程序，由于使用`kbuild`来编译模块，所以Cmake无法替代`Makefile`，所以将`Makefile`添加到`drivers/LED`。

应用程序运行结果如下：
```bash
/lib/modules/4.1.15/tinyLinux_IoT_kernellab/usr/bin # ./myctl 

===== 设备控制程序 =====
支持的设备:
  1. LED

支持的命令:
  LED设备:
    0 - 关闭LED
    1 - 打开LED

使用方法:
  交互式: myctl
  命令行: myctl <device> <command>
  例如  : myctl led 1
========================

请选择要操作的设备:
1. LED
0. 退出
请输入选择 (0-1): 1

请选择命令:
0. 关闭
1. 打开
```

### 2025.11.05 添加INPUT子系统按键功能
添加按键功能，按键功能需要用到INPUT子系统，INPUT子系统是Linux内核中用于处理输入设备的子系统，如鼠标、键盘、触摸屏、游戏控制器等。
添加`./drivers/INPUT_KEY/input_key.c`,修改`./CMakeLists.txt`,`./app/myctl.c`,`./app/CMakeLists.txt`。

### 2025.11.13 开始添加IIC传感器功能
在单片机中IIC设备通信通常需要写一个xxx.c文件，里面有些函数，如`IIC_init()`,`IIC_read()`,`IIC_write()`，这样是吧IIC控制器和IIC芯片杂糅到一起，要分离。分离出来有两部分驱动：
 - IIC主机驱动：IIC控制器驱动，如`i2c_core.c`
 - IIC设备驱动：IIC传感器驱动，如`i2c_sensor.c`
IIC主机驱动写一次就够了，IIC设备驱动需要根据传感器的寄存器地址和数据长度进行编写。正好符合Linux驱动的分离与分层的原则。
Linux内核也将IIC驱动分为两部分：
- IIC总线驱动：IIC适配器驱动。
- IIC设备驱动：IIC传感器驱动，针对具体的传感器进行编写。

IIC适配器在内核中使用i2c_adapter结构体，用来描述IIC总线，IIC总线驱动需要实现i2c_adapter结构体的成员函数。
```c
struct i2c_adapter {
    struct module *owner;           // 指向拥有此适配器的模块
    unsigned int class;             // 适配器支持的I2C设备类
    const struct i2c_algorithm *algo; // 指向I2C算法结构的指针
    void *algo_data;                // 指向特定于算法的数据指针
    int timeout;                    // 超时时间（以jiffies为单位）
    int retries;                    // 重试次数
    struct device dev;              // 关联的底层设备结构体
    int nr;                         // 适配器编号（如i2c-0中的0）
    char name[48];                  // 适配器名称字符串
    struct list_head userspace_clients; // 用户空间创建的客户端链表
    const struct i2c_adapter_quirks *quirks; // 适配器特殊行为/限制描述
};
```
使用：先申请一个`i2c_adapter`结构体，然后初始化，最后将`i2c_adapter`结构体注册到内核中。
`i2c_adapter`通过`i2c_algorithm`结构体定义了底层通信方法，需要I2C适配器编写人员实现。
```c
struct i2c_algorithm {
    int (*master_xfer)(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
    int (*smbus_xfer)(struct i2c_adapter *adap, u16 addr, unsigned short flags,
                      char read_write, u8 command, int size, 
                      union i2c_smbus_data *data);
    u32 (*functionality)(struct i2c_adapter *adap);
};
```
其中`master_xfer`函数指针指向具体实现I2C消息传输的函数，这是I2C通信的核心。
I.MX6ULL的I2C控制器驱动时`i2c_imx.c`。
```c
struct i2c_client {
    unsigned short flags;           // 标志位，如I2C_CLIENT_TEN表示使用10位地址
    unsigned short addr;            // 芯片地址（注意：只存储7位地址，位于低7位）
    char name[I2C_NAME_SIZE];       // 设备类型名称
    struct i2c_adapter *adapter;    // 所属的I2C适配器
    struct i2c_driver *driver;      // 设备的驱动程序
    struct device dev;              // 设备模型中的设备结构
    int irq;                        // 设备产生的IRQ（如果有的话）
    struct list_head detected;      // 用于链接到i2c_driver.clients列表
#if IS_ENABLED(CONFIG_I2C_SLAVE)
    i2c_slave_cb_t slave_cb;        // 从模式的回调函数
#endif
};
```
`i2c_client`结构体描述I2C设备，I2C设备驱动需要实现`i2c_client`结构体的成员函数。内核会在启动时或检测到设备时自动创建`i2c_client`结构体，驱动开发者不需要手动定义。我们需要在设备树中添加具体的i2c设备节点，并指定设备类型。比如：
```dts
&i2c1 {
	clock-frequency = <100000>;
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_i2c1>;
	status = "okay";

	mag3110@0e {
		compatible = "fsl,mag3110";
		reg = <0x0e>;
		position = <2>;
	};

	fxls8471@1e {
		compatible = "fsl,fxls8471";
		reg = <0x1e>;
		position = <0>;
		interrupt-parent = <&gpio5>;
		interrupts = <0 8>;
	};
};
```
这样系统在解析设备树时，就会自动创建`i2c_client`结构体，并注册到内核中。

`i2c_driver`结构体描述I2C设备驱动,找个需要开发者创建.
```
+----------------+
|  i2c_driver    |  (设备驱动程序) - 开发者创建
+----------------+
        |
        | (绑定)
        v
+----------------+
|  i2c_client    |  (具体设备实例) - 内核自动创建
+----------------+
        |
        | (使用)
        v
+----------------+
|  i2c_adapter   |  (I2C控制器)
+----------------+
```
- 驱动开发者实现i2c_driver结构体并注册到I2C子系统
- 内核根据设备树或ACPI信息自动创建i2c_client结构体
- I2C核心将i2c_driver与匹配的i2c_client绑定
- 调用probe函数初始化设备
- 设备可以正常工作，通过i2c_client与硬件通信
- 当设备移除或驱动卸载时，调用remove函数清理资源

在使用设备树的时候，I2C设备挂载到指定节点下,修改设备树：
- 设备树io初始化
```dts
pinctrl-names = "default"; //i2c1引脚复用
pinctrl-0 = <&pinctrl_i2c1>;//i2c1引脚复用
```
设置`pinctrl_i2c1`：
```dts
pinctrl_i2c1: i2c1grp {
  fsl,pins = <
    MX6UL_PAD_UART4_TX_DATA__I2C1_SCL 0x4001b8b0
    MX6UL_PAD_UART4_RX_DATA__I2C1_SDA 0x4001b8b0
  >;
};
```
其中`MX6UL_PAD_UART4_TX_DATA__I2C1_SCL`要去看原理图上的连接关系，再去`imx6ul-pinfunc.h`中找对应的宏定义。
注意在同一个iic节点下，多个设备不能有相同的地址。
修改好之后重新生成设备树文件。编写驱动框架，I2C字符设备驱动框架。初始化AP3216C，实现`ap3216c_read()`函数。接下来通过`i2c_transfer()`函数进行数据传输。
```c
int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
```
`struct i2c_adapter *adap`: 表示要使用的I2C适配器对象
`struct i2c_msg *msgs`: 指向i2c_msg结构体数组的指针，每个结构体代表一次单独的消息传递操作
`int num`: 数组中包含的消息数量
- 成功时返回传输成功的消息数量
- 发生错误时返回负数表示失败原因
i2c_transfer函数本身并不直接驱动硬件完成消息交互，而是通过以下流程工作：

寻找到对应的`i2c_adapter`;找到该适配器关联的`i2c_algorithm`;调用`i2c_algorithm`中的`master_xfer()`函数;`master_xfer()`函数才是真正驱动硬件完成实际消息传输的接口
## 2025.11.18 添加SPI六轴传感器
主机控制器驱动：SOC的SPI外设驱动，此驱动是半导体原厂编写好的，当spi控制器和驱动匹配时，probe函数会调用，完成驱动初始化。

SPI控制器驱动的核心就是spi_master构建，注册和注销。

修改设备树，片选信号不作为硬件片选，而是作为普通的GPIO，在程序中自行控制片选引脚，添加IO相关信息：
```dts
pinctrl_ecspi3:icm20608{
  fsl,pins = <
    MX6UL_PAD_UART2_TX_DATA__GPIO1_IO20 0x10b0
    MX6UL_PAD_UART2_RX_DATA__ECSPI3_SCLK 0x10b1
    MX6UL_PAD_UART2_CTS_B__ECSPI3_MOSI 0x10b1
    MX6UL_PAD_UART2_RTS_B__ECSPI3_MISO 0x10b1
  >;
};
```
屏蔽其他UART2_TX引脚
创建设备树文件：
```dts
&ecspi3{
	fsl,spi-num-chipselects = <1>;
	cs_gpio=<&gpio1 20 GPIO_ACTIVE_LOW>;
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_ecspi3>;
	status = "okay";

	/* 对应的SPI芯片的子节点 */
	spidev0:icm20608@0 {
		#address-cells = <1>;
		#size-cells = <1>;
		compatible = "invn,icm20608";
		reg = <0>;
		spi-max-frequency = <8000000>;/* spi 的时钟 */
	};
};
```
用到两个重要的结构体：`struct spi_transfer`和`struct spi_message`，`spi_transfer`用来构建收发数据内容，然后打包到`spi_message`中，最后调用`spi_sync()`函数进行数据传输。

完成SPI驱动的编写。