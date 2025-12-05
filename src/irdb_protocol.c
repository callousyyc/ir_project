/**
 * @file irdb_protocol.c
 * @brief IRDB协议实现
 */

#include "irdb_protocol.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(irdb_protocol, LOG_LEVEL_INF);

#define TOLERANCE_PERCENT 20
// #define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* 字符串比较（不区分大小写）- 如果系统没有提供 */
#ifndef CONFIG_POSIX_API
static int strcasecmp(const char *s1, const char *s2) {
  while (*s1 && *s2) {
    int c1 = tolower((unsigned char)*s1);
    int c2 = tolower((unsigned char)*s2);
    if (c1 != c2) {
      return c1 - c2;
    }
    s1++;
    s2++;
  }
  return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}
#endif

/* 协议参数表 - 基于IrScrutinizer标准 */
static const irdb_protocol_params_t protocol_params[] = {
    {.protocol_id = IRDB_PROTOCOL_NEC1,
     .name = "NEC1",
     .frequency = 38000,
     .duty_cycle = 33,
     .header_mark = 9000,
     .header_space = 4500,
     .bit_mark = 560,
     .bit_0_space = 560,
     .bit_1_space = 1690,
     .trailer_mark = 560,
     .gap = 108000,
     .device_bits = 8,
     .subdevice_bits = 8,
     .function_bits = 8,
     .toggle_bit = false},
    {.protocol_id = IRDB_PROTOCOL_NEC2,
     .name = "NEC2",
     .frequency = 38000,
     .duty_cycle = 33,
     .header_mark = 9000,
     .header_space = 4500,
     .bit_mark = 560,
     .bit_0_space = 560,
     .bit_1_space = 1690,
     .trailer_mark = 560,
     .gap = 108000,
     .device_bits = 16,
     .subdevice_bits = 0,
     .function_bits = 8,
     .toggle_bit = false},
    {.protocol_id = IRDB_PROTOCOL_RC5,
     .name = "RC5",
     .frequency = 36000,
     .duty_cycle = 25,
     .header_mark = 0,
     .header_space = 0,
     .bit_mark = 889,
     .bit_0_space = 889,
     .bit_1_space = 889,
     .trailer_mark = 0,
     .gap = 113792,
     .device_bits = 5,
     .subdevice_bits = 0,
     .function_bits = 6,
     .toggle_bit = true},
    {.protocol_id = IRDB_PROTOCOL_SONY12,
     .name = "Sony12",
     .frequency = 40000,
     .duty_cycle = 33,
     .header_mark = 2400,
     .header_space = 600,
     .bit_mark = 1200,
     .bit_0_space = 600,
     .bit_1_space = 600, // Sony使用脉冲宽度编码
     .trailer_mark = 0,
     .gap = 45000,
     .device_bits = 5,
     .subdevice_bits = 0,
     .function_bits = 7,
     .toggle_bit = false},
    {.protocol_id = IRDB_PROTOCOL_SONY15,
     .name = "Sony15",
     .frequency = 40000,
     .duty_cycle = 33,
     .header_mark = 2400,
     .header_space = 600,
     .bit_mark = 1200,
     .bit_0_space = 600,
     .bit_1_space = 600,
     .trailer_mark = 0,
     .gap = 45000,
     .device_bits = 8,
     .subdevice_bits = 0,
     .function_bits = 7,
     .toggle_bit = false},
    {.protocol_id = IRDB_PROTOCOL_SAMSUNG32,
     .name = "Samsung32",
     .frequency = 38000,
     .duty_cycle = 33,
     .header_mark = 4500,
     .header_space = 4500,
     .bit_mark = 560,
     .bit_0_space = 560,
     .bit_1_space = 1690,
     .trailer_mark = 560,
     .gap = 108000,
     .device_bits = 8,
     .subdevice_bits = 8,
     .function_bits = 8,
     .toggle_bit = false}};

/* 获取协议参数 */
const irdb_protocol_params_t *
irdb_get_protocol_params(irdb_protocol_id_t protocol) {
  for (size_t i = 0; i < ARRAY_SIZE(protocol_params); i++) {
    if (protocol_params[i].protocol_id == protocol) {
      return &protocol_params[i];
    }
  }
  return NULL;
}

