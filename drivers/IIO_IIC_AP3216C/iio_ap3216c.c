/*
 * iio_ap3216c.c -- AP3216C 环境光/接近传感器 IIO 驱动
 *
 * 硬件说明:
 *   AP3216C 是 LiteON 出品的 I2C 接口三合一光传感器, 内部集成:
 *     1. ALS (Ambient Light Sensor): 环境光强度检测, 16bit
 *     2. IR  (Infrared):             红外光检测,       10bit
 *     3. PS  (Proximity Sensor):     接近检测,         10bit
 *   I2C 从机地址通常为 0x1E (7bit).
 *
 * 驱动框架:
 *   本驱动基于 Linux IIO (Industrial I/O) 子系统实现, 将 ALS/IR/PS
 *   三个物理量分别注册为 3 个 IIO 通道, 用户空间可通过 sysfs 的
 *   /sys/bus/iio/devices/iio:deviceX/ 目录读取原始值与比例系数.
 *
 * I2C 寄存器访问:
 *   - 写寄存器: [寄存器地址, 写入数据] 组成一次 I2C 写传输
 *   - 读寄存器: 先发寄存器地址(不产生 STOP), 随后连续读出若干字节
 *
 * 作者: TATAROSE
 * 许可: GPL
 */

#include <linux/delay.h>   /* msleep() 等毫秒级延时函数 */
#include <linux/errno.h>   /* 错误码定义: -EIO -ENOMEM 等 */
#include <linux/i2c.h>     /* I2C 总线框架: i2c_client/i2c_msg 等 */
#include <linux/iio/iio.h> /* IIO 子系统核心: iio_dev/iio_chan_spec 等 */
#include <linux/kernel.h>  /* BIT()/GENMASK() 位操作宏 */
#include <linux/module.h>  /* 内核模块相关宏 */
#include <linux/mutex.h>   /* 互斥锁, 用于串行化 I2C 访问 */
#include <linux/of.h>      /* 设备树匹配 (of_match_table) */
#include <linux/types.h>   /* u8/u16 等基本数据类型 */

/* ===== AP3216C 寄存器地址定义 (见数据手册寄存器映射表) ===== */
#define AP3216C_REG_SYSTEM_CONFIG 0x00 /* 系统配置寄存器: 工作模式/软件复位 */
#define AP3216C_REG_IR_DATA_LOW 0x0a   /* 红外(IR)数据低字节 */
#define AP3216C_REG_ALS_DATA_LOW 0x0c  /* 环境光(ALS)数据低字节 */
#define AP3216C_REG_PS_DATA_LOW 0x0e   /* 接近(PS)数据低字节 */

/* 系统配置寄存器 bit[2:0] 可写入的工作模式 */
#define AP3216C_MODE_POWER_DOWN 0x00 /* 掉电模式: 关闭全部检测, 最低功耗 */
#define AP3216C_MODE_ALS_PS_IR 0x03  /* 同时使能 ALS + PS + IR 三路检测 */
#define AP3216C_MODE_SW_RESET 0x04   /* 软件复位: 复位全部寄存器为默认值 */

/* ===== 数据寄存器位域定义 =====
 * 各数据寄存器的高字节紧随低字节之后(地址 +1), 读取时连续读 2 字节.
 */

/* IR 数据: 共 10bit = 高字节 8bit + 低字节 bit[1:0];
 * 低字节 bit7 为 IR 数据溢出标志 */
#define AP3216C_IR_OVERFLOW BIT(7)             /* IR 溢出标志位 */
#define AP3216C_IR_DATA_LOW_MASK GENMASK(1, 0) /* IR 低字节有效数据位 */

/* PS 数据: 共 10bit = 高字节 bit[5:0] + 低字节 bit[3:0];
 * 高/低字节的 bit6 分别为 PS/IR 溢出标志 */
#define AP3216C_PS_IR_OVERFLOW BIT(6)           /* PS/IR 溢出标志位 */
#define AP3216C_PS_DATA_LOW_MASK GENMASK(3, 0)  /* PS 低字节有效数据位 */
#define AP3216C_PS_DATA_HIGH_MASK GENMASK(5, 0) /* PS 高字节有效数据位 */

/* ALS 上电默认量程: 0 ~ 20661 lux, 分辨率 0.35 lux/count.
 * IIO 中 scale 以 "整数 + 微小数" 表示: 0.35 = val(0) + val2(350000)*1e-6 */
#define AP3216C_ALS_SCALE_MICRO 350000

/*
 * 驱动私有数据结构体
 * 通过 devm_iio_device_alloc() 分配在 iio_dev 内存之后,
 * 运行时用 iio_priv() 由 iio_dev 反查其首地址.
 */
