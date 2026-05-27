#include <linux/types.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/iio/types.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include <linux/delay.h>

#define ICM20_REG_WHO_AM_I 0x75   // ICM-20608 chip ID should be 0xAF
#define ICM20_REG_PWR_MGMT_1 0x6B // power management register 1

#define ICM20_REG_ACCEL_XOUT_H 0x3B // accelerometer X-axis high byte register
#define ICM20_REG_ACCEL_XOUT_L 0x3C // accelerometer X-axis low byte register
#define ICM20_REG_ACCEL_YOUT_H 0x3D // accelerometer Y-axis high byte register
#define ICM20_REG_ACCEL_YOUT_L 0x3E // accelerometer Y-axis low byte register
#define ICM20_REG_ACCEL_ZOUT_H 0x3F // accelerometer Z-axis high byte register
#define ICM20_REG_ACCEL_ZOUT_L 0x40 // accelerometer Z-axis low byte register

#define ICM20_REG_GYRO_XOUT_H 0x43 // gyroscope X-axis high byte register
#define ICM20_REG_GYRO_XOUT_L 0x44 // gyroscope X
#define ICM20_REG_GYRO_YOUT_H 0x45 // gyroscope Y-axis high byte register
#define ICM20_REG_GYRO_YOUT_L 0x46 // gyroscope Y-axis low byte register
#define ICM20_REG_GYRO_ZOUT_H 0x47 // gyroscope Z-axis high byte register
#define ICM20_REG_GYRO_ZOUT_L 0x48 // gyroscope Z-axis low byte register


#define ICM20_REG_TEMP_OUT_H 0x41 // temperature high byte register
#define ICM20_REG_TEMP_OUT_L 0x42 // temperature low byte register

#define ICM20_REG_ACCEL_CONFIG2 0x1D
#define ICM20_REG_GYRO_CONFIG  0x1B
#define ICM20_REG_ACCEL_CONFIG 0x1C

static const struct iio_chan_spec icm20608_channels[] = {
        {
                .type = IIO_ACCEL,
                .modified = 1,
                .channel2 = IIO_MOD_X,
                .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
                .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
                .address = ICM20_REG_ACCEL_XOUT_H,
        },
        {
                .type = IIO_ACCEL,
                .modified = 1,
                .channel2 = IIO_MOD_Y,
                .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
                .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
                .address = ICM20_REG_ACCEL_YOUT_H,
        },
        {
                .type = IIO_ACCEL,
                .modified = 1,
                .channel2 = IIO_MOD_Z,
                .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
                .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
                .address = ICM20_REG_ACCEL_ZOUT_H,
        },
        {
                .type = IIO_ANGL_VEL,
                .modified = 1,
                .channel2 = IIO_MOD_X,
                .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
                .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
                .address = ICM20_REG_GYRO_XOUT_H,
        },
        {
                .type = IIO_ANGL_VEL,
                .modified = 1,
                .channel2 = IIO_MOD_Y,
                .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
                .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
                .address = ICM20_REG_GYRO_YOUT_H,
        },
        {
                .type = IIO_ANGL_VEL,
                .modified = 1,
                .channel2 = IIO_MOD_Z,
                .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
                .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
                .address = ICM20_REG_GYRO_ZOUT_H,
        },
        {
                .type = IIO_TEMP,
                .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
                .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
                .address = ICM20_REG_TEMP_OUT_H,
        },

};

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

        switch (mask) {         // check what info is requested
        case IIO_CHAN_INFO_RAW: // read raw value
                ret = icm20608_read_reg(dev, chan->address, buf, 2);
                if (ret < 0) {      // read error
                        return ret; // return error code
                }
                *val = (s16)((buf[0] << 8) |
                             buf[1]); // combine high and low bytes
                return IIO_VAL_INT;   // return raw value
        case IIO_CHAN_INFO_SCALE:     // return scale factor
                switch (chan->type) {
                        case IIO_ACCEL:
                                *val = 0;
                                *val2 = 598; // 0.000598 m/s²/LSB (±2g)
                                return IIO_VAL_INT_PLUS_MICRO;
                        case IIO_ANGL_VEL:
                                *val = 0;
                                *val2 = 1065; // 0.001065 rad/s/LSB (±2000dps)
                                return IIO_VAL_INT_PLUS_MICRO;
                        case IIO_TEMP:
                                *val = 326;
                                *val2 = 800000; // 326.8 LSB/°C
                                return IIO_VAL_INT_PLUS_MICRO;
                }
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
        buf[0] = reg & 0x7F; // clear bit 7 to write
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
        spi_set_drvdata(spi, dev); // store dev in spi

        spi->mode = SPI_MODE_0; // SPI mode 0
        spi_setup(spi);

        icm20608_write_reg(dev, ICM20_REG_PWR_MGMT_1, 0x80);
        mdelay(50);
        /* 唤醒: CLKSEL=1 自动选择最佳时钟(PLL) */
        icm20608_write_reg(dev, ICM20_REG_PWR_MGMT_1, 0x01);
        mdelay(100);  /* 等待 PLL 锁定 + 陀螺仪起振 */

        /* 配置陀螺仪量程: ±2000dps */
        icm20608_write_reg(dev, ICM20_REG_GYRO_CONFIG, 0x18);
        mdelay(10);

        /* 配置加速度计量程: ±2g */
        icm20608_write_reg(dev, ICM20_REG_ACCEL_CONFIG, 0x00);
        mdelay(10);

        ret = icm20608_read_reg(dev, ICM20_REG_WHO_AM_I, &chip_id, 1);
        if (ret < 0) {
                dev_err(&spi->dev, "read WHO_AM_I faild!\r\n");
                return ret;
        }
        if (chip_id != 0xAF) {
                dev_err(&spi->dev,
                        "chip id mismatch: expected 0xAF, got 0x%02X\r\n",
                        chip_id);
                return -ENODEV;
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
                        .name = "icm20608",
                        .of_match_table = icm20608_of_match,
                },
        .probe = icm20608_probe,
        .remove = icm20608_remove,
};

module_spi_driver(icm20608_driver);

MODULE_SOFTDEP("pre: industrialio");
MODULE_AUTHOR("TATAROSE");
MODULE_LICENSE("GPL");
