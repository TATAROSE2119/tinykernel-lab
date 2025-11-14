#include "asm/gpio.h"
#include "linux/fs.h"
#include "linux/input.h"
#include "linux/interrupt.h"
#include "linux/printk.h"
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <linux/jiffies.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/timer.h>
#include <linux/types.h>

#include <linux/input.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

// 定义按键数量，表示系统中有多少个按键需要处理
#define KEY_NUM 1
// 定义设备数量，表示创建的字符设备数量
#define KEYINPUT_CNT 1
// 定义设备名称，用于标识输入设备的名称
#define KEYINPUT_NAME "keyinput"
// 定义KEY0按键值，用于标识第一个按键的键值
#define KEY0_VALUE 0x01
// 定义无效按键值，用于表示无效或错误的按键状态
#define INVAKEY 0xff

#define NODE_NAME_OF_KEY_IN_DTS "/key"

// 声明模块使用的许可证，声明为GPL许可证
MODULE_LICENSE("GPL");
// 声明模块作者信息
MODULE_AUTHOR("TATAROSE");

// 定义中断按键描述结构体，用于描述每个按键的相关信息
struct irq_keydesc {
        int gpio;            // GPIO编号，标识按键连接的GPIO引脚
        int irqnum;          // 中断号，每个GPIO对应的中断号
        unsigned char value; // 按键值，按键的键值标识
        char name[10];       // 名称，按键的名称标识
        irqreturn_t (*handler)(
                int, void *); // 中断服务函数指针，指向按键中断处理函数
};

// 定义key输入设备结构体，用于管理整个按键输入设备
struct keyinput_dev {
        dev_t devid;             // 设备号，包含主设备号和次设备号
        struct cdev cdev;        // 字符设备结构体，用于字符设备操作
        struct class *class;     // 类指针，用于sysfs中的设备分类管理
        struct device *device;   // 设备指针，指向具体的设备实例
        struct device_node *nd;  // 设备节点指针，指向设备树中的节点
        struct timer_list timer; // 防抖定时器，用于按键消抖处理
        struct irq_keydesc
                irq_keydesc[KEY_NUM]; // 按键描述数组，存储所有按键的描述信息
        unsigned char curkernum;      // 当前按键号，记录当前正在处理的按键编号
        struct input_dev *input_dev;  // 输入设备指针，指向Linux输入子系统设备
};

// 声明全局设备结构体变量，用于整个模块的设备管理
struct keyinput_dev keyinputdev;

// KEY0中断服务函数，当KEY0按键产生中断时被调用
static irqreturn_t key0_handler(int irq, void *dev_id) {
        struct keyinput_dev *dev = (struct keyinput_dev *)dev_id;

        // 记录当前按键号为0，表示是第一个按键(KEY0)产生了中断
        dev->curkernum = 0;
        // 设置定时器参数，将设备结构体作为参数传递给定时器处理函数
        dev->timer.data = (volatile long)dev_id;
        // 启动防抖定时器，10ms后触发定时器处理函数，用于按键消抖
        mod_timer(&dev->timer, jiffies + msecs_to_jiffies(10));
        // 返回中断处理成功标识
        return IRQ_RETVAL(IRQ_HANDLED);
}

// 定时器处理函数，用于按键消抖处理，避免按键抖动导致的误触发
void input_key_timer_function(unsigned long arg) {
        unsigned char value; // 存储GPIO电平状态的变量
        unsigned char num;   // 存储当前按键编号的变量
        struct keyinput_dev *dev =
                (struct keyinput_dev *)arg; // 获取设备结构体指针
        struct irq_keydesc *keydesc;        // 按键描述符指针

        // 获取当前按键编号，确定是哪个按键触发了中断
        num = dev->curkernum;
        // 获取对应按键的描述符结构体指针
        keydesc = &dev->irq_keydesc[num];
        // 读取GPIO电平状态，判断按键是按下还是释放
        value = gpio_get_value(keydesc->gpio);

        // 根据电平状态上报按键事件到Linux输入子系统
        if (value == 0) {
                // 按键按下，GPIO电平为低电平，上报按键按下事件
                input_event(dev->input_dev, EV_KEY, KEY_0, 1);
                input_sync(dev->input_dev); // 同步事件，确保事件被立即处理
        } else {
                // 按键释放，GPIO电平为高电平，上报按键释放事件
                input_event(dev->input_dev, EV_KEY, KEY_0, 0);
                input_sync(dev->input_dev); // 同步事件，确保事件被立即处理
        }
}

