/*
 * @Description: AP3216C光传感器驱动程序
 * @Author: TATAROSE
 * @LastEditors: TATAROSE
 * @LastEditTime: 2021-09-05 20:05:03
 */
/* #region include */
#include "linux/fs.h"
#include "linux/input.h"
#include "linux/interrupt.h"
#include "linux/mod_devicetable.h"
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

#include "ap3216creg.h"
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
/* #endregion */

// 定义设备数量和设备名称
#define AP3216C_CNT 1          // 设备数量
#define AP3216C_NAME "ap3216c" // 设备名称

MODULE_LICENSE("GPL");     // 声明模块许可证为GPL
MODULE_AUTHOR("TATAROSE"); // 声明模块作者

/**
 * @brief AP3216C设备结构体，用于保存设备相关信息
 * 
 * 这个结构体包含了驱动程序运行所需的所有信息，
 * 包括设备号、字符设备结构、类、设备节点以及私有数据等。
 */
struct ap3216c_dev {
        dev_t devid;                    // 设备号(主设备号+次设备号)
        int major;                      // 主设备号
        int minor;                      // 次设备号
        struct class *class;            // 设备类指针，用于sysfs接口
        struct cdev cdev;               // 字符设备结构体
        struct device *device;          // 设备节点指针
        void *private_data;             // 私有数据指针，用于存储i2c_client
        unsigned short ir, ps, als;     // 传感器数据：红外线(ir)、接近(ps)、光照(als)
};

// 全局设备结构体实例
static struct ap3216c_dev ap3216c_dev;

/**
 * @brief 读取AP3216C的N个连续寄存器的值
 * 
 * 此函数通过I2C总线读取AP3216C传感器指定起始地址开始的多个连续寄存器的值。
 * 使用两步I2C传输：第一步写入要读取的寄存器地址，第二步读取数据。
 * 
 * @param dev ap3216c设备结构体指针
 * @param reg 起始寄存器地址
 * @param val 数据存储缓冲区指针
 * @param len 要读取的数据长度(字节数)
 * @return 成功返回传输的消息数(2)，失败返回负数错误码
 */
static int ap3216c_read_regs(struct ap3216c_dev *dev, u8 reg, void *val, int len) {
        // 定义两个I2C消息，一个用于发送寄存器地址，一个用于读取数据
        struct i2c_msg msgs[2];
        struct i2c_client *client = (struct i2c_client *)dev->private_data;

        // 第一条消息：发送要读取的寄存器地址
        msgs[0].addr = client->addr;     // 设备地址
        msgs[0].flags = 0;               // 表示写操作
        msgs[0].buf = &reg;              // 数据缓冲区指针，指向要读取的寄存器地址
        msgs[0].len = 1;                 // 发送的字节数，只发送一个寄存器地址

        // 第二条消息：读取寄存器数据
        msgs[1].addr = client->addr;     // 设备地址
        msgs[1].flags = I2C_M_RD;        // 表示读操作
        msgs[1].buf = val;               // 数据存储缓冲区
        msgs[1].len = len;               // 要读取的字节数

        // 获取I2C适配器(总线控制器)
        client->adapter = i2c_get_adapter(client->adapter->nr);

        // 执行I2C传输，共传输2条消息
        return i2c_transfer(client->adapter, msgs, 2);
}

/**
 * @brief 向AP3216C写入数据
 * 
 * 此函数通过I2C总线向AP3216C传感器的指定寄存器写入数据。
 * 写入时需要先发送寄存器地址，然后发送要写入的数据。
 * 
 * @param dev ap3216c设备结构体指针
 * @param reg 寄存器地址
 * @param buf 要写入的数据缓冲区指针
 * @param len 要写入的数据长度
 * @return 成功返回传输的消息数(1)，失败返回负数错误码
 */
