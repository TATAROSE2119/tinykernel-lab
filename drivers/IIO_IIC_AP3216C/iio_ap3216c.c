#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/types.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include <linux/delay.h>

static const struct iio_chan_spec ap3216c_channels[] = {
        {
                .type = IIO_INTENSITY,
                .modified = 1,
                .channel2 = IIO_MOD_LIGHT,
                .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
                .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
                .address = 0x0A, // ALS数据寄存器地址
        },
        {
                .type = IIO_INTENSITY,
                .modified = 1,
                .channel2 = IIO_MOD_IR,
                .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
                .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
                .address = 0x0C, // IR数据寄存器地址
        },
        {
                .type = IIO_PROXIMITY,
                .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
                .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
                .address = 0x0E, // PS数据寄存器地址
        },
};

struct ap3216c_iio_dev {
        struct i2c_client *client;
        struct mutex lock;
};

static int ap3216c_read_regs(struct ap3216c_iio_dev *dev, u8 reg, u8 *buf,
                             int len) {
        int ret;
        struct i2c_msg msgs[2];
        struct i2c_client *client = (struct ap3216c_iio_dev *)dev->client;

        // 第一条消息：发送要读取的寄存器地址
        msgs[0].addr = client->addr; // 设备地址
        msgs[0].flags = 0;           // 表示写操作
        msgs[0].buf = &reg; // 数据缓冲区指针，指向要读取的寄存器地址
        msgs[0].len = 1; // 发送的字节数，只发送一个寄存器地址

        // 第二条消息：读取数据
        msgs[1].addr = client->addr; // 设备地址
        msgs[1].flags = I2C_M_RD;    // 表示读操作
        msgs[1].buf = buf;           // 数据存储缓冲区
        msgs[1].len = len;           // 要读取的字节数

        ret = i2c_transfer(client->adapter, msgs, 2);
        if (ret < 0) {
                return ret;
        }
        if (ret != 2) {
                return -EIO;
        }
        return 0;
}
static int ap3216c_write_regs(struct ap3216c_iio_dev *dev, u8 reg, u8 *buf,
                              int len) {
        int ret;
        char buffer[256]; // 临时缓冲区，用于组合寄存器地址和数据
        struct i2c_msg msgs; // I2C消息结构体
        struct i2c_client *client = (struct i2c_client *)dev->client;

        buffer[0] = reg;                 // 第一个字节是寄存器地址
        memcpy(&buffer[1], buf, len);    // 后面是实际要写入的数据
        msgs.addr = client->addr;        // 设备地址
        msgs.flags = 0;                  // 表示写操作
        msgs.buf = buffer;               // 数据缓冲区
        msgs.len = len + 1;              // 数据长度（包括寄存器地址
        
        ret=i2c_transfer(client->adapter, &msgs, 1); // 执行I2C传输，传输1条消息
        return ret;
}

void ap3216c_read_data(struct ap3216c_iio_dev *dev, short *data) {
        u8 buf[6]; // 用于存储读取的6字节数据
        int ret;

        // 读取6字节数据，从寄存器0x0A开始
        ret = ap3216c_read_regs(dev, 0x0A, buf, 6);
        if (ret < 0) {
                printk("Failed to read data from AP3216C: %d\n", ret);
                return;
        }

        // 将读取的字节数据转换为传感器数据
        data[0] = (buf[1] << 8) | buf[0]; // IR数据
        data[1] = (buf[3] << 8) | buf[2]; // ALS数据
        data[2] = (buf[5] << 8) | buf[4]; // PS数据
}