struct ap3216c_iio_dev {
        struct i2c_client *client; /* I2C 客户端, 持有适配器/从机地址等信息 */
        struct mutex lock;         /* 互斥锁: 防止并发访问打乱 I2C 读时序 */
};

/*
 * IIO 通道定义表
 * 每个 iio_chan_spec 对应一个用户可见的数据通道:
 *   - type:        通道类型 (环境光/光强度/接近)
 *   - address:     该通道数据寄存器地址(驱动内部直接使用)
 *   - info_mask_*: 声明该通道支持哪些属性(原始值 raw / 比例系数 scale)
 */
static const struct iio_chan_spec ap3216c_channels[] = {
        {
                .type = IIO_LIGHT, /* 环境光通道, 单位 lux */
                /* 该通道独立支持 raw 和 scale 两个属性 */
                .info_mask_separate =
                        BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
                .address = AP3216C_REG_ALS_DATA_LOW, /* 对应数据寄存器 */
        },
        {
                .type = IIO_INTENSITY, /* 光强度通道 */
                .modified = 1,         /* 需要 channel2 进一步修饰通道含义 */
                .channel2 = IIO_MOD_LIGHT_IR, /* 修饰为"红外光强度" */
                .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
                .address = AP3216C_REG_IR_DATA_LOW,
        },
        {
                .type = IIO_PROXIMITY, /* 接近检测通道 */
                .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
                .address = AP3216C_REG_PS_DATA_LOW,
        },
};

/*
 * 连续读取寄存器 (支持多字节)
 * 实现方式: 在一条 I2C 事务中拼接两条消息:
 *   第 1 条: 写方向, 只发送 1 字节寄存器地址;
 *   第 2 条: 读方向, 接收 len 字节数据.
 * 由于两段消息在同一事务内且中间无 STOP, 芯片会按地址自增依次返回数据.
 */
static int ap3216c_read_regs(struct ap3216c_iio_dev *data, u8 reg, u8 *buf,
                             int len) {
        struct i2c_client *client = data->client;
        struct i2c_msg msgs[2] = {
                {
                        .addr = client->addr,   /* I2C 从机地址 */
                        .flags = client->flags, /* 无 I2C_M_RD, 即写方向 */
                        .buf = &reg,            /* 发送内容: 寄存器地址 */
                        .len = 1,
                },
                {
                        .addr = client->addr,
                        .flags = client->flags | I2C_M_RD, /* 读方向 */
                        .buf = buf,     /* 数据接收缓冲区 */
                        .len = len,     /* 期望读取的字节数 */
                },
        };
        int ret;

        /* 执行 I2C 事务, 返回值为成功传输的消息条数 */
        ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
        if (ret < 0)
                return ret;          /* 传输出错, 返回负错误码 */
        if (ret != ARRAY_SIZE(msgs)) /* 消息未全部完成, 视为 I/O 错误 */
                return -EIO;

        return 0;
}

/*
 * 写单个寄存器
 * 一次 I2C 写事务发送 2 字节: [寄存器地址, 写入值].
 */
static int ap3216c_write_reg(struct ap3216c_iio_dev *data, u8 reg, u8 value) {
        struct i2c_client *client = data->client;
        u8 buf[] = {reg, value}; /* 发送缓冲: 寄存器地址 + 数据 */
        struct i2c_msg msg = {
                .addr = client->addr,
                .flags = client->flags, /* 写方向 */
                .buf = buf,
                .len = sizeof(buf),     /* 共 2 字节 */
        };
        int ret;

        ret = i2c_transfer(client->adapter, &msg, 1);
        if (ret < 0)
                return ret;
        if (ret != 1) /* 唯一一条消息未完成 */
                return -EIO;

        return 0;
}

/*
 * 读取一个通道的原始数据
 * 按通道对应的寄存器地址区分数据格式并拼接原始值:
 *   - ALS: 16bit, (高字节 << 8) | 低字节
 *   - IR : 10bit, (高字节 << 2) | (低字节 & 0x3)
 *   - PS : 10bit, (高字节 & 0x3F) << 4 | (低字节 & 0xF)
 * 溢出(光线过强等)时返回 -EOVERFLOW.
 * 整个读取过程持互斥锁, 避免并发访问破坏"地址+数据"的读时序.
 */
