/*
 * iio_icm_20608.c -- ICM-20608 六轴惯性传感器 IIO 驱动
 *
 * 硬件说明:
 *   ICM-20608 是 InvenSense 出品的 6 轴惯性测量单元 (IMU), 内部集成:
 *     1. 3 轴加速度计 (Accelerometer): 16bit
 *     2. 3 轴陀螺仪   (Gyroscope):     16bit
 *     3. 温度传感器  (Temperature)
 *   本驱动通过 SPI 总线访问芯片 (SPI 模式 0, CPOL=0 / CPHA=0).
 *
 * 驱动框架:
 *   基于 Linux IIO (Industrial I/O) 子系统实现, 将加速度计 X/Y/Z、
 *   陀螺仪 X/Y/Z、温度共 7 个物理量注册为 7 个 IIO 通道, 用户空间
 *   可通过 sysfs (/sys/bus/iio/devices/iio:deviceX/) 读取原始值
 *   与比例系数.
 *
 * SPI 寄存器访问协议:
 *   每次访问先发送 1 字节 "寄存器地址 + R/W 位":
 *     - bit7 = 1: 读寄存器, 之后芯片按地址自增连续输出数据
 *     - bit7 = 0: 写寄存器, 紧随其后发送 1 字节待写入数据
 *
 * 作者: TATAROSE
 * 许可: GPL
 */

#include <linux/types.h>     /* u8/s16 等基本数据类型 */
#include <linux/device.h>    /* 设备模型: dev_err/dev_info 等 */
#include <linux/gfp.h>       /* GFP_KERNEL 内存分配标志 */
#include <linux/iio/types.h> /* IIO 通道类型定义 */
#include <linux/iio/iio.h>   /* IIO 子系统核心接口 */
#include <linux/iio/sysfs.h> /* IIO sysfs 辅助接口 */
#include <linux/module.h>    /* 内核模块相关宏 */
#include <linux/spi/spi.h>   /* SPI 总线框架: spi_device/spi_driver */

#include <linux/delay.h> /* mdelay() 毫秒级忙等延时 */

/* ===== ICM-20608 寄存器地址定义 (见数据手册寄存器映射表) ===== */
#define ICM20_REG_WHO_AM_I 0x75   /* 芯片 ID 寄存器: ICM-20608 固定为 0xAF */
#define ICM20_REG_PWR_MGMT_1 0x6B /* 电源管理寄存器 1: 复位/睡眠/时钟源选择 */

/* 加速度计三轴输出寄存器 (每轴 2 字节, 高字节在前) */
#define ICM20_REG_ACCEL_XOUT_H 0x3B /* 加速度计 X 轴输出高字节 */
#define ICM20_REG_ACCEL_XOUT_L 0x3C /* 加速度计 X 轴输出低字节 */
#define ICM20_REG_ACCEL_YOUT_H 0x3D /* 加速度计 Y 轴输出高字节 */
#define ICM20_REG_ACCEL_YOUT_L 0x3E /* 加速度计 Y 轴输出低字节 */
#define ICM20_REG_ACCEL_ZOUT_H 0x3F /* 加速度计 Z 轴输出高字节 */
#define ICM20_REG_ACCEL_ZOUT_L 0x40 /* 加速度计 Z 轴输出低字节 */

/* 陀螺仪三轴输出寄存器 (每轴 2 字节, 高字节在前) */
#define ICM20_REG_GYRO_XOUT_H 0x43 /* 陀螺仪 X 轴输出高字节 */
#define ICM20_REG_GYRO_XOUT_L 0x44 /* 陀螺仪 X 轴输出低字节 */
#define ICM20_REG_GYRO_YOUT_H 0x45 /* 陀螺仪 Y 轴输出高字节 */
#define ICM20_REG_GYRO_YOUT_L 0x46 /* 陀螺仪 Y 轴输出低字节 */
#define ICM20_REG_GYRO_ZOUT_H 0x47 /* 陀螺仪 Z 轴输出高字节 */
#define ICM20_REG_GYRO_ZOUT_L 0x48 /* 陀螺仪 Z 轴输出低字节 */


