/**
 * @file ir_service.c
 * @brief IR服务层实现
 */

#include "ir_service.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ir_service, LOG_LEVEL_INF);

#define MAX_RAW_TIMINGS 512

/* 服务状态 */
static struct {
  irdb_database_t current_db;
  bool db_loaded;

  /* 接收状态 */
  struct {
    ir_service_rx_callback_t callback;
    void *user_data;
    uint32_t timings[MAX_RAW_TIMINGS];
    uint32_t timing_count;
    struct k_timer timeout_timer;
    bool active;
  } rx;
} service_state;

/* 接收超时处理 */
static void rx_timeout_handler(struct k_timer *timer) {
  if (service_state.rx.timing_count > 0 && service_state.rx.callback) {
    irdb_entry_t decoded_entry;

    int ret = irdb_decode_from_raw(
        &service_state.current_db, service_state.rx.timings,
        service_state.rx.timing_count, &decoded_entry);

    if (ret == 0) {
      LOG_INF("Decoded: %s (P:%u D:%u.%u F:%u)", decoded_entry.function_name,
              decoded_entry.protocol, decoded_entry.device,
              decoded_entry.subdevice, decoded_entry.function);

      service_state.rx.callback(&decoded_entry, service_state.rx.user_data);
    } else {
      LOG_WRN("Failed to decode signal");
    }

    service_state.rx.timing_count = 0;
  }
}

/* HAL接收回调 */
static void hal_rx_callback(ir_pulse_t *pulse, void *user_data) {
  if (!service_state.rx.active) {
    return;
  }

  if (service_state.rx.timing_count < MAX_RAW_TIMINGS) {
    service_state.rx.timings[service_state.rx.timing_count++] =
        pulse->duration_us;
  }

  /* 重置超时 */
  k_timer_start(&service_state.rx.timeout_timer, K_MSEC(150), K_NO_WAIT);
}

/* 服务初始化 */
int ir_service_init(void) {
  memset(&service_state, 0, sizeof(service_state));

  /* 初始化HAL */
  int ret = ir_hal_init();
  if (ret < 0) {
    LOG_ERR("HAL init failed: %d", ret);
    return ret;
  }

  /* 初始化接收定时器 */
  k_timer_init(&service_state.rx.timeout_timer, rx_timeout_handler, NULL);

  LOG_INF("IR Service initialized");
  return 0;
}

/* 加载遥控器数据库 */
int ir_service_load_remote(const ir_service_config_t *config) {
  if (!config) {
    return -EINVAL;
  }

  /* 释放旧数据库 */
  if (service_state.db_loaded) {
    irdb_free_database(&service_state.current_db);
    service_state.db_loaded = false;
  }

  int ret = -EINVAL;
  char path[128];

  switch (config->load_method) {
  case IRDB_LOAD_FILESYSTEM:
    irdb_build_path(path, sizeof(path), config->manufacturer,
                    config->device_type, config->device, config->subdevice);
    ret = irdb_load_from_file(&service_state.current_db, path);
    break;

  case IRDB_LOAD_HTTP:
    ret = irdb_load_from_http(&service_state.current_db, config->manufacturer,
                              config->device_type, config->device,
                              config->subdevice);
    break;

  default:
    LOG_ERR("Unsupported load method");
    return -ENOTSUP;
  }

  if (ret == 0) {
    strncpy(service_state.current_db.manufacturer, config->manufacturer,
            sizeof(service_state.current_db.manufacturer) - 1);
    strncpy(service_state.current_db.device_type, config->device_type,
            sizeof(service_state.current_db.device_type) - 1);

    service_state.db_loaded = true;
    LOG_INF("Loaded remote: %s %s (%u,%u) - %u functions", config->manufacturer,
            config->device_type, config->device, config->subdevice,
            service_state.current_db.entry_count);
  }

  return ret;
}

/* 从嵌入式CSV加载 */
int ir_service_load_embedded_csv(const char *csv_data, const char *manufacturer,
                                 const char *device_type) {
  if (!csv_data) {
    return -EINVAL;
  }

  /* 释放旧数据库 */
  if (service_state.db_loaded) {
    irdb_free_database(&service_state.current_db);
    service_state.db_loaded = false;
  }

  int ret = irdb_load_embedded(&service_state.current_db, csv_data);

  if (ret == 0) {
    if (manufacturer) {
      strncpy(service_state.current_db.manufacturer, manufacturer,
              sizeof(service_state.current_db.manufacturer) - 1);
    }
    if (device_type) {
      strncpy(service_state.current_db.device_type, device_type,
              sizeof(service_state.current_db.device_type) - 1);
    }

    service_state.db_loaded = true;
    LOG_INF("Loaded embedded database: %u functions",
            service_state.current_db.entry_count);
  }

  return ret;
}