static int ap3216c_read_channel(struct ap3216c_iio_dev *data,
                                const struct iio_chan_spec *chan, int *val) {
        u8 buf[2]; /* 依次存放: 数据低字节、数据高字节 */
        int ret;

        mutex_lock(&data->lock); /* 加锁: 保护 I2C 读时序 */
        ret = ap3216c_read_regs(data, chan->address, buf, sizeof(buf));
        mutex_unlock(&data->lock); /* 解锁 */
        if (ret)
                return ret;

        switch (chan->address) {
        case AP3216C_REG_ALS_DATA_LOW:
                /* ALS: 16bit, 高字节 << 8 与低字节拼接 */
                *val = ((u16)buf[1] << 8) | buf[0];
                break;
        case AP3216C_REG_IR_DATA_LOW:
                /* 检查低字节 bit7 的 IR 溢出标志 */
                if (buf[0] & AP3216C_IR_OVERFLOW)
                        return -EOVERFLOW;
                /* IR: 10bit = 高字节(8bit) << 2 | 低字节 bit[1:0] */
                *val = ((u16)buf[1] << 2) | (buf[0] & AP3216C_IR_DATA_LOW_MASK);
                break;
        case AP3216C_REG_PS_DATA_LOW:
                /* 高/低字节 bit6 分别是 PS/IR 溢出标志 */
                if ((buf[0] | buf[1]) & AP3216C_PS_IR_OVERFLOW)
                        return -EOVERFLOW;
                /* PS: 10bit = 高字节 bit[5:0] << 4 | 低字节 bit[3:0] */
                *val = ((u16)(buf[1] & AP3216C_PS_DATA_HIGH_MASK) << 4) |
                       (buf[0] & AP3216C_PS_DATA_LOW_MASK);
                break;
        default:
                return -EINVAL; /* 未知寄存器, 正常情况下不应发生 */
        }

        return IIO_VAL_INT; /* 返回值格式: 单个整数 */
}

/*
 * IIO read_raw 回调
 * 用户空间读取通道属性(如 cat in_illuminance_raw)时, IIO 框架调用此函数,
 * mask 表示请求的属性类型:
 *   - IIO_CHAN_INFO_RAW:   返回原始采样值 (val)
 *   - IIO_CHAN_INFO_SCALE: 返回比例系数 (val + val2 组合), 仅 ALS 支持
 */
static int ap3216c_read_raw(struct iio_dev *indio_dev,
                            const struct iio_chan_spec *chan, int *val,
                            int *val2, long mask) {
        struct ap3216c_iio_dev *data = iio_priv(indio_dev);

        switch (mask) {
        case IIO_CHAN_INFO_RAW:
                /* 读取原始数据 */
                return ap3216c_read_channel(data, chan, val);
        case IIO_CHAN_INFO_SCALE:
                /* 只有环境光通道声明了 scale 属性 */
                if (chan->address != AP3216C_REG_ALS_DATA_LOW)
                        return -EINVAL;

                /* 0.35 lux/count: val=0 整数部分, val2=350000 微小数部分 */
                *val = 0;
                *val2 = AP3216C_ALS_SCALE_MICRO;
                return IIO_VAL_INT_PLUS_MICRO; /* 值 = val + val2 * 10^-6 */
        default:
                return -EINVAL; /* 未支持的属性 */
        }
}

/* IIO 设备操作接口, 将框架回调绑定到本驱动的实现 */
static const struct iio_info ap3216c_info = {
        .read_raw = ap3216c_read_raw, /* 通道属性读取入口 */
        .driver_module = THIS_MODULE, /* 防止驱动卸载时模块引用计数出错 */
};

/*
 * 芯片初始化流程:
 *   1. 写软件复位命令, 复位全部寄存器到默认值;
 *   2. 延时 15ms (数据手册要求: 复位后 10ms 内芯片不得接收任何命令);
 *   3. 配置工作模式为 ALS + PS + IR 同时使能;
 *   4. 回读系统配置寄存器, 校验模式位是否正确写入, 确认芯片在线.
 */
static int ap3216c_chip_init(struct ap3216c_iio_dev *data) {
        u8 mode; /* 保存回读的模式字节 */
        int ret;

        /* 1. 软件复位 */
        ret = ap3216c_write_reg(data, AP3216C_REG_SYSTEM_CONFIG,
                                AP3216C_MODE_SW_RESET);
        if (ret)
                return ret;

        /* 复位后必须等待: 手册要求 10ms, 这里留 15ms 余量 */
        msleep(15);

        /* 2. 使能 ALS + PS + IR 三种检测 */
        ret = ap3216c_write_reg(data, AP3216C_REG_SYSTEM_CONFIG,
                                AP3216C_MODE_ALS_PS_IR);
        if (ret)
                return ret;

        /* 3. 回读校验 */
        ret = ap3216c_read_regs(data, AP3216C_REG_SYSTEM_CONFIG, &mode, 1);
        if (ret)
                return ret;
        /* 模式字段位于 bit[2:0], 不一致说明芯片异常或未应答 */
        if ((mode & GENMASK(2, 0)) != AP3216C_MODE_ALS_PS_IR)
                return -ENODEV;

        return 0;
}

/*
 * I2C 驱动 probe 函数
 * 设备与驱动匹配成功(设备树 compatible 或 id_table)后由总线框架调用,
 * 完成: 适配器功能检查 -> 内存分配 -> 私有数据初始化 -> 芯片初始化 ->
 *       IIO 设备注册.
 */
