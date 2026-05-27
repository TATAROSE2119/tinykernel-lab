#include "fcntl.h"
#include "linux/input.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "sys/ioctl.h"
#include "sys/stat.h"
#include "sys/types.h"
#include "unistd.h"

#define CMD_LED_ON 1
#define CMD_LED_OFF 0
#define DEVICE_FILE_NAME_LED "/dev/led-dts-platform"
#define DEVICE_FILE_NAME_INPUT_KEY "/dev/input/event1"
#define DEVICE_FILE_NAME_AP3216C "/dev/ap3216c"
#define DEVICE_FILE_NAME_ICM20608 "/dev/icm20608"

/* IIO sysfs 路径 */
#define IIO_DEVICE_PATH "/sys/bus/iio/devices/iio:device0"

/* 滑动平均滤波窗口大小 */
#define FILTER_WINDOW 8

/* 滑动平均滤波器 */
typedef struct {
        float buf[FILTER_WINDOW];
        int idx;
        int count;
        float sum;
} moving_avg_t;

static void ma_init(moving_avg_t *f) {
        int i;
        for (i = 0; i < FILTER_WINDOW; i++) f->buf[i] = 0.0f;
        f->idx = 0;
        f->count = 0;
        f->sum = 0.0f;
}

static float ma_filter(moving_avg_t *f, float val) {
        f->sum -= f->buf[f->idx];
        f->buf[f->idx] = val;
        f->sum += val;
        f->idx = (f->idx + 1) % FILTER_WINDOW;
        if (f->count < FILTER_WINDOW) f->count++;
        return f->sum / (float)f->count;
}

/* 辅助函数：从 sysfs 读取一个整数 */
static int read_sysfs_int(const char *attr) {
        char path[256];
        char buf[32];
        int fd, ret;
        snprintf(path, sizeof(path), "%s/%s", IIO_DEVICE_PATH, attr);
        fd = open(path, O_RDONLY);
        if (fd < 0) return 0;
        ret = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (ret < 0) return 0;
        buf[ret] = '\0';
        return atoi(buf);
}

/* 绘制进度条: val 在 [-range, +range] 范围内, 宽度 width */
static void draw_bar(char *bar, int width, float val, float range) {
        int i, mid = width / 2;
        int blen = (int)((val + range) / (2.0f * range) * (float)width);
        if (blen < 0) blen = 0;
        if (blen > width) blen = width;
        for (i = 0; i < blen; i++) bar[i] = '=';
        for (; i < width; i++) bar[i] = ' ';
        bar[mid] = '|';
        bar[width] = '\0';
}