/* 温度输出寄存器 (2 字节, 高字节在前) */
#define ICM20_REG_TEMP_OUT_H 0x41 /* 温度输出高字节 */
#define ICM20_REG_TEMP_OUT_L 0x42 /* 温度输出低字节 */

/* 量程/滤波配置寄存器 */
#define ICM20_REG_ACCEL_CONFIG2 0x1D /* 加速度计配置 2(低通滤波, 本驱动未使用) */
#define ICM20_REG_GYRO_CONFIG  0x1B  /* 陀螺仪配置: bit[4:3] FS_SEL 选择量程 */
#define ICM20_REG_ACCEL_CONFIG 0x1C  /* 加速度计配置: bit[4:3] FS_SEL 选择量程 */

/*
 * IIO 通道定义表 (共 7 个通道)
 * 加速度计与陀螺仪按轴拆分为独立通道:
 *   - IIO_ACCEL    + IIO_MOD_X/Y/Z: 加速度计三轴
 *   - IIO_ANGL_VEL + IIO_MOD_X/Y/Z: 陀螺仪三轴 (角速度)
 *   - IIO_TEMP:                     温度
 * 属性声明:
 *   - info_mask_separate = RAW:      每个通道单独支持原始值
 *   - info_mask_shared_by_type = SCALE: 同类型通道共享一个比例系数
 *     (三轴量程相同, 故 scale 整个类型只读一次)
 * address 保存该通道输出寄存器高字节地址, 读取时从该地址连续读 2 字节.
 */
static const struct iio_chan_spec icm20608_channels[] = {
        {
                .type = IIO_ACCEL, /* 加速度计 */
                .modified = 1,     /* 使用 channel2 区分轴向 */
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
                .type = IIO_ANGL_VEL, /* 角速度(陀螺仪) */
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
                .type = IIO_TEMP, /* 温度通道 */
                .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
                .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
                .address = ICM20_REG_TEMP_OUT_H,
        },

};

/* 驱动私有数据结构体: 保存在 iio_dev 之后, 运行时由 iio_priv() 获取 */
struct icm20608_dev {
        struct spi_device *spi; /* SPI 设备, 用于寄存器读写 */
};

/*
 * 读取寄存器
 * SPI 读时序: 先发送 1 字节 "寄存器地址 | 0x80"(bit7 置 1 表示读),
 * 芯片随后在同一片选周期内按地址自增连续输出 len 字节.
 * spi_write_then_read() 一次完成 "先写地址、再读数据" 两段操作,
 * 且中途不释放片选, 恰好满足芯片的读寄存器时序要求.
 */
static int icm20608_read_reg(struct icm20608_dev *dev, u8 reg, u8 *buf,
                             u8 len) {
        u8 tx_data = reg | 0x80; /* 最高位置 1: 读操作 */
        return spi_write_then_read(dev->spi, &tx_data, 1, buf, len);
}

/*
 * IIO read_raw 回调
 * 用户空间读取通道属性时由 IIO 框架调用, mask 表示请求的属性:
 *   - IIO_CHAN_INFO_RAW:   读取 16bit 原始采样值(大端拼接为有符号数)
 *   - IIO_CHAN_INFO_SCALE: 按通道类型返回比例系数
 *       . 加速度计: 0.000598 m/s²/LSB (±2g 量程)
 *       . 陀螺仪:   0.001065 rad/s/LSB (±2000dps 量程)
 *       . 温度:     326.8 LSB/°C (由温度原始值换算摄氏度用)
 */
