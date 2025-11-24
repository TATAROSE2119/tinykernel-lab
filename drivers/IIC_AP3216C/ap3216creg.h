#ifndef __AP3216C_H
#define __AP3216C_H

// AP3216C寄存器地址定义
#define AP3216C_SYSTEMCONG   0x00    // 系统配置寄存器
#define AP3216C_INTSTATUS    0x01    // 中断状态寄存器
#define AP3216C_INTCLEAR     0x02    // 中断清除寄存器
#define AP3216C_IRDATALOW    0x0A    // IR数据低字节寄存器
#define AP3216C_IRDATAHIGH   0x0B    // IR数据高字节寄存器
#define AP3216C_ALSDATALOW   0x0C    // ALS数据低字节寄存器
#define AP3216C_ALSDATAHIGH  0X0D    // ALS数据高字节寄存器
#define AP3216C_PSDATALOW    0X0E    // PS数据低字节寄存器
#define AP3216C_PSDATAHIGH   0X0F    // PS数据高字节寄存器

#endif // !__AP3216C_H