// 按键IO初始化函数，用于初始化所有按键相关的GPIO和中断
static int keyio_init(void) {
        unsigned char i = 0;  // 循环变量
        char name[10];        // 临时存储名称的缓冲区
        int return_value = 0; // 函数返回值

        // 查找设备树中的/keyinput节点，获取设备树配置信息
        keyinputdev.nd = of_find_node_by_path(NODE_NAME_OF_KEY_IN_DTS);
        if (keyinputdev.nd == NULL) {
                printk("can not find keyinput node\n"); // 打印错误信息
                return -EINVAL;                         // 返回错误码
        }

        // 获取GPIO编号，从设备树中读取key-gpio属性配置的GPIO引脚
        for (i = 0; i < KEY_NUM; i++) {
                keyinputdev.irq_keydesc[i].gpio =
                        of_get_named_gpio(keyinputdev.nd, "key-gpio", i);
                if (keyinputdev.irq_keydesc[i].gpio < 0) {
                        printk("can not find key-gpio%d\n", i); // 打印错误信息
                        return -EINVAL;                         // 返回错误码
                }
        }

        // 配置GPIO引脚，包括申请GPIO、设置为输入模式、获取中断号等
        for (i = 0; i < KEY_NUM; i++) {
                memset(keyinputdev.irq_keydesc[i].name, 0,
                       sizeof(name)); // 清空名称缓冲区
                sprintf(keyinputdev.irq_keydesc[i].name, "KEY%d",
                        i); // 设置按键名称
                // 申请GPIO引脚，防止被其他模块占用
                gpio_request(keyinputdev.irq_keydesc[i].gpio,
                             keyinputdev.irq_keydesc[i].name);
                // 设置GPIO为输入模式
                gpio_direction_input(keyinputdev.irq_keydesc[i].gpio);
                // 获取GPIO对应的中断号
                keyinputdev.irq_keydesc[i].irqnum =
                        gpio_to_irq(keyinputdev.irq_keydesc[i].gpio);
                if (keyinputdev.irq_keydesc[i].irqnum < 0) {
                        printk("gpio to irq failed,gpio=%d return=%d",
                               keyinputdev.irq_keydesc[i].gpio,
                               keyinputdev.irq_keydesc[i].irqnum);
                        return keyinputdev.irq_keydesc[i].irqnum;
                }
        }

        // 设置中断处理函数和按键值
        keyinputdev.irq_keydesc[0].handler = key0_handler;
        keyinputdev.irq_keydesc[0].value = KEY0_VALUE;

        // 注册中断处理函数，为每个按键注册对应的中断服务程序
        for (i = 0; i < KEY_NUM; i++) {
                return_value = request_irq(
                        keyinputdev.irq_keydesc[i].irqnum,
                        keyinputdev.irq_keydesc[i].handler,
                        IRQF_TRIGGER_FALLING |
                                IRQF_TRIGGER_RISING, // 设置中断触发方式为下降沿和上升沿都触发
                        keyinputdev.irq_keydesc[i].name, &keyinputdev);
                if (return_value < 0) {
                        printk("request irq failed\n"); // 打印错误信息
                        return -EINVAL;                 // 返回错误码
                }
        }

        // 初始化防抖定时器
        init_timer(&keyinputdev.timer);
        // 设置定时器处理函数
        keyinputdev.timer.function = input_key_timer_function;

        return 0; // 返回成功
}

// 模块初始化函数，当模块加载时被调用
static int __init input_key_init(void) {
        int return_value = 0; // 函数返回值

        // 初始化按键IO，包括GPIO和中断配置
        keyio_init();

        // 分配输入设备结构体内存
        keyinputdev.input_dev = input_allocate_device();
        if (!keyinputdev.input_dev) {
                printk("input_allocate_device failed\n"); // 打印错误信息
                return -ENOMEM;                           // 返回内存不足错误码
        }

        // 设置输入设备名称
        keyinputdev.input_dev->name = KEYINPUT_NAME;
        // 设置支持的事件类型为按键事件
        __set_bit(EV_KEY, keyinputdev.input_dev->evbit);
        // 设置支持重复按键事件
        __set_bit(EV_REP, keyinputdev.input_dev->keybit);
        // 设置支持KEY_0按键
        __set_bit(KEY_0, keyinputdev.input_dev->keybit);

        // 注册输入设备到Linux输入子系统
        return_value = input_register_device(keyinputdev.input_dev);
        if (return_value) {
                printk("input_register_device failed\n"); // 打印错误信息
                input_free_device(
                        keyinputdev.input_dev); // 释放已分配的输入设备
                return -EINVAL;                 // 返回错误码
        }
        printk("keyinput init\r\n");
        return 0; // 返回成功
}

// 模块退出函数，当模块卸载时被调用
static void __exit input_key_exit(void) {
        unsigned char i = 0; // 循环变量

        printk("keyinput exit start\r\n"); // 添加调试信息

        // 删除防抖定时器，同步等待定时器处理完成
        del_timer_sync(&keyinputdev.timer);

        // 释放所有按键的中断和GPIO资源
        for (i = 0; i < KEY_NUM; i++) {
                // 释放中断资源
                free_irq(keyinputdev.irq_keydesc[i].irqnum, &keyinputdev);
                // 释放GPIO资源
                gpio_free(keyinputdev.irq_keydesc[i].gpio);
        }

        // 注销并释放输入设备
        if (keyinputdev.input_dev) {
                input_unregister_device(keyinputdev.input_dev);
                input_free_device(keyinputdev.input_dev);
        }

        printk("keyinput exit done\r\n"); // 确保这条语句能够执行
}

// 注册模块初始化函数
module_init(input_key_init);
// 注册模块退出函数
module_exit(input_key_exit);