static int icm20608_read_raw(struct iio_dev *indio_dev,
                             struct iio_chan_spec const *chan, int *val,
                             int *val2, long mask) {
        struct icm20608_dev *dev = iio_priv(indio_dev);
        int ret;
        u8 buf[2]; /* 依次存放: 数据高字节、数据低字节 */

        switch (mask) {         /* 根据请求的属性类型分发 */
        case IIO_CHAN_INFO_RAW: /* 读取原始值 */
                /* 从通道对应的寄存器地址连续读 2 字节 */
                ret = icm20608_read_reg(dev, chan->address, buf, 2);
                if (ret < 0) {      /* 读取失败 */
                        return ret; /* 直接返回错误码 */
                }
                /* 高字节在前(大端)拼成 16bit, 按有符号数(补码)解释 */
                *val = (s16)((buf[0] << 8) |
                             buf[1]);
                return IIO_VAL_INT; /* 值格式: 单个整数 */
        case IIO_CHAN_INFO_SCALE:   /* 返回比例系数 */
                switch (chan->type) { /* 不同通道类型系数不同 */
                        case IIO_ACCEL:
                                /* ±2g: 16384 LSB/g
                                 * 1 LSB = 9.80665/16384 ≈ 0.000598 m/s² */
                                *val = 0;
                                *val2 = 598; /* 微小数部分 598e-6 */
                                return IIO_VAL_INT_PLUS_MICRO;
                        case IIO_ANGL_VEL:
                                /* ±2000dps: 16.4 LSB/(°/s)
                                 * 1 LSB ≈ 0.001065 rad/s */
                                *val = 0;
                                *val2 = 1065; /* 微小数部分 1065e-6 */
                                return IIO_VAL_INT_PLUS_MICRO;
                        case IIO_TEMP:
                                /* 温度灵敏度 326.8 LSB/°C:
                                 * 实际温度 = 原始值 / 326.8 */
                                *val = 326;
                                *val2 = 800000; /* 微小数部分 0.8 */
                                return IIO_VAL_INT_PLUS_MICRO;
                }
                /* 内层 switch 未匹配到类型时, 落入外层 default 统一报错 */
        default:
                return -EINVAL; /* 未支持的属性或类型 */
        }
}

/* IIO 设备操作接口, 将框架回调绑定到本驱动的实现 */
static const struct iio_info icm20608_info = {
        .read_raw = icm20608_read_raw, /* 通道属性读取入口 */
        .driver_module = THIS_MODULE,  /* 模块引用管理 */
};

/*
 * 写寄存器
 * SPI 写时序: 先发送 1 字节 "寄存器地址 & 0x7F"(bit7 清 0 表示写),
 * 紧接着发送 1 字节待写入数据, 两字节在同一片选周期内完成.
 */
static int icm20608_write_reg(struct icm20608_dev *dev, u8 reg, u8 data) {
        u8 buf[2];
        buf[0] = reg & 0x7F; /* 最高位清 0: 写操作 */
        buf[1] = data;       /* 待写入的数据 */
        return spi_write(dev->spi, buf, 2); /* 一次发送"地址+数据"共 2 字节 */
}

/* 设备树匹配表: 与 dts 中 compatible = "invn,icm20608" 的节点匹配 */
static const struct of_device_id icm20608_of_match[] = {
        {.compatible = "invn,icm20608"}, {}};

/* 导出匹配表到模块信息, 用于模块自动加载 */
MODULE_DEVICE_TABLE(of, icm20608_of_match);

/*
 * SPI 驱动 probe 函数
 * 设备与驱动匹配成功后由总线框架调用, 流程:
 *   分配内存 -> 配置 SPI -> 复位并唤醒芯片 -> 配置量程 ->
 *   校验芯片 ID -> 注册 IIO 设备.
 */