static int ap3216c_write_regs(struct ap3216c_dev *dev, u8 reg, u8 *buf, int len) {
        char buffer[256];                // 临时缓冲区，用于组合寄存器地址和数据
        struct i2c_msg msgs;             // I2C消息结构体
        struct i2c_client *client = (struct i2c_client *)dev->private_data;

        buffer[0] = reg;                 // 第一个字节是寄存器地址
        memcpy(&buffer[1], buf, len);    // 后面是实际要写入的数据
        msgs.addr = client->addr;        // 设备地址
        msgs.flags = 0;                  // 表示写操作
        msgs.buf = buffer;               // 数据缓冲区
        msgs.len = len + 1;              // 数据长度（包括寄存器地址）
        return i2c_transfer(client->adapter, &msgs, 1); // 执行I2C传输，传输1条消息
}

/**
 * @brief 读取AP3216C单个寄存器的值
 * 
 * 这是一个便捷函数，用于读取AP3216C传感器单个寄存器的值。
 * 它是对ap3216c_read_regs函数的封装，专门用于读取单字节数据。
 * 
 * @param dev ap3216c设备结构体指针
 * @param reg 寄存器地址
 * @return 寄存器的值
 */
static unsigned char ap3216c_read_reg(struct ap3216c_dev *dev, u8 reg) {
        u8 data;                               // 存储读取的数据
        ap3216c_read_regs(dev, reg, &data, 1); // 调用多字节读取函数，读取1字节
        return data;                           // 返回读取的数据
}

/**
 * @brief 向AP3216C写入单个寄存器的值
 * 
 * 这是一个便捷函数，用于向AP3216C传感器单个寄存器写入值。
 * 它是对ap3216c_write_regs函数的封装，专门用于写入单字节数据。
 * 
 * @param dev ap3216c设备结构体指针
 * @param reg 寄存器地址
 * @param data 要写入的数据
 */
static void ap3216c_write_reg(struct ap3216c_dev *dev, u8 reg, u8 data) {
        u8 buf = 0;                            // 临时数据缓冲区
        buf = data;                            // 设置要写入的数据
        ap3216c_write_regs(dev, reg, &buf, 1); // 调用多字节写入函数，写入1字节
}

/**
 * @brief 打开设备文件时调用的函数
 * 
 * 当用户空间程序打开/dev/ap3216c设备文件时，此函数会被内核调用。
 * 函数完成设备初始化工作，包括设置系统配置寄存器使能传感器。
 * 
 * @param inode inode结构体指针，包含设备文件的元信息
 * @param filp 文件结构体指针，代表打开的文件
 * @return 成功返回0，失败返回负数错误码
 */
static int ap3216c_open(struct inode *inode, struct file *filp) {
        unsigned char val;
        filp->private_data = &ap3216c_dev;     // 将设备结构体赋值给文件私有数据
        printk("ap3216c_open\r\n");            // 打印调试信息
        
        // 初始化设备，配置AP3216C传感器
        ap3216c_write_reg(&ap3216c_dev, AP3216C_SYSTEMCONG, 0x4);  // 软件复位
        mdelay(50);                                                // 延时等待复位完成
        ap3216c_write_reg(&ap3216c_dev, AP3216C_SYSTEMCONG, 0x3);  // 使能ALS+PS+IR功能
        val = ap3216c_read_reg(&ap3216c_dev, AP3216C_SYSTEMCONG);  // 读回配置值验证
        printk("AP3216C_SYSTEMCONG=%#x\r\n", val);                 // 打印系统控制寄存器的值,验证是否写入成功
        return 0;
}

/**
 * @brief 关闭设备文件时调用的函数
 * 
 * 当用户空间程序关闭/dev/ap3216c设备文件时，此函数会被内核调用。
 * 
 * @param inode inode结构体指针，包含设备文件的元信息
 * @param filp 文件结构体指针，代表打开的文件
 * @return 成功返回0，失败返回负数错误码
 */
static int ap3216c_release(struct inode *inode, struct file *filp) {
        struct ap3216c_dev *dev = (struct ap3216c_dev *)filp->private_data; // 获取设备结构体
        printk("ap3216c close\r\n");                                        // 打印调试信息
        return 0;
}

/**
 * @brief 从AP3216C传感器读取数据
 * 
 * 该函数读取AP3216C传感器的IR、ALS和PS三种数据。
 * 根据数据手册，需要连续读取6个寄存器来获取完整的传感器数据。
 * 
 * @param dev ap3216c设备结构体指针
 */
