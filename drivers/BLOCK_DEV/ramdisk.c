
#include "linux/bio.h"
#include "linux/blk_types.h"
#include "linux/blkdev.h"
#include "linux/cdrom.h"
#include "linux/export.h"
#include "linux/fs.h"
#include "linux/genhd.h"
#include "linux/gfp.h"
#include "linux/input.h"
#include "linux/interrupt.h"
#include "linux/mm.h"
#include "linux/mod_devicetable.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/spinlock.h"
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/hdreg.h>
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

#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#define RAMDISK_SIZE (2 * 1024 * 1024) // 内存盘容量：2MB
#define RAMDISK_NAME "ramdisk"         // 设备名
#define RAMDISK_MINOR 3                // 分配的次设备号数量（0~2）

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TATAROSE");
// 设备打开回调：这里只打印调试信息
static int ramdisk_open(struct block_device *bd, fmode_t mode) {
        printk("ramdisk open\r\n");
        return 0;
}
// 设备关闭回调：同样仅打印调试信息
static void ramdisk_release(struct gendisk *disk, fmode_t mode) {
        printk("ramdisk release\r\n");
}
// 获取几何信息的回调，这里未真正填充，仅打印
static int ramdisk_getgeo(struct block_device *bd, struct hd_geometry *geo) {
        printk("ramdisk getgeo\r\n");
        geo->heads = 2;
        geo->cylinders = 32;
        geo->sectors = RAMDISK_SIZE / (2 * 32 * 512);

        return 0;
}

static const struct block_device_operations ramdisk_fops = {
        .owner = THIS_MODULE,
        .open = ramdisk_open,
        .release = ramdisk_release,
        .getgeo = ramdisk_getgeo,
};

struct ramdisk_dev {
        int major;                   // 动态申请的主设备号
        unsigned char *ramdisk_buff; // 实际存储数据的内存缓冲区
        struct gendisk *gendisk;     // 磁盘抽象结构体
        struct request_queue *queue; // 请求队列
        spinlock_t lock;             // 保护队列的自旋锁
};
struct ramdisk_dev ramdiskdev;

// 处理一个 request 的数据传输：根据读/写方向做 memcpy
/* static int ramdick_transfer(struct request *req) {
        // 数据传输三要素：源，目的，长度。
        // 涉及到内存地址，块设备地址，长度
        unsigned long start = blk_rq_pos(req)
                              << 9; // 块设备扇区地址，左移九位，得到
        unsigned long len = blk_rq_cur_bytes(req); // 长度
        int err = 0;

        //获取bio里面的缓冲区
        //
并不是所有的块设备都是用memcpy，还有SD卡，EMMC设备，现在使用内存模拟的
        // 读:从磁盘读数据到内存缓冲区
        // 写:从内存缓冲区读数据到磁盘
        void *buffer = bio_data(req->bio);
        if (rq_data_dir(req) == READ) {
                memcpy(buffer, ramdiskdev.ramdisk_buff + start, len); // 读数据
        } else {
                memcpy(ramdiskdev.ramdisk_buff + start, buffer, len); // 写数据
        }
        return err;
}
 */
// 制造请求函数
static void ramdisk_make_request(struct request_queue *q, struct bio *bio) {
        int offset;
        struct bio_vec bvec;
        struct bvec_iter iter;
        int len;

        offset = bio->bi_iter.bi_sector << 9; // 获取扇区地址,改为字节地址。

        bio_for_each_segment(bvec, bio, iter) {
                char *ptr = page_address(bvec.bv_page) +
                            bvec.bv_offset; // 获取物理地址
                len = bvec.bv_len;
                if (bio_data_dir(bio) == READ) {
                        memcpy(ptr, ramdiskdev.ramdisk_buff + offset,
                               len); // 读数据
                } else {
                        memcpy(ramdiskdev.ramdisk_buff + offset, ptr,
                               len); // 写数据
                }
                offset += len; // 移动偏移
        }
        set_bit(BIO_UPTODATE, &bio->bi_flags);
        bio_endio(bio, 0);
}

// 模块加载入口：申请内存、注册设备号、创建 gendisk 和请求队列
static int __init ramdisk_init(void) {
        int ret = 0;
        ramdiskdev.ramdisk_buff = kzalloc(RAMDISK_SIZE, GFP_KERNEL);
        if (ramdiskdev.ramdisk_buff == NULL) {
                ret = -EINVAL;
        }
        ramdiskdev.major = register_blkdev(0, RAMDISK_NAME);
        if (ramdiskdev.major < 0) {
                kfree(ramdiskdev.ramdisk_buff);
                ret = -EINVAL;
        }
        printk("ramdisk major=%d\r\n", ramdiskdev.major);
        ramdiskdev.gendisk = alloc_disk(RAMDISK_MINOR);
        if (!ramdiskdev.gendisk) {
                unregister_blkdev(ramdiskdev.major, RAMDISK_NAME);
                ret = -EINVAL;
        }
        spin_lock_init(&ramdiskdev.lock);

        ramdiskdev.queue = blk_alloc_queue(GFP_KERNEL);
        if (!ramdiskdev.queue) { // 申请请求队列失败
                ret = -EINVAL;
                del_gendisk(ramdiskdev.gendisk);
        }
        blk_queue_make_request(ramdiskdev.queue,
                               ramdisk_make_request); // 添加处理函数

        ramdiskdev.gendisk->major = ramdiskdev.major;
        ramdiskdev.gendisk->first_minor = 0;
        ramdiskdev.gendisk->fops = &ramdisk_fops;
        ramdiskdev.gendisk->queue = ramdiskdev.queue;
        ramdiskdev.gendisk->private_data = &ramdiskdev;
        sprintf(ramdiskdev.gendisk->disk_name, RAMDISK_NAME);
        set_capacity(ramdiskdev.gendisk, RAMDISK_SIZE / 512);

        add_disk(ramdiskdev.gendisk);

        printk("ramdisk init\r\n");
        return ret;
}

// 模块卸载入口：释放 gendisk/队列/设备号/内存
static void __exit ramdisk_exit(void) {
        del_gendisk(ramdiskdev.gendisk);

        blk_cleanup_queue(ramdiskdev.queue);
        put_disk(ramdiskdev.gendisk);
        unregister_blkdev(ramdiskdev.major, RAMDISK_NAME);
        kfree(ramdiskdev.ramdisk_buff);
        printk("ramdisk exit\r\n");
}

module_init(ramdisk_init);
module_exit(ramdisk_exit);
