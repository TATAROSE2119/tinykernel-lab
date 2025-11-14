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
                if (return_value < 0){
                        printf("read %s error\n", device);
                }else{
                        printf("type: %d, code: %d, value: %d\n",
                               ie.type, ie.code, ie.value);
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
        printf("\n");
        printf("支持的命令:\n");
        printf("  LED设备:\n");
        printf("    0 - 关闭LED\n");
        printf("    1 - 打开LED\n");
        printf("  input_key设备:\n");
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
        printf("0. 退出\n");
        printf("请输入选择 (0-2): ");

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
        case 2:// input_key
                printf("\n请查看:\n");
                return control_input_key(DEVICE_FILE_NAME_INPUT_KEY);
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