/* 解析CSV行 */
static int parse_csv_line(const char *line, irdb_entry_t *entry) {
  // 格式: function_name, protocol, device, subdevice, function
  // 示例: Power,1,7,7,12

  char func_name[32];
  int protocol, device, subdevice, function;

  // 查找第一个逗号之前的内容作为功能名
  const char *comma = strchr(line, ',');
  if (!comma) {
    return -EINVAL;
  }

  size_t name_len = comma - line;
  if (name_len >= sizeof(func_name)) {
    name_len = sizeof(func_name) - 1;
  }

  strncpy(func_name, line, name_len);
  func_name[name_len] = '\0';

  // 解析数字部分
  if (sscanf(comma + 1, "%d,%d,%d,%d", &protocol, &device, &subdevice,
             &function) != 4) {
    return -EINVAL;
  }

  // 填充条目
  strncpy(entry->function_name, func_name, sizeof(entry->function_name) - 1);
  entry->protocol = protocol;
  entry->device = device;
  entry->subdevice = subdevice;
  entry->function = function;

  return 0;
}

/* 解析CSV数据 */
int irdb_parse_csv(const char *csv_data, irdb_database_t *db) {
  if (!csv_data || !db) {
    return -EINVAL;
  }

  memset(db, 0, sizeof(irdb_database_t));

  const char *p = csv_data;
  uint32_t capacity = 0;

  // 跳过可能的BOM和空行
  while (*p && (*p == '\n' || *p == '\r' || *p == ' ')) {
    p++;
  }

  while (*p) {
    char line[256];
    size_t len = 0;

    // 读取一行
    while (*p && *p != '\n' && *p != '\r' && len < sizeof(line) - 1) {
      line[len++] = *p++;
    }
    line[len] = '\0';

    // 跳过行结束符
    while (*p && (*p == '\n' || *p == '\r')) {
      p++;
    }

    // 跳过空行和注释
    if (len == 0 || line[0] == '#') {
      continue;
    }

    // 扩展数组
    if (db->entry_count >= capacity) {
      capacity = capacity == 0 ? 16 : capacity * 2;
      irdb_entry_t *new_entries =
          realloc(db->entries, capacity * sizeof(irdb_entry_t));
      if (!new_entries) {
        LOG_ERR("Memory allocation failed");
        irdb_free_database(db);
        return -ENOMEM;
      }
      db->entries = new_entries;
    }

    // 解析行
    if (parse_csv_line(line, &db->entries[db->entry_count]) == 0) {
      db->entry_count++;
    }
  }

  LOG_INF("Parsed %u IRDB entries", db->entry_count);
  return 0;
}

/* 释放数据库 */
void irdb_free_database(irdb_database_t *db) {
  if (db && db->entries) {
    free(db->entries);
    memset(db, 0, sizeof(irdb_database_t));
  }
}

/* 查找功能 */
const irdb_entry_t *irdb_find_function(const irdb_database_t *db,
                                       const char *function_name) {
  if (!db || !function_name) {
    return NULL;
  }

  for (uint32_t i = 0; i < db->entry_count; i++) {
    if (strcasecmp(db->entries[i].function_name, function_name) == 0) {
      return &db->entries[i];
    }
  }

  return NULL;
}

/* 编码为原始时序 */
int irdb_encode_to_raw(const irdb_entry_t *entry, uint32_t *timings_out,
                       uint32_t *length_out, uint32_t max_length) {
  if (!entry || !timings_out || !length_out) {
    return -EINVAL;
  }

  const irdb_protocol_params_t *params =
      irdb_get_protocol_params(entry->protocol);
  if (!params) {
    LOG_ERR("Unknown protocol: %u", entry->protocol);
    return -ENOTSUP;
  }

  uint32_t idx = 0;

  // 引导码
  if (params->header_mark > 0) {
    if (idx + 2 > max_length)
      return -ENOMEM;
    timings_out[idx++] = params->header_mark;
    timings_out[idx++] = params->header_space;
  }

  // 组装完整码字
  uint64_t code = 0;
  uint32_t total_bits = 0;

  // 添加设备码
  if (params->device_bits > 0) {
    code = (code << params->device_bits) |
           (entry->device & ((1 << params->device_bits) - 1));
    total_bits += params->device_bits;
  }

  // 添加子设备码
  if (params->subdevice_bits > 0) {
    code = (code << params->subdevice_bits) |
           (entry->subdevice & ((1 << params->subdevice_bits) - 1));
    total_bits += params->subdevice_bits;
  }

  // 添加功能码
  code = (code << params->function_bits) |
         (entry->function & ((1 << params->function_bits) - 1));
  total_bits += params->function_bits;

  // 对于NEC1，添加功能码的反码
  if (entry->protocol == IRDB_PROTOCOL_NEC1) {
    code = (code << 8) | ((~entry->function) & 0xFF);
    total_bits += 8;
  }

  // 编码数据位
  for (int i = total_bits - 1; i >= 0; i--) {
    if (idx + 2 > max_length)
      return -ENOMEM;

    bool bit = (code >> i) & 1;

    // RC5使用曼彻斯特编码
    if (entry->protocol == IRDB_PROTOCOL_RC5) {
      if (bit) {
        timings_out[idx++] = params->bit_0_space; // space then mark
        timings_out[idx++] = params->bit_mark;
      } else {
        timings_out[idx++] = params->bit_mark; // mark then space
        timings_out[idx++] = params->bit_0_space;
      }
    }
    // Sony使用脉冲位置调制
    else if (entry->protocol >= IRDB_PROTOCOL_SONY12 &&
             entry->protocol <= IRDB_PROTOCOL_SONY20) {
      timings_out[idx++] = bit ? params->bit_mark : params->bit_mark / 2;
      timings_out[idx++] = params->bit_0_space;
    }
    // 标准脉冲距离编码
    else {
      timings_out[idx++] = params->bit_mark;
      timings_out[idx++] = bit ? params->bit_1_space : params->bit_0_space;
    }
  }

  // 结束标记
  if (params->trailer_mark > 0) {
    if (idx + 1 > max_length)
      return -ENOMEM;
    timings_out[idx++] = params->trailer_mark;
  }

  *length_out = idx;
  return 0;
}

