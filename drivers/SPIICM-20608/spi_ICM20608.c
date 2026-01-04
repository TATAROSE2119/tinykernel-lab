/* #region include */
#include "asm-generic/int-ll64.h"
#include "asm/uaccess.h"
#include "linux/gfp.h"
#include "linux/slab.h"
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "icm20608reg.h"

/* #endregion */

#define ICM20608_NAME "icm20608"
#define ICM20608_COUNT 1

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TATAROSE");

struct icm20608_dev {
        dev_t devid;
        int major;
        int minor;
        struct class *class;
        struct cdev cdev;
        struct device *device;
        void *private_data;
        struct device_node *nd;

        int cs_gpio;

        signed int gyro_x_adc;
        signed int gyro_y_adc;
        signed int gyro_z_adc;
        signed int accel_x_adc;
        signed int accel_y_adc;
        signed int accel_z_adc;
        signed int temp_adc;
};
static struct icm20608_dev icm20608_dev;

/* SPI读寄存器 */
static int spi_icm20608_read_regs(struct icm20608_dev *dev, u8 reg, u8 *buf,
                                  u8 len) {
        int ret;
        struct spi_device *spi = dev->private_data;
        u8 tx_buf[1] = {reg | 0x80}; // 读命令
        struct spi_transfer t[] = {
                {
                        .tx_buf = tx_buf, .len = 1,
                },
                {
                        .rx_buf = buf, .len = len,
                },
        };

        // 手动控制片选（推荐方式）
        gpio_set_value(dev->cs_gpio, 0);
        ret = spi_sync_transfer(spi, t, ARRAY_SIZE(t));
        gpio_set_value(dev->cs_gpio, 1);

        return ret;
}

/* SPI写寄存器 */
/* 正确的写寄存器函数（和读一样用 spi_sync_transfer） */
static int spi_icm20608_write_reg(struct icm20608_dev *dev, u8 reg, u8 value) {
        int ret;
        struct spi_device *spi = dev->private_data;
        u8 tx_buf[2] = {reg & 0x7F,
                        value}; // 第1字节：地址（bit7=0表示写） + 第2字节：数据
        struct spi_transfer t = {
                .tx_buf = tx_buf, .len = 2,
        };

        gpio_set_value(dev->cs_gpio, 0);
        // ret = spi_write(spi, tx_buf, 2); // 或者用 spi_sync_transfer 也行
        // 下面这句也行，效果一样：
        ret = spi_sync_transfer(spi, &t, 1);
        gpio_set_value(dev->cs_gpio, 1);

        return ret;
} // 读取单个寄存器
static u8 spi_icm20608_read_reg(struct icm20608_dev *dev, u8 reg) {
        u8 data = 0;
        spi_icm20608_read_regs(dev, reg, &data, 1);
        return data;
}
int spi_icm20608_reg_init(struct icm20608_dev *dev) {
        u8 id;
        u8 value = 0;

        /* 复位设备 */
        spi_icm20608_write_reg(dev, ICM20_PWR_MGMT_1, 0x80);
        mdelay(10);

        /* 唤醒 + 使用最佳时钟源 */
        spi_icm20608_write_reg(dev, ICM20_PWR_MGMT_1, 0x01); // 自动选择最佳时钟
        mdelay(10);

        id = spi_icm20608_read_reg(dev, ICM20_WHO_AM_I);
        dev_info(dev->device, "ICM20608 WHO_AM_I = 0x%02X (should be 0xAF)\n",
                 id);

        spi_icm20608_write_reg(dev, ICM20_SMPLRT_DIV, 0X00);
        spi_icm20608_write_reg(dev, ICM20_GYRO_CONFIG, 0x18);
        spi_icm20608_write_reg(dev, ICM20_ACCEL_CONFIG, 0x18);
        spi_icm20608_write_reg(dev, ICM20_CONFIG, 0x04);
        spi_icm20608_write_reg(dev, ICM20_ACCEL_CONFIG2, 0x04);
        spi_icm20608_write_reg(dev, ICM20_PWR_MGMT_2, 0x00);
        spi_icm20608_write_reg(dev, ICM20_LP_MODE_CFG, 0x00);
        spi_icm20608_write_reg(dev, ICM20_FIFO_EN, 0x00);

        dev_info(dev->device, "ICM20608 initialized successfully!\n");
        return 0; // 必须返回 0！
}
void icm20608_readdata(struct icm20608_dev *dev) {
        unsigned char data[14];
        spi_icm20608_read_regs(dev, ICM20_ACCEL_XOUT_H, data, 14);

        dev->accel_x_adc = (signed short)((data[0] << 8) | data[1]);
        dev->accel_y_adc = (signed short)((data[2] << 8) | data[3]);
        dev->accel_z_adc = (signed short)((data[4] << 8) | data[5]);
        dev->temp_adc = (signed short)((data[6] << 8) | data[7]);
        dev->gyro_x_adc = (signed short)((data[8] << 8) | data[9]);
        dev->gyro_y_adc = (signed short)((data[10] << 8) | data[11]);
        dev->gyro_z_adc = (signed short)((data[12] << 8) | data[13]);
}
static int icm20608_open(struct inode *inode, struct file *filp) {
        filp->private_data = &icm20608_dev;
        struct spi_device *spi = (struct spi_device *)icm20608_dev.private_data;

        if (spi)
                dev_info(&spi->dev, "icm20608_open\r\n");
        return 0;
}
static int icm20608_release(struct inode *inode, struct file *filp) {
        struct spi_device *spi = (struct spi_device *)icm20608_dev.private_data;

        if (spi)
                dev_info(&spi->dev, "icm20608_release\r\n");
        return 0;
}
static ssize_t icm20608_read(struct file *filp, char __user *buf, size_t count,
                             loff_t *offp) {
        signed int data[7];
        long err = 0;
        struct icm20608_dev *dev = (struct icm20608_dev *)filp->private_data;

        icm20608_readdata(dev);
        data[0] = dev->accel_x_adc;
        data[1] = dev->accel_y_adc;
        data[2] = dev->accel_z_adc;
        data[3] = dev->temp_adc;
        data[4] = dev->gyro_x_adc;
        data[5] = dev->gyro_y_adc;
        data[6] = dev->gyro_z_adc;
        err = copy_to_user(buf, data, sizeof(data));
        if (err)
                return -EFAULT;
        return sizeof(data);
}