void ap3216c_readdata(struct ap3216c_dev *dev) {
        unsigned char buf[6];    // 缓冲区，用于存储从传感器读取的原始数据
        unsigned char i = 0;

        // 连续读取6个寄存器的数据(从IR低字节到PS高字节)
        for (i = 0; i < 6; i++) {
                buf[i] = ap3216c_read_reg(dev, AP3216C_IRDATALOW + i);
        }
        
        // 检查数据有效性(通过IR数据的最高位判断)
        if (buf[0] & 0x80) {
                // 数据无效时，将所有传感器数据设为0
                dev->ir = 0;
                dev->ps = 0;
        } else {
                // 数据有效时，按照数据手册格式解析各传感器数据
                dev->ir = ((unsigned short)buf[1] << 8) | buf[0];     // IR数据由两个字节组成
                dev->ps = (((unsigned short)buf[5] & 0x3F) << 4) |    // PS数据由两个字节组成
                          (buf[4] & 0x0F);
        }
        // ALS数据也由两个字节组成
        dev->als = ((unsigned short)buf[3] << 8) | buf[2];
}

/**
 * @brief 从设备读取数据时调用的函数
 * 
 * 当用户空间程序从/dev/ap3216c设备文件读取数据时，此函数会被内核调用。
 * 函数会读取最新的传感器数据(IR、ALS、PS)并复制到用户空间。
 * 
 * @param filp 文件结构体指针
 * @param buf 用户空间缓冲区指针
 * @param count 要读取的数据长度
 * @param off 文件偏移量指针
 * @return 实际读取的字节数
 */
static ssize_t ap3216c_read(struct file *filp, char __user *buf, size_t count,
                            loff_t *off) {
        long ret;       
        short data[3];   // 用于存储三个传感器数据的数组
        struct ap3216c_dev *dev = (struct ap3216c_dev *)filp->private_data; // 获取设备结构体
        
        ap3216c_readdata(dev);  // 从传感器读取最新数据
        
        // 将传感器数据放入数组准备传递给用户空间
        data[0] = dev->ir;   // 红外线强度数据
        data[1] = dev->als;  // 环境光照强度数据
        data[2] = dev->ps;   // 接近距离数据
        
        // 将内核空间数据复制到用户空间
        ret = copy_to_user(buf, data, sizeof(data));
        return sizeof(data);  // 返回读取的字节数
}

// 字符设备操作函数集
struct file_operations ap3216c_fops = {
        .owner = THIS_MODULE,       // 指向当前模块
        .open = ap3216c_open,       // 打开设备函数
        .release = ap3216c_release, // 关闭设备函数
        .read = ap3216c_read,       // 读取设备函数
};

// 设备ID表，用于传统的I2C设备匹配
static struct i2c_device_id ap3216c_id[] = {{"lsc,ap3216c", 0}, {}};

// 设备树匹配表，用于设备树方式匹配设备
static struct of_device_id ap3216c_of_match[] = {
        {.compatible = "lsc,ap3216c"}, // 匹配设备树中的compatible属性
        {},
};

/**
 * @brief 驱动探测函数，当匹配到设备时调用
 * 
 * 当系统检测到与驱动匹配的AP3216C设备时，此函数会被内核调用。
 * 函数负责完成字符设备的注册、设备节点创建等工作。
 * 
 * @param client i2c_client结构体指针，代表匹配到的I2C设备
 * @param id i2c_device_id结构体指针，匹配的设备ID信息
 * @return 成功返回0，失败返回负数错误码
 */