/* 时序匹配（带容差） */
static bool timing_match(uint32_t measured, uint32_t expected) {
  if (expected == 0)
    return measured == 0;

  uint32_t tolerance = expected * TOLERANCE_PERCENT / 100;
  return (measured >= expected - tolerance) &&
         (measured <= expected + tolerance);
}

/* 解码原始时序 */
int irdb_decode_from_raw(const irdb_database_t *db, const uint32_t *timings,
                         uint32_t length, irdb_entry_t *entry_out) {
  if (!db || !timings || length < 4 || !entry_out) {
    return -EINVAL;
  }

  // 尝试所有数据库条目的协议
  for (uint32_t i = 0; i < db->entry_count; i++) {
    const irdb_entry_t *entry = &db->entries[i];
    const irdb_protocol_params_t *params =
        irdb_get_protocol_params(entry->protocol);

    if (!params)
      continue;

    uint32_t idx = 0;

    // 检查引导码
    if (params->header_mark > 0) {
      if (idx + 2 > length)
        continue;

      if (!timing_match(timings[idx], params->header_mark) ||
          !timing_match(timings[idx + 1], params->header_space)) {
        continue;
      }
      idx += 2;
    }

    // 解码数据位
    uint64_t decoded = 0;
    uint32_t bits_decoded = 0;
    uint32_t total_bits =
        params->device_bits + params->subdevice_bits + params->function_bits;

    if (entry->protocol == IRDB_PROTOCOL_NEC1) {
      total_bits += 8; // 功能码反码
    }

    while (idx + 1 < length && bits_decoded < total_bits) {
      bool bit_value;

      if (entry->protocol == IRDB_PROTOCOL_RC5) {
        // 曼彻斯特解码
        if (timing_match(timings[idx], params->bit_mark) &&
            timing_match(timings[idx + 1], params->bit_0_space)) {
          bit_value = false;
        } else if (timing_match(timings[idx], params->bit_0_space) &&
                   timing_match(timings[idx + 1], params->bit_mark)) {
          bit_value = true;
        } else {
          break;
        }
      } else {
        // 脉冲距离解码
        if (!timing_match(timings[idx], params->bit_mark)) {
          break;
        }

        if (timing_match(timings[idx + 1], params->bit_0_space)) {
          bit_value = false;
        } else if (timing_match(timings[idx + 1], params->bit_1_space)) {
          bit_value = true;
        } else {
          break;
        }
      }

      decoded = (decoded << 1) | (bit_value ? 1 : 0);
      bits_decoded++;
      idx += 2;
    }

    if (bits_decoded != total_bits) {
      continue;
    }

    // 提取字段
    uint16_t function = decoded & ((1 << params->function_bits) - 1);
    decoded >>= params->function_bits;

    if (entry->protocol == IRDB_PROTOCOL_NEC1) {
      decoded >>= 8; // 跳过反码
    }

    uint16_t subdevice = 0;
    if (params->subdevice_bits > 0) {
      subdevice = decoded & ((1 << params->subdevice_bits) - 1);
      decoded >>= params->subdevice_bits;
    }

    uint16_t device = decoded & ((1 << params->device_bits) - 1);

    // 匹配数据库条目
    if (device == entry->device && subdevice == entry->subdevice &&
        function == entry->function) {
      memcpy(entry_out, entry, sizeof(irdb_entry_t));
      return 0;
    }
  }

  return -ENOENT;
}