struct file_operations icm20608_fops = {
        .owner = THIS_MODULE,
        .open = icm20608_open,
        .release = icm20608_release,
        .read = icm20608_read,
};

static int icm20608_probe(struct spi_device *spi) {
        int ret;
        dev_info(&spi->dev, "icm20608_probe\r\n");

        icm20608_dev.major = 0;
        if (icm20608_dev.major) {
                icm20608_dev.devid = MKDEV(icm20608_dev.major, 0);
                ret = register_chrdev_region(icm20608_dev.devid, ICM20608_COUNT,
                                             ICM20608_NAME);
        } else {
                ret = alloc_chrdev_region(&icm20608_dev.devid, 0,
                                          ICM20608_COUNT, ICM20608_NAME);
                icm20608_dev.major = MAJOR(icm20608_dev.devid);
                icm20608_dev.minor = MINOR(icm20608_dev.devid);
        }

        if (ret) {
                dev_err(&spi->dev, "alloc_chrdev_region failed\r\n");
                return ret;
        }
        dev_info(&spi->dev, "icm20608 major=%d,minor=%d\r\n",
                 icm20608_dev.major, icm20608_dev.minor);

        icm20608_dev.cdev.owner = THIS_MODULE;
        cdev_init(&icm20608_dev.cdev, &icm20608_fops);
        ret = cdev_add(&icm20608_dev.cdev, icm20608_dev.devid, ICM20608_COUNT);
        if (ret) {
                dev_err(&spi->dev, "cdev_add failed\r\n");
                return ret;
        }

        icm20608_dev.class = class_create(THIS_MODULE, ICM20608_NAME);
        if (IS_ERR(icm20608_dev.class)) {
                dev_err(&spi->dev, "class_create failed\r\n");
                return PTR_ERR(icm20608_dev.class);
        }
        icm20608_dev.device =
                device_create(icm20608_dev.class, NULL, icm20608_dev.devid,
                              NULL, ICM20608_NAME);
        if (IS_ERR(icm20608_dev.device)) {
                dev_err(&spi->dev, "device_create failed\r\n");
                return PTR_ERR(icm20608_dev.device);
        }

        /* 获取cs gpio */
        icm20608_dev.nd = of_get_parent(spi->dev.of_node);
        icm20608_dev.cs_gpio = of_get_named_gpio(icm20608_dev.nd, "cs-gpios", 0);
        if (icm20608_dev.cs_gpio < 0) {
                dev_err(&spi->dev, "get cs gpio failed\r\n");
                return -EINVAL;
        } else {
                dev_info(&spi->dev, "cs gpio num: %d\r\n",
                         icm20608_dev.cs_gpio);
        }

        // ret = gpio_request(icm20608_dev.cs_gpio, "cs");
        // if (ret < 0) {
        //         dev_err(&spi->dev, "gpio_request failed\r\n");
        //         return ret;
        // }
        // ret = gpio_direction_output(icm20608_dev.cs_gpio, 1); /* 默认置高 */
        // if (ret < 0) {
        //         dev_err(&spi->dev, "gpio_direction_output failed\r\n");
        //         return ret;
        // }
        
        spi->mode = SPI_MODE_0;
        spi_setup(spi);

        icm20608_dev.private_data = spi;

        ret = spi_icm20608_reg_init(&icm20608_dev);
        if (ret) {
                dev_err(&spi->dev, "ICM20608 init failed\r\n");
        }
        return ret;
}
static int icm20608_remove(struct spi_device *spi) {
        cdev_del(&icm20608_dev.cdev);
        unregister_chrdev_region(icm20608_dev.devid, ICM20608_COUNT);
        device_destroy(icm20608_dev.class, icm20608_dev.devid);
        class_destroy(icm20608_dev.class);
        if (gpio_is_valid(icm20608_dev.cs_gpio)) {
                gpio_free(icm20608_dev.cs_gpio);
        }
        dev_info(&spi->dev, "icm20608 removed\r\n");
        return 0;
}

struct spi_device_id icm20608_id[] = {{"invn,icm20608", 0}, {}};
static const struct of_device_id icm20608_of_match[] = {
        {.compatible = "invn,icm20608"}, {}};

/* spi driver */
struct spi_driver icm20608_driver = {
        .driver =
                {
                        .name = "icm20608",
                        .owner = THIS_MODULE,
                        .of_match_table = icm20608_of_match,
                },
        .id_table = icm20608_id,
        .probe = icm20608_probe,
        .remove = icm20608_remove,
};
static int __init spi_icm20608_init(void) {
        int ret;
        ret = spi_register_driver(&icm20608_driver);
        return 0;
}

static void __exit spi_icm20608_exit(void) {
        spi_unregister_driver(&icm20608_driver);
}

module_init(spi_icm20608_init);
module_exit(spi_icm20608_exit);