int control_icm20608(char *device) {
        int gx_raw, gy_raw, gz_raw;
        int ax_raw, ay_raw, az_raw;
        int temp_raw;
        float gx, gy, gz;
        float ax, ay, az;
        float temp_c;
        char bar[41];

        /* 7 个滤波器: ax, ay, az, gx, gy, gz, temp */
        moving_avg_t fax, fay, faz, fgx, fgy, fgz, ftemp;
        ma_init(&fax); ma_init(&fay); ma_init(&faz);
        ma_init(&fgx); ma_init(&fgy); ma_init(&fgz);
        ma_init(&ftemp);

        (void)device;

        printf("\033[2J\033[H");
        printf("╔══════════════════════════════════════════════════════════╗\n");
        printf("║       ICM-20608  6-Axis + Temp  Monitor (Filtered)     ║\n");
        printf("╠══════════════════════════════════════════════════════════╣\n");

        while (1) {
                /* ---- 加速度计 ---- */
                ax_raw = read_sysfs_int("in_accel_x_raw");
                ay_raw = read_sysfs_int("in_accel_y_raw");
                az_raw = read_sysfs_int("in_accel_z_raw");
                ax = ma_filter(&fax, ax_raw * 0.000598f / 9.8f);
                ay = ma_filter(&fay, ay_raw * 0.000598f / 9.8f);
                az = ma_filter(&faz, az_raw * 0.000598f / 9.8f);

                /* ---- 陀螺仪 ---- */
                gx_raw = read_sysfs_int("in_anglvel_x_raw");
                gy_raw = read_sysfs_int("in_anglvel_y_raw");
                gz_raw = read_sysfs_int("in_anglvel_z_raw");
                gx = ma_filter(&fgx, gx_raw * 0.001065f);
                gy = ma_filter(&fgy, gy_raw * 0.001065f);
                gz = ma_filter(&fgz, gz_raw * 0.001065f);

                /* ---- 温度 ---- */
                temp_raw = read_sysfs_int("in_temp_raw");
                temp_c = ma_filter(&ftemp, temp_raw / 326.8f + 25.0f);

                printf("\033[5;0H");

                /* 加速度 */
                draw_bar(bar, 40, ax, 2.0f);
                printf("║ Accel X [%s] %+6.3fg (%6d) ║\n", bar, ax, ax_raw);
                draw_bar(bar, 40, ay, 2.0f);
                printf("║ Accel Y [%s] %+6.3fg (%6d) ║\n", bar, ay, ay_raw);
                draw_bar(bar, 40, az, 2.0f);
                printf("║ Accel Z [%s] %+6.3fg (%6d) ║\n", bar, az, az_raw);

                printf("╠══════════════════════════════════════════════════════════╣\n");

                /* 陀螺仪 */
                draw_bar(bar, 40, gx, 5.0f);
                printf("║ Gyro  X [%s] %+7.3f rad/s (%6d) ║\n", bar, gx, gx_raw);
                draw_bar(bar, 40, gy, 5.0f);
                printf("║ Gyro  Y [%s] %+7.3f rad/s (%6d) ║\n", bar, gy, gy_raw);
                draw_bar(bar, 40, gz, 5.0f);
                printf("║ Gyro  Z [%s] %+7.3f rad/s (%6d) ║\n", bar, gz, gz_raw);

                printf("╠══════════════════════════════════════════════════════════╣\n");

                /* 温度 */
                printf("║ Temp     %+8.2f °C   (raw: %6d)                       ║\n",
                       temp_c, temp_raw);

                printf("╠══════════════════════════════════════════════════════════╣\n");
                printf("║ Accel ±2g | Gyro ±2000dps | Filter W=%d | 100ms refresh║\n",
                       FILTER_WINDOW);
                printf("╚══════════════════════════════════════════════════════════╝\n");

                usleep(100000);
        }
        return 0;
}
int control_ap3216c(char *device) {
        int fd, return_value;
        unsigned short data[3];
        unsigned short ps, ir, als;
        fd = open(device, O_RDONLY);
        if (fd < 0) {
                printf("open %s error\n", device);
                return -1;
        }
        while (1) {
                return_value = read(fd, data, sizeof(data));
                if (return_value < 0) {
                        printf("read %s error\n", device);
                        return -1;
                } else {
                        ir = data[0];
                        als = data[1];
                        ps = data[2];
                        printf("ap3216c data:ir= %d,als=%d,ps=%d \r\n", ir, als,
                               ps);
                }
                usleep(200000);//延时200ms
        }
        close(fd);

        return 0;
}
int control_input_key(char *device) {
        static struct input_event ie;
        int fd, return_value;

        fd = open(device, O_RDWR);
        if (fd < 0) {
                printf("open %s error\n", device);
                return -1;
        }
        while (1) {
                return_value =
                        read(fd, &ie, sizeof(struct input_event)); // 读取数据
                if (return_value < 0) {
                        printf("read %s error\n", device);
                } else {
                        printf("type: %d, code: %d, value: %d\n", ie.type,
                               ie.code, ie.value);
                        switch (ie.type) {
                        case EV_KEY:
                                switch (ie.code) {
                                case EV_KEY:
                                        printf("按键A按下\r\n");
                                        break;
                                case EV_REL:
                                        printf("按键A移动 EV_REL\r\n");
                                        break;
                                case EV_ABS:
                                        printf("按键A移动 EV_ABS\r\n");
                                        break;
                                }
                        }
                }
        }
        close(fd);
        return 0;
}

