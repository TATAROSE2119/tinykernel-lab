#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/types.h>

#define AP3216C_REG_SYSTEM_CONFIG 0x00
#define AP3216C_REG_IR_DATA_LOW 0x0a
#define AP3216C_REG_ALS_DATA_LOW 0x0c
#define AP3216C_REG_PS_DATA_LOW 0x0e

#define AP3216C_MODE_POWER_DOWN 0x00
#define AP3216C_MODE_ALS_PS_IR 0x03
#define AP3216C_MODE_SW_RESET 0x04

#define AP3216C_IR_OVERFLOW BIT(7)
#define AP3216C_IR_DATA_LOW_MASK GENMASK(1, 0)
#define AP3216C_PS_IR_OVERFLOW BIT(6)
#define AP3216C_PS_DATA_LOW_MASK GENMASK(3, 0)
#define AP3216C_PS_DATA_HIGH_MASK GENMASK(5, 0)

/* Default ALS range after reset: 0 to 20661 lux, 0.35 lux/count. */
#define AP3216C_ALS_SCALE_MICRO 350000

struct ap3216c_iio_dev {
        struct i2c_client *client;
        struct mutex lock;
};

static const struct iio_chan_spec ap3216c_channels[] = {
        {
                .type = IIO_LIGHT,
                .info_mask_separate =
                        BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
                .address = AP3216C_REG_ALS_DATA_LOW,
        },
        {
                .type = IIO_INTENSITY,
                .modified = 1,
                .channel2 = IIO_MOD_LIGHT_IR,
                .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
                .address = AP3216C_REG_IR_DATA_LOW,
        },
        {
                .type = IIO_PROXIMITY,
                .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
                .address = AP3216C_REG_PS_DATA_LOW,
        },
};

static int ap3216c_read_regs(struct ap3216c_iio_dev *data, u8 reg, u8 *buf,
                             int len) {
        struct i2c_client *client = data->client;
        struct i2c_msg msgs[2] = {
                {
                        .addr = client->addr,
                        .flags = client->flags,
                        .buf = &reg,
                        .len = 1,
                },
                {
                        .addr = client->addr,
                        .flags = client->flags | I2C_M_RD,
                        .buf = buf,
                        .len = len,
                },
        };
        int ret;

        ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
        if (ret < 0)
                return ret;
        if (ret != ARRAY_SIZE(msgs))
                return -EIO;

        return 0;
}

static int ap3216c_write_reg(struct ap3216c_iio_dev *data, u8 reg, u8 value) {
        struct i2c_client *client = data->client;
        u8 buf[] = {reg, value};
        struct i2c_msg msg = {
                .addr = client->addr,
                .flags = client->flags,
                .buf = buf,
                .len = sizeof(buf),
        };
        int ret;

        ret = i2c_transfer(client->adapter, &msg, 1);
        if (ret < 0)
                return ret;
        if (ret != 1)
                return -EIO;

        return 0;
}

static int ap3216c_read_channel(struct ap3216c_iio_dev *data,
                                const struct iio_chan_spec *chan, int *val) {
        u8 buf[2];
        int ret;

        mutex_lock(&data->lock);
        ret = ap3216c_read_regs(data, chan->address, buf, sizeof(buf));
        mutex_unlock(&data->lock);
        if (ret)
                return ret;

        switch (chan->address) {
        case AP3216C_REG_ALS_DATA_LOW:
                *val = ((u16)buf[1] << 8) | buf[0];
                break;
        case AP3216C_REG_IR_DATA_LOW:
                if (buf[0] & AP3216C_IR_OVERFLOW)
                        return -EOVERFLOW;
                *val = ((u16)buf[1] << 2) | (buf[0] & AP3216C_IR_DATA_LOW_MASK);
                break;
        case AP3216C_REG_PS_DATA_LOW:
                if ((buf[0] | buf[1]) & AP3216C_PS_IR_OVERFLOW)
                        return -EOVERFLOW;
                *val = ((u16)(buf[1] & AP3216C_PS_DATA_HIGH_MASK) << 4) |
                       (buf[0] & AP3216C_PS_DATA_LOW_MASK);
                break;
        default:
                return -EINVAL;
        }

        return IIO_VAL_INT;
}

