#include "asm/gpio.h"
#include "asm/string.h"
#include "linux/fs.h"
#include "linux/interrupt.h"
#include "linux/mm.h"
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

#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#define LED_CNT 1
#define LED_NAME "led-dts-platform"     //显示在 /dev 文件夹中显示的名字
#define LED_ON 1
#define LED_OFF 0
#define LED_NAME_IN_DTS "gpio-leds"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TATAROSE");

// gpioled dev struct
struct gpioled_dev {
        dev_t devid;            // 设备号
        struct cdev cdev;       // cdev 结构体
        struct class *class;    // 类
        struct device *device;  // 设备
        int major;              // 主设备号
        int minor;              // 次设备号
        struct device_node *nd; // 设备节点
        int led_gpio_num;       // led gpio num
};
struct gpioled_dev gpioled;
// file ops struct
static int gpioled_open(struct inode *inode, struct file *filp) {
        filp->private_data = &gpioled;
        printk("gpioled_open\r\n");
        return 0;
}
static ssize_t led_write(struct file *filp, const char __user *buf,
                         size_t count, loff_t *ppos) {
        int ret = 0;
        unsigned char databuf[1];
        unsigned char led_status;
        struct gpioled_dev *dev = filp->private_data; // 获取设备结构体的

        ret = copy_from_user(databuf, buf, count);
        if (ret) {
                printk("copy_from_user failed!\n");
                return -EFAULT;
        }
        led_status = databuf[0];// 获取数据
        if(led_status==LED_OFF){
                gpio_set_value(dev->led_gpio_num,LED_ON);
        }else{
                gpio_set_value(dev->led_gpio_num,LED_OFF);
        }
        printk("led_write\r\n");
        return 0;
}
static int gpioled_release(struct inode *inode, struct file *filp) {
        printk("gpioled_release\r\n");
        return 0;
}

struct file_operations gpioled_fops = {
        .owner = THIS_MODULE,
        .open = gpioled_open,
        .write = led_write,
        .release = gpioled_release,
};

static int __init led_init(void) {
        int return_value = 0;
        // register_chrdev
        gpioled.major = 0;
        if (gpioled.major) {
                gpioled.devid = MKDEV(gpioled.major, 0); // 注册设备号
                register_chrdev_region(gpioled.devid, LED_CNT,
                                       LED_NAME); // 注册设备号
        } else {
                alloc_chrdev_region(&gpioled.devid, 0, LED_CNT, LED_NAME);
                gpioled.major = MAJOR(gpioled.devid);
                gpioled.minor = MINOR(gpioled.devid);
                printk("major = %d, minor = %d\n", gpioled.major,
                       gpioled.minor);
        }
        // cdev init
        gpioled.cdev.owner = THIS_MODULE;        // 设置cdev结构体的owner
        cdev_init(&gpioled.cdev, &gpioled_fops); // 初始化cdev结构体

        // cdev add
        return_value = cdev_add(&gpioled.cdev, gpioled.devid, LED_CNT);
        if (return_value) {
                printk("cdev_add failed!\n");
                return return_value;
        }

        // class create
        gpioled.class = class_create(THIS_MODULE, LED_NAME); // 创建类
        if (IS_ERR(gpioled.class)) {
                printk("class_create failed!\n");
                return PTR_ERR(gpioled.class);
        }
        // device create
        gpioled.device = device_create(gpioled.class, NULL, gpioled.devid, NULL,
                                       LED_NAME);
        if (IS_ERR(gpioled.device)) {
                printk("device_create failed!\n");
                return PTR_ERR(gpioled.device);
        }

        // get device node
        gpioled.nd = of_find_node_by_path("/gpioled");
        if (gpioled.nd == NULL) {
                printk("gpioled node not found!\n");
                return -EINVAL;
        }
        // get gpio num
        gpioled.led_gpio_num =
                of_get_named_gpio(gpioled.nd, "led-gpios", 0); // 获取gpio num
        if (gpioled.led_gpio_num < 0) {
                printk("get led-gpio num failed!\n");
                return -EINVAL;
        } else {
                printk("led-gpio num = %d\n", gpioled.led_gpio_num);
        }
        // request gpio number
        return_value = gpio_request(gpioled.led_gpio_num, "led-gpio");
        if (return_value) {
                printk("gpio_request failed!\n");
                return return_value;
        }
        // set gpio direction
        return_value = gpio_direction_output(gpioled.led_gpio_num,
                                             1); // 设置gpio方向为输出
        if (return_value) {
                printk("gpio_direction_output failed!\n");
                return return_value;
        }

        gpio_set_value(gpioled.led_gpio_num, 0); // 设置gpio值为0
        return 0;
}
static void __exit led_exit(void) {
        // unregister_chrdev
        cdev_del(&gpioled.cdev);
        unregister_chrdev_region(gpioled.devid, LED_CNT);
        device_destroy(gpioled.class, gpioled.devid);
        class_destroy(gpioled.class);
        // free gpio
        gpio_free(gpioled.led_gpio_num);
}
static int gpioled_probe(struct platform_device *pdev) {
        led_init();
        printk("gpioled_probe\r\n");
        return 0;
}
static int gpioled_remove(struct platform_device *pdev) {
        printk("gpioled_remove\r\n");
        led_exit();
        return 0;
}
static const struct of_device_id gpioled_of_match[] = {
        // 匹配表
        {
                .compatible = LED_NAME_IN_DTS,
        },
        {}, // 结束标记
};

static struct platform_driver gpioled_driver = {
        // 驱动结构体
        .probe = gpioled_probe,   // 匹配成功时调用
        .remove = gpioled_remove, // 移除时调用
        .driver =
                {
                        .name = "imx6ull-led",  // 驱动名
                        .owner = THIS_MODULE,
                        .of_match_table = gpioled_of_match, // 匹配
                },
};
static int __init led_driver_init(void) {
        // led_init();
        printk("led_driver_init\r\n");
        return platform_driver_register(&gpioled_driver);
}
static void __exit led_driver_exit(void) {
        // led_exit();
        platform_driver_unregister(&gpioled_driver);
        printk("led_driver_exit\r\n");
}

module_init(led_driver_init);
module_exit(led_driver_exit);