static int icm20608_probe(struct spi_device *spi) {
        struct icm20608_dev *dev;
        u8 chip_id; /* 保存读出的芯片 ID */
        int ret;
        struct iio_dev *indio_dev;

        /* 旧实现(直接 kzalloc), 已改用下方的 devm_iio_device_alloc:
         * devm 接口在设备移除时会自动释放内存, 避免手动 free. */
        // dev=devm_kzalloc(&spi->dev, sizeof(*dev),GFP_KERNEL);// allocate
        // memory
        // if(!dev){
        //         return -ENOMEM;
        // }

        /* 分配 iio_dev 及紧跟其后的私有数据结构体 */
        indio_dev =
                devm_iio_device_alloc(&spi->dev, sizeof(struct icm20608_dev));
        if (!indio_dev) {
                return -ENOMEM;
        }

        /* 初始化私有数据 */
        dev = iio_priv(indio_dev);
        dev->spi = spi;
        spi_set_drvdata(spi, dev); /* 保存句柄, 供 remove 等回调使用 */

        /* 配置 SPI 模式 0 (CPOL=0, CPHA=0) 并应用 */
        spi->mode = SPI_MODE_0;
        spi_setup(spi);

        /* 1. 复位芯片: PWR_MGMT_1 bit7 = 1 触发内部复位 */
        icm20608_write_reg(dev, ICM20_REG_PWR_MGMT_1, 0x80);
        mdelay(50); /* 等待复位完成 */

        /* 2. 唤醒芯片: CLKSEL=1, 自动选择最佳时钟源(PLL) */
        icm20608_write_reg(dev, ICM20_REG_PWR_MGMT_1, 0x01);
        mdelay(100); /* 等待 PLL 锁定 + 陀螺仪起振 */

        /* 3. 配置陀螺仪量程: 0x18 -> FS_SEL=11, ±2000dps */
        icm20608_write_reg(dev, ICM20_REG_GYRO_CONFIG, 0x18);
        mdelay(10);

        /* 4. 配置加速度计量程: 0x00 -> FS_SEL=00, ±2g */
        icm20608_write_reg(dev, ICM20_REG_ACCEL_CONFIG, 0x00);
        mdelay(10);

        /* 5. 读取芯片 ID 并校验, 确认总线上正是 ICM-20608 */
        ret = icm20608_read_reg(dev, ICM20_REG_WHO_AM_I, &chip_id, 1);
        if (ret < 0) {
                dev_err(&spi->dev, "read WHO_AM_I faild!\r\n");
                return ret;
        }
        if (chip_id != 0xAF) { /* ICM-20608 的 ID 固定为 0xAF */
                dev_err(&spi->dev,
                        "chip id mismatch: expected 0xAF, got 0x%02X\r\n",
                        chip_id);
                return -ENODEV;
        }

        /* 填充 IIO 设备描述信息 */
        indio_dev->dev.parent = &spi->dev;   /* 父设备 */
        indio_dev->name = "icm20608";        /* 设备名 */
        indio_dev->info = &icm20608_info;    /* 操作接口 */
        indio_dev->modes = INDIO_DIRECT_MODE; /* 仅支持 sysfs 直接读取 */
        indio_dev->channels = icm20608_channels; /* 通道表 */
        indio_dev->num_channels = ARRAY_SIZE(icm20608_channels); /* 通道个数 */

        /* 注册 IIO 设备, 自动创建 sysfs 节点 */
        return devm_iio_device_register(&spi->dev, indio_dev);
}

/*
 * SPI 驱动 remove 函数
 * 设备卸载时调用. 此处仅打印日志; 生产代码通常应让芯片进入睡眠
 * 以降低功耗, 内存由 devm 框架自动回收.
 */
static int icm20608_remove(struct spi_device *spi) {
        dev_info(&spi->dev, "icm20608_remove\n");
        return 0;
}

/* SPI 驱动结构体: 绑定 probe/remove 及设备树匹配表 */
static struct spi_driver icm20608_driver = {
        .driver =
                {
                        .name = "icm20608", /* 驱动名称 */
                        .of_match_table = icm20608_of_match, /* 设备树匹配表 */
                },
        .probe = icm20608_probe,   /* 匹配成功后调用 */
        .remove = icm20608_remove, /* 移除时调用 */
};

/* 展开为模块 init/exit 函数并注册 SPI 驱动 */
module_spi_driver(icm20608_driver);

/* 模块软依赖: 加载本模块前先加载 industrialio (IIO 核心) */
MODULE_SOFTDEP("pre: industrialio");
MODULE_AUTHOR("TATAROSE");
MODULE_LICENSE("GPL");
