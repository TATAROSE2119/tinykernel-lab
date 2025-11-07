#include "fcntl.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "sys/stat.h"
#include "sys/types.h"
#include "unistd.h"

int main(int argc, char *argv[]) {
        int fd, return_value;
        char *filename;
        unsigned char databuf[1];

        if (argc != 3) {
                printf("Usage: %s filename\n", argv[0]);
                return -1;
        }
        filename = argv[1];
        fd = open(filename, O_RDWR);
        if (fd < 0) {
                printf("open %s error\n", filename);
                return -1;
        }
        databuf[0] = atoi(argv[2]); // 输入数据

        return_value = write(fd, databuf, sizeof(databuf));
        if (return_value < 0) {
                printf("write %s error\n", filename);
				close(fd);
				return -1;
        }
		return_value=close(fd);
		if (return_value < 0){	
				printf("close %s error\n", filename);
				return -1;
		}
        return 0;
}