static int ap3216c_read_raw(struct iio_dev *indio_dev,
                            const struct iio_chan_spec *chan, int *val,
                            int *val2, long mask) {
        struct ap3216c_iio_dev *data = iio_priv(indio_dev);

        switch (mask) {
        case IIO_CHAN_INFO_RAW:
                return ap3216c_read_channel(data, chan, val);
        case IIO_CHAN_INFO_SCALE:
                if (chan->address != AP3216C_REG_ALS_DATA_LOW)
                        return -EINVAL;

                *val = 0;
                *val2 = AP3216C_ALS_SCALE_MICRO;
                return IIO_VAL_INT_PLUS_MICRO;
        default:
                return -EINVAL;
        }
}

static const struct iio_info ap3216c_info = {
        .read_raw = ap3216c_read_raw,
        .driver_module = THIS_MODULE,
};

static int ap3216c_chip_init(struct ap3216c_iio_dev *data) {
        u8 mode;
        int ret;

        ret = ap3216c_write_reg(data, AP3216C_REG_SYSTEM_CONFIG,
                                AP3216C_MODE_SW_RESET);
        if (ret)
                return ret;

        /* The chip must not receive another command for 10 ms after reset. */
        msleep(15);

        ret = ap3216c_write_reg(data, AP3216C_REG_SYSTEM_CONFIG,
                                AP3216C_MODE_ALS_PS_IR);
        if (ret)
                return ret;

        ret = ap3216c_read_regs(data, AP3216C_REG_SYSTEM_CONFIG, &mode, 1);
        if (ret)
                return ret;
        if ((mode & GENMASK(2, 0)) != AP3216C_MODE_ALS_PS_IR)
                return -ENODEV;

        return 0;
}

static int ap3216c_probe(struct i2c_client *client,
                         const struct i2c_device_id *id) {
        struct ap3216c_iio_dev *data;
        struct iio_dev *indio_dev;
        int ret;

        if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
                return -EOPNOTSUPP;

        indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
        if (!indio_dev)
                return -ENOMEM;

        data = iio_priv(indio_dev);
        data->client = client;
        mutex_init(&data->lock);
        i2c_set_clientdata(client, indio_dev);

        ret = ap3216c_chip_init(data);
        if (ret) {
                ap3216c_write_reg(data, AP3216C_REG_SYSTEM_CONFIG,
                                  AP3216C_MODE_POWER_DOWN);
                dev_err(&client->dev, "failed to initialize sensor: %d\n", ret);
                return ret;
        }

        indio_dev->dev.parent = &client->dev;
        indio_dev->name = "ap3216c";
        indio_dev->info = &ap3216c_info;
        indio_dev->modes = INDIO_DIRECT_MODE;
        indio_dev->channels = ap3216c_channels;
        indio_dev->num_channels = ARRAY_SIZE(ap3216c_channels);

        ret = devm_iio_device_register(&client->dev, indio_dev);
        if (ret) {
                ap3216c_write_reg(data, AP3216C_REG_SYSTEM_CONFIG,
                                  AP3216C_MODE_POWER_DOWN);
                return ret;
        }

        dev_info(&client->dev,
                 "AP3216C ambient light/proximity sensor ready\n");
        return 0;
}

static int ap3216c_remove(struct i2c_client *client) {
        struct iio_dev *indio_dev = i2c_get_clientdata(client);
        struct ap3216c_iio_dev *data = iio_priv(indio_dev);
        int ret;

        ret = ap3216c_write_reg(data, AP3216C_REG_SYSTEM_CONFIG,
                                AP3216C_MODE_POWER_DOWN);
        if (ret)
                dev_warn(&client->dev, "failed to power down sensor: %d\n",
                         ret);

        return 0;
}

static const struct of_device_id ap3216c_of_match[] = {
        {.compatible = "lsc,ap3216c"}, {}};
MODULE_DEVICE_TABLE(of, ap3216c_of_match);

static const struct i2c_device_id ap3216c_id[] = {{"ap3216c", 0}, {}};
MODULE_DEVICE_TABLE(i2c, ap3216c_id);

static struct i2c_driver ap3216c_driver = {
        .driver =
                {
                        .name = "iio_ap3216c",
                        .owner = THIS_MODULE,
                        .of_match_table = of_match_ptr(ap3216c_of_match),
                },
        .probe = ap3216c_probe,
        .remove = ap3216c_remove,
        .id_table = ap3216c_id,
};
module_i2c_driver(ap3216c_driver);

MODULE_SOFTDEP("pre: industrialio");
MODULE_AUTHOR("TATAROSE");
MODULE_DESCRIPTION("AP3216C ambient light, infrared and proximity sensor");
MODULE_LICENSE("GPL");