static int ap3216c_probe(struct i2c_client *client,
                         const struct i2c_device_id *id) {
        struct ap3216c_iio_dev *data;
        struct iio_dev *indio_dev;
        int ret;

        /* 检查 I2C 控制器是否支持本驱动需要的传输方式(I2C_FUNC_I2C) */
        if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
                return -EOPNOTSUPP;

        /* 分配 iio_dev 及其私有数据(设备移除时由 devm 自动释放, 无需手动 free) */
        indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
        if (!indio_dev)
                return -ENOMEM;

        /* 初始化私有数据 */
        data = iio_priv(indio_dev);
        data->client = client;
        mutex_init(&data->lock);              /* 初始化互斥锁 */
        i2c_set_clientdata(client, indio_dev); /* 保存句柄供 remove 使用 */

        /* 初始化芯片(复位 + 配置工作模式 + 校验) */
        ret = ap3216c_chip_init(data);
        if (ret) {
                /* 初始化失败时尽量让芯片进入掉电模式 */
                ap3216c_write_reg(data, AP3216C_REG_SYSTEM_CONFIG,
                                  AP3216C_MODE_POWER_DOWN);
                dev_err(&client->dev, "failed to initialize sensor: %d\n", ret);
                return ret;
        }

        /* 填充 IIO 设备描述信息 */
        indio_dev->dev.parent = &client->dev; /* 父设备: 决定 sysfs 层级 */
        indio_dev->name = "ap3216c"; /* 设备名: sysfs 中的 iio:deviceX 名称 */
        indio_dev->info = &ap3216c_info; /* 操作接口 */
        indio_dev->modes = INDIO_DIRECT_MODE; /* 仅支持 sysfs 直接读取模式 */
        indio_dev->channels = ap3216c_channels; /* 通道表 */
        indio_dev->num_channels = ARRAY_SIZE(ap3216c_channels); /* 通道个数 */

        /* 向 IIO 子系统注册设备(自动创建 sysfs 节点) */
        ret = devm_iio_device_register(&client->dev, indio_dev);
        if (ret) {
                /* 注册失败同样掉电, 避免芯片一直耗电 */
                ap3216c_write_reg(data, AP3216C_REG_SYSTEM_CONFIG,
                                  AP3216C_MODE_POWER_DOWN);
                return ret;
        }

        dev_info(&client->dev,
                 "AP3216C ambient light/proximity sensor ready\n");
        return 0;
}

/*
 * I2C 驱动 remove 函数
 * 设备卸载时调用, 将芯片置为掉电模式以降低功耗.
 * 注意: iio_dev 及私有数据由 devm 框架自动释放, 无需手动处理.
 */
static int ap3216c_remove(struct i2c_client *client) {
        struct iio_dev *indio_dev = i2c_get_clientdata(client);
        struct ap3216c_iio_dev *data = iio_priv(indio_dev);
        int ret;

        /* 卸载前让芯片掉电 */
        ret = ap3216c_write_reg(data, AP3216C_REG_SYSTEM_CONFIG,
                                AP3216C_MODE_POWER_DOWN);
        if (ret)
                dev_warn(&client->dev, "failed to power down sensor: %d\n",
                         ret);

        return 0;
}

/* 设备树匹配表: 与 dts 中 compatible = "lsc,ap3216c" 的节点匹配 */
static const struct of_device_id ap3216c_of_match[] = {
        {.compatible = "lsc,ap3216c"}, {}};
MODULE_DEVICE_TABLE(of, ap3216c_of_match); /* 导出匹配表, 用于模块自动加载 */

/* 传统(非设备树)I2C 设备匹配表 */
static const struct i2c_device_id ap3216c_id[] = {{"ap3216c", 0}, {}};
MODULE_DEVICE_TABLE(i2c, ap3216c_id);

/* I2C 驱动结构体: 绑定 probe/remove 及匹配表 */
static struct i2c_driver ap3216c_driver = {
        .driver =
                {
                        .name = "iio_ap3216c", /* 驱动名称(模块名) */
                        .owner = THIS_MODULE,
                        .of_match_table = of_match_ptr(ap3216c_of_match),
                },
        .probe = ap3216c_probe,   /* 设备匹配成功后调用 */
        .remove = ap3216c_remove, /* 设备移除时调用 */
        .id_table = ap3216c_id,
};
/* 展开为模块 init/exit 函数并注册 I2C 驱动 */
module_i2c_driver(ap3216c_driver);

/* 模块软依赖: 加载本模块前先加载 industrialio (IIO 核心) */
MODULE_SOFTDEP("pre: industrialio");
MODULE_AUTHOR("TATAROSE");
MODULE_DESCRIPTION("AP3216C ambient light, infrared and proximity sensor");
MODULE_LICENSE("GPL");