static int ap3216c_probe(struct i2c_client *client,
                         const struct i2c_device_id *id) {
        int ret;
        printk("ap3216c_probe!\r\n"); // 打印调试信息

        // 搭建字符设备驱动框架,在/dev下生成设备文件
        ap3216c_dev.major = 0; // 初始化主设备号为0，表示动态分配

        // 注册设备号
        if (ap3216c_dev.major) {
                // 如果主设备号已指定，则使用静态注册
                ap3216c_dev.devid = MKDEV(ap3216c_dev.major, 0);
                ret = register_chrdev_region(ap3216c_dev.devid, AP3216C_CNT,
                                             AP3216C_NAME);
        } else {
                // 否则使用动态分配设备号
                ret = alloc_chrdev_region(&ap3216c_dev.devid, 0, AP3216C_CNT,
                                          AP3216C_NAME);
                ap3216c_dev.major = MAJOR(ap3216c_dev.devid); // 获取主设备号
                ap3216c_dev.minor = MINOR(ap3216c_dev.devid); // 获取次设备号
        }

        // 检查设备号注册是否成功
        if (ret) {
                printk("ap3216c register_chrdev_region err\n");
                return ret;
        }
        printk("ap3216c major=%d,minor=%d\n", ap3216c_dev.major,
               ap3216c_dev.minor);

        // 初始化并添加字符设备
        ap3216c_dev.cdev.owner = THIS_MODULE;
        cdev_init(&ap3216c_dev.cdev, &ap3216c_fops); // 初始化字符设备
        ret = cdev_add(&ap3216c_dev.cdev, ap3216c_dev.devid,
                       AP3216C_CNT); // 添加字符设备
        if (ret) {
                printk("ap3216c cdev_add err\n");
                return ret;
        }

        // 创建设备类，用于在/sys/class/下创建设备类目录
        ap3216c_dev.class = class_create(THIS_MODULE, AP3216C_NAME);
        if (IS_ERR(ap3216c_dev.class)) {
                printk("ap3216c class_create err\n");
                return PTR_ERR(ap3216c_dev.class);
        }

        // 创建设备节点，将在/dev/下创建设备文件
        ap3216c_dev.device = device_create(
                ap3216c_dev.class, NULL, ap3216c_dev.devid, NULL, AP3216C_NAME);
        if (IS_ERR(ap3216c_dev.device)) {
                printk("ap3216c device_create err\n");
                return PTR_ERR(ap3216c_dev.device);
        }

        // 保存i2c_client指针到私有数据中，供后续I2C通信使用
        ap3216c_dev.private_data = client;
        return 0;
}

/**
 * @brief 驱动移除函数，当设备被移除时调用
 * 
 * 当AP3216C设备从系统中移除时，此函数会被内核调用。
 * 函数负责释放驱动申请的各种资源，包括字符设备、设备号、设备节点等。
 * 
 * @param client i2c_client结构体指针，代表要移除的I2C设备
 * @return 成功返回0
 */
static int ap3216c_remove(struct i2c_client *client) {
        // 删除字符设备
        cdev_del(&ap3216c_dev.cdev);
        // 注销设备号
        unregister_chrdev_region(ap3216c_dev.devid, AP3216C_CNT);
        // 销毁设备节点
        device_destroy(ap3216c_dev.class, ap3216c_dev.devid);
        // 销毁设备类
        class_destroy(ap3216c_dev.class);
        return 0;
}

// I2C驱动结构体，定义了驱动的核心信息和回调函数
static struct i2c_driver ap3216c_driver = {
        .probe = ap3216c_probe,          // 探测函数，设备匹配时调用
        .remove = ap3216c_remove,        // 移除函数，设备移除时调用
        .driver =
                {
                        .name = "iic_ap3216c",           // 驱动名称
                        .owner = THIS_MODULE,            // 指向当前模块
                        .of_match_table = of_match_ptr(ap3216c_of_match), // 设备树匹配表
                },
        .id_table = ap3216c_id,          // 设备ID表
};

/**
 * @brief 模块初始化函数，在模块加载时调用
 * 
 * 当使用insmod或modprobe命令加载驱动模块时，此函数会被内核调用。
 * 函数负责向I2C子系统注册驱动程序。
 * 
 * @return 成功返回0
 */
static int __init iic_ap3216c_init(void) {
        int ret = 0;
        ret = i2c_add_driver(&ap3216c_driver); // 注册I2C驱动

        return 0;
}

/**
 * @brief 模块退出函数，在模块卸载时调用
 * 
 * 当使用rmmod命令卸载驱动模块时，此函数会被内核调用。
 * 函数负责从I2C子系统注销驱动程序。
 */
static void __exit iic_ap3216c_exit(void) {
        i2c_del_driver(&ap3216c_driver); // 注销I2C驱动
}

// 指定模块初始化和退出函数
module_init(iic_ap3216c_init);
module_exit(iic_ap3216c_exit);