/* 发送命令 */
int ir_service_send_command(const char *function_name, uint32_t repeat) {
  if (!function_name) {
    return -EINVAL;
  }

  if (!service_state.db_loaded) {
    LOG_ERR("No database loaded");
    return -EINVAL;
  }

  /* 查找功能 */
  const irdb_entry_t *entry =
      irdb_find_function(&service_state.current_db, function_name);
  if (!entry) {
    LOG_ERR("Function not found: %s", function_name);
    return -ENOENT;
  }

  return ir_service_send_entry(entry, repeat);
}

/* 发送IRDB条目 */
int ir_service_send_entry(const irdb_entry_t *entry, uint32_t repeat) {
  if (!entry) {
    return -EINVAL;
  }

  /* 获取协议参数 */
  const irdb_protocol_params_t *params =
      irdb_get_protocol_params(entry->protocol);
  if (!params) {
    LOG_ERR("Unknown protocol: %u", entry->protocol);
    return -ENOTSUP;
  }

  /* 编码为原始时序 */
  uint32_t timings[MAX_RAW_TIMINGS];
  uint32_t timing_count;

  int ret = irdb_encode_to_raw(entry, timings, &timing_count, MAX_RAW_TIMINGS);
  if (ret < 0) {
    LOG_ERR("Encoding failed: %d", ret);
    return ret;
  }

  LOG_DBG("Sending: %s (P:%u D:%u.%u F:%u) %u timings", entry->function_name,
          entry->protocol, entry->device, entry->subdevice, entry->function,
          timing_count);

  /* 启动发送 */
  ret = ir_hal_tx_start(params->frequency);
  if (ret < 0) {
    return ret;
  }

  /* 发送重复次数 */
  for (uint32_t r = 0; r < repeat; r++) {
    /* 发送所有时序 */
    for (uint32_t i = 0; i < timing_count; i++) {
      bool is_mark = (i % 2 == 0); // 偶数索引为mark
      ir_hal_tx_pulse(timings[i], is_mark);
    }

    /* 重复间隔 */
    if (r < repeat - 1 && params->gap > 0) {
      k_usleep(params->gap);
    }
  }

  /* 停止发送 */
  ir_hal_tx_stop();

  LOG_INF("Sent: %s (%u repeats)", entry->function_name, repeat);
  return 0;
}

/* 启动接收 */
int ir_service_start_receive(ir_service_rx_callback_t callback,
                             void *user_data) {
  if (!callback) {
    return -EINVAL;
  }

  if (!service_state.db_loaded) {
    LOG_ERR("No database loaded");
    return -EINVAL;
  }

  service_state.rx.callback = callback;
  service_state.rx.user_data = user_data;
  service_state.rx.timing_count = 0;
  service_state.rx.active = true;

  int ret = ir_hal_rx_start(hal_rx_callback, NULL);
  if (ret < 0) {
    LOG_ERR("Failed to start receive: %d", ret);
    return ret;
  }

  LOG_INF("Started receiving");
  return 0;
}

/* 停止接收 */
int ir_service_stop_receive(void) {
  service_state.rx.active = false;
  k_timer_stop(&service_state.rx.timeout_timer);
  ir_hal_rx_stop();

  LOG_INF("Stopped receiving");
  return 0;
}

/* 列出所有功能 */
int ir_service_list_functions(char *buf, size_t buf_size) {
  if (!buf || buf_size == 0) {
    return -EINVAL;
  }

  if (!service_state.db_loaded) {
    return -EINVAL;
  }

  size_t offset = 0;
  const irdb_database_t *db = &service_state.current_db;

  offset += snprintf(buf + offset, buf_size - offset, "Remote: %s %s\n",
                     db->manufacturer, db->device_type);

  offset += snprintf(buf + offset, buf_size - offset, "Functions (%u):\n",
                     db->entry_count);

  for (uint32_t i = 0; i < db->entry_count && offset < buf_size; i++) {
    offset +=
        snprintf(buf + offset, buf_size - offset, "  %-20s P:%u D:%u.%u F:%u\n",
                 db->entries[i].function_name, db->entries[i].protocol,
                 db->entries[i].device, db->entries[i].subdevice,
                 db->entries[i].function);
  }

  return 0;
}

/* 获取数据库 */
const irdb_database_t *ir_service_get_database(void) {
  return service_state.db_loaded ? &service_state.current_db : NULL;
}