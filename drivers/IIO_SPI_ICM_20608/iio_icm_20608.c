//#include "asm-generic/int-ll64.h"
#include "linux/device.h"
#include "linux/gfp.h"
#include "linux/iio/types.h"
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include <linux/delay.h>

#define ICM20_REG_WHO_AM_I 0x75
#define ICM20_REG_PWR_MGMT_1 0x6B

#define ICM20_REG_ACCEL_XOUT_H 0x3B

static const struct iio_chan_spec icm20608_channels[] = {{
        .type = IIO_ACCEL,
        .modified = 1,
        .channel2 = IIO_MOD_X,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
        .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
}};

struct icm20608_dev {
        struct spi_device *spi;
};
static int icm20608_read_reg(struct icm20608_dev *dev, u8 reg, u8 *buf,
                             u8 len) {
        u8 tx_data = reg | 0x80; // set bit 7 to read
        return spi_write_then_read(dev->spi, &tx_data, 1, buf, len);
}

static int icm20608_read_raw(struct iio_dev *indio_dev,
                             struct iio_chan_spec const *chan, int *val,
                             int *val2, long mask) {
        struct icm20608_dev *dev = iio_priv(indio_dev);
        int ret;
        u8 buf[2];

        switch (mask) {
        case IIO_CHAN_INFO_RAW:
                ret = icm20608_read_reg(dev, icm20608_channels[0].address, buf,
                                        2);
                if (ret < 0) {
                        return ret;
                }
        case IIO_VAL_INT_PLUS_MICRO:
                *val = 0;
                *val2 = 598;
                return IIO_VAL_INT_PLUS_MICRO;
        default:
                return -EINVAL;
        }
}

static const struct iio_info icm20608_info = {
        .read_raw = icm20608_read_raw,
        .driver_module = THIS_MODULE,
};

// write reg
static int icm20608_write_reg(struct icm20608_dev *dev, u8 reg, u8 data) {
        u8 buf[2];
        buf[0] = reg & 0x70; // clear bit 7 to write
        buf[1] = data;
        return spi_write(dev->spi, buf, 2);
}
static const struct of_device_id icm20608_of_match[] = {
        {.compatible = "invn,icm20608"}, {}};

MODULE_DEVICE_TABLE(of, icm20608_of_match);

static int icm20608_probe(struct spi_device *spi) {
        struct icm20608_dev *dev;
        u8 chip_id;
        int ret;
        struct iio_dev *indio_dev;

        // dev=devm_kzalloc(&spi->dev, sizeof(*dev),GFP_KERNEL);// allocate
        // memory
        // if(!dev){
        //         return -ENOMEM;
        // }
        indio_dev =
                devm_iio_device_alloc(&spi->dev, sizeof(struct icm20608_dev));
        if (!indio_dev) {
                return -ENOMEM;
        }
        dev = iio_priv(indio_dev);
        dev->spi = spi;
        // spi_set_drvdata(spi, dev);// store dev in spi

        spi->mode = SPI_MODE_0; // SPI mode 0
        spi_setup(spi);

        icm20608_write_reg(dev, ICM20_REG_PWR_MGMT_1, 0x80);
        mdelay(50);
        icm20608_write_reg(dev, ICM20_REG_PWR_MGMT_1, 0x01);
        mdelay(50);

        ret = icm20608_read_reg(dev, ICM20_REG_WHO_AM_I, &chip_id, 1);
        if (ret < 0) {
                dev_err(&spi->dev, "read WHO_AM_I faild!\r\n");
                return ret;
        }

        indio_dev->dev.parent = &spi->dev;
        indio_dev->name = "icm20608";
        indio_dev->info = &icm20608_info;
        indio_dev->modes = INDIO_DIRECT_MODE;
        indio_dev->channels = icm20608_channels;
        indio_dev->num_channels = ARRAY_SIZE(icm20608_channels);

        return devm_iio_device_register(&spi->dev, indio_dev);
}

static int icm20608_remove(struct spi_device *spi) {
        dev_info(&spi->dev, "icm20608_remove\n");
        return 0;
}

static struct spi_driver icm20608_driver = {
        .driver =
                {
                        .name = "icm20608", .of_match_table = icm20608_of_match,
                },
        .probe = icm20608_probe,
        .remove = icm20608_remove,
};


module_spi_driver(icm20608_driver);

MODULE_AUTHOR("TATAROSE");
MODULE_LICENSE("GPL");
