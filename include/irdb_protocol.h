/**
 * @file irdb_protocol.h
 * @brief IRDB协议定义和CSV解析器
 *
 * IRDB使用 protocol,device,subdevice,function 格式存储IR码
 */

#ifndef IRDB_PROTOCOL_H
#define IRDB_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>


/* IRDB协议编号 */
typedef enum {
  IRDB_PROTOCOL_NEC1 = 1,
  IRDB_PROTOCOL_NEC2 = 2,
  IRDB_PROTOCOL_RC5 = 4,
  IRDB_PROTOCOL_RC6 = 5,
  IRDB_PROTOCOL_SONY12 = 15,
  IRDB_PROTOCOL_SONY15 = 16,
  IRDB_PROTOCOL_SONY20 = 17,
  IRDB_PROTOCOL_SAMSUNG32 = 20,
  IRDB_PROTOCOL_SAMSUNG36 = 21,
} irdb_protocol_id_t;

/* IRDB条目 */
typedef struct {
  char function_name[32]; // 功能名称 (如 "Power", "Vol+")
  uint16_t protocol;      // 协议编号
  uint16_t device;        // 设备码
  uint16_t subdevice;     // 子设备码
  uint16_t function;      // 功能码
} irdb_entry_t;

/* IRDB数据库 */
typedef struct {
  char manufacturer[64]; // 制造商
  char device_type[64];  // 设备类型
  irdb_entry_t *entries; // 条目数组
  uint32_t entry_count;  // 条目数量
} irdb_database_t;

/* 协议参数表 */
typedef struct {
  irdb_protocol_id_t protocol_id;
  const char *name;
  uint32_t frequency;     // 载波频率(Hz)
  uint32_t duty_cycle;    // 占空比(%)
  uint32_t header_mark;   // 引导标记(us)
  uint32_t header_space;  // 引导间隔(us)
  uint32_t bit_mark;      // 位标记(us)
  uint32_t bit_0_space;   // 0位间隔(us)
  uint32_t bit_1_space;   // 1位间隔(us)
  uint32_t trailer_mark;  // 结束标记(us)
  uint32_t gap;           // 重复间隔(us)
  uint8_t device_bits;    // 设备码位数
  uint8_t subdevice_bits; // 子设备码位数
  uint8_t function_bits;  // 功能码位数
  bool toggle_bit;        // 是否有toggle位
} irdb_protocol_params_t;

/* 协议参数查询 */
const irdb_protocol_params_t *
irdb_get_protocol_params(irdb_protocol_id_t protocol);

/* CSV解析 */
int irdb_parse_csv(const char *csv_data, irdb_database_t *db);

/* 释放数据库 */
void irdb_free_database(irdb_database_t *db);

/* 查找功能 */
const irdb_entry_t *irdb_find_function(const irdb_database_t *db,
                                       const char *function_name);

/* 编码为原始数据 */
int irdb_encode_to_raw(const irdb_entry_t *entry, uint32_t *timings_out,
                       uint32_t *length_out, uint32_t max_length);

/* 解码原始数据 */
int irdb_decode_from_raw(const irdb_database_t *db, const uint32_t *timings,
                         uint32_t length, irdb_entry_t *entry_out);

#endif /* IRDB_PROTOCOL_H */