int control_led(char *device, int command) {
        int fd, return_value;
        unsigned char databuf[1];
        fd = open(device, O_RDWR);
        if (fd < 0) {
                printf("open %s error\n", device);
                return -1;
        }
        switch (command) {
        case CMD_LED_ON:
                databuf[0] = 1;
                break;
        case CMD_LED_OFF:
                databuf[0] = 0;
                break;
        default:
                printf("Unknown command for LED: %d\n", command);
                close(fd);
                return -1;
        }
        return_value = write(fd, databuf, sizeof(databuf));
        if (return_value < 0) {
                printf("write %s error\n", device);
                close(fd);
                return -1;
        }
        return_value = close(fd);
        if (return_value < 0) {
                printf("close %s error\n", device);
                return -1;
        }
        return 0;
}

void show_menu() {
        printf("\n===== 设备控制程序 =====\n");
        printf("支持的设备:\n");
        printf("  1. led\n");
        printf("  2. input_key\n");
        printf("  3. i2c_ap3216c\n");
        printf("  4. spi_icm20608\n");
        printf("\n");
        printf("支持的命令:\n");
        printf("  LED设备:\n");
        printf("    0 - 关闭LED\n");
        printf("    1 - 打开LED\n");
        printf("  input_key设备:\n");
        printf("  i2c_ap3216c设备:\n");
        printf("\n");
        printf("使用方法:\n");
        printf("  交互式: %s\n", "myctl");
        printf("  命令行: %s <device> <command>\n", "myctl");
        printf("  例如  : %s led 1\n", "myctl");
        printf("========================\n\n");
}

int interactive_mode() {
        int device_choice, command_choice;
        char input[10];

        show_menu();

        printf("请选择要操作的设备:\n");
        printf("1. LED\n");
        printf("2. input_key\n");
        printf("3. i2c_ap3216c\n");
        printf("4. spi_icm20608\n");
        printf("0. 退出\n");
        printf("请输入选择 (0-4): ");

        if (fgets(input, sizeof(input), stdin) == NULL) {
                printf("输入错误\n");
                return -1;
        }

        device_choice = atoi(input);
        if (device_choice == 0) {
                printf("退出程序\n");
                return 0;
        }

        // 执行命令
        switch (device_choice) {
        case 1: // LED
                printf("\n请选择命令:\n");
                printf("0. 关闭\n");
                printf("1. 打开\n");
                printf("请输入命令 (0-1): ");

                if (fgets(input, sizeof(input), stdin) == NULL) {
                        printf("输入错误\n");
                        return -1;
                }

                command_choice = atoi(input);
                if (command_choice != 0 && command_choice != 1) {
                        printf("无效的命令选择\n");
                        return -1;
                }

                return control_led(DEVICE_FILE_NAME_LED, command_choice);
        case 2: // input_key
                printf("\n请查看:\n");
                return control_input_key(DEVICE_FILE_NAME_INPUT_KEY);
        case 3:
                printf("\n请查看:\n");
                return control_ap3216c(DEVICE_FILE_NAME_AP3216C);
        case 4:
                printf("\n请查看:\n");
                return control_icm20608(DEVICE_FILE_NAME_ICM20608);
        default:
                printf("无效的设备\n");
                return -1;
        }

        return 0;
}

int main(int argc, char *argv[]) {
        if (argc != 3) {
                // 进入交互式模式
                return interactive_mode();
        }

        char *device = argv[1];
        int command = atoi(argv[2]);

        if (strcmp(device, "led") == 0) {
                return control_led(DEVICE_FILE_NAME_LED, command);
        } else {
                printf("Unsupported device: %s\n", device);
                return -1;
        }

        return 0;
}
