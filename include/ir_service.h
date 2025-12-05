/**
 * @file ir_service.h
 * @brief IR服务层 - 整合HAL、IRDB协议和加载器
 */

#ifndef IR_SERVICE_H
#define IR_SERVICE_H

#include "ir_hal.h"
#include "irdb_loader.h"
#include "irdb_protocol.h"
#include <stddef.h>


/* IR服务配置 */
typedef struct {
  irdb_load_method_t load_method;
  char manufacturer[64];
  char device_type[64];
  uint8_t device;
  uint8_t subdevice;
} ir_service_config_t;

/* IR服务初始化 */
int ir_service_init(void);

/* 加载遥控器数据库 */
int ir_service_load_remote(const ir_service_config_t *config);

/* 从嵌入式数据加载 */
int ir_service_load_embedded_csv(const char *csv_data, const char *manufacturer,
                                 const char *device_type);

/* 发送命令（通过功能名） */
int ir_service_send_command(const char *function_name, uint32_t repeat);

/* 发送原始IRDB条目 */
int ir_service_send_entry(const irdb_entry_t *entry, uint32_t repeat);

/* 接收回调 */
typedef void (*ir_service_rx_callback_t)(const irdb_entry_t *entry,
                                         void *user_data);

/* 启动接收 */
int ir_service_start_receive(ir_service_rx_callback_t callback,
                             void *user_data);

/* 停止接收 */
int ir_service_stop_receive(void);

/* 列出当前数据库的所有功能 */
int ir_service_list_functions(char *buf, size_t buf_size);

/* 获取当前数据库信息 */
const irdb_database_t *ir_service_get_database(void);

#endif /* IR_SERVICE_H */