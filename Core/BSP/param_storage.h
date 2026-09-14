#ifndef __PARAM_STORAGE_H
#define __PARAM_STORAGE_H

#include "main.h"

/* 掉电保存三件套: 设备 ID、采样周期、Modbus 从站地址。
 * 上电读 Flash 并验 magic + CRC, 失败就用默认值。 */

#define PARAM_MAGIC                 0x50524D54UL    /* "PRMT" */
#define PARAM_DEFAULT_DEV_ID        1U
#define PARAM_DEFAULT_MODBUS_ADDR   1U
#define PARAM_DEFAULT_PERIOD_MS     2000U
#define PARAM_PERIOD_MIN_MS         500U
#define PARAM_PERIOD_MAX_MS         60000U

void     Param_Load( void );             /* main 里调一次, 调度器启动前 */
void     Param_Save( void );             /* 擦页+写+回读校验 */

uint8_t  Param_GetDevId( void );
uint8_t  Param_GetModbusAddr( void );
uint16_t Param_GetPeriodMs( void );

void Param_SetDevId( uint8_t id );       /* 自带合法性检查 + 自动落盘 */
void Param_SetModbusAddr( uint8_t addr );
void Param_SetPeriodMs( uint16_t ms );

#endif /* __PARAM_STORAGE_H */
