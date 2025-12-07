/**
 * @file ir_learning.c
 * @brief IR自学习模块实现
 */

#include "ir_learning.h"
#include "ir_hal.h"
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>


#ifdef CONFIG_FILE_SYSTEM
#include <zephyr/fs/fs.h>
#endif

LOG_MODULE_REGISTER(ir_learning, LOG_LEVEL_INF);

#define LEARNING_TIMEOUT_DEFAULT_MS 5000
#define SIGNAL_END_TIMEOUT_MS 150
#define MIN_PULSE_US 50
#define LEARNING_STORAGE_PATH "/lfs/ir_learned"

/* 学习状态 */
static struct {
  ir_learned_signal_t current_signal;
  ir_learn_callback_t callback;
  void *user_data;
  struct k_timer timeout_timer;
  struct k_timer end_timer;
  bool active;
  uint32_t edge_count;
  uint32_t start_time_us;
} learn_state;

/* 信号结束检测定时器 */
static void signal_end_handler(struct k_timer *timer) {
  if (!learn_state.active || learn_state.edge_count == 0) {
    return;
  }

  LOG_INF("Signal capture completed: %u edges", learn_state.edge_count);

  learn_state.current_signal.timing_count = learn_state.edge_count;
  learn_state.current_signal.valid = true;
  learn_state.active = false;

  /* 停止HAL接收 */
  ir_hal_rx_stop();

  /* 通知完成 */
  if (learn_state.callback) {
    learn_state.callback(IR_LEARN_COMPLETED, &learn_state.current_signal,
                         learn_state.user_data);
  }
}

/* 学习超时处理 */
static void learning_timeout_handler(struct k_timer *timer) {
  if (!learn_state.active) {
    return;
  }

  LOG_WRN("Learning timeout");

  learn_state.active = false;
  ir_hal_rx_stop();

  if (learn_state.callback) {
    learn_state.callback(IR_LEARN_TIMEOUT, NULL, learn_state.user_data);
  }
}

/* HAL接收回调 - 记录时序 */
static void learning_rx_callback(ir_pulse_t *pulse, void *user_data) {
  /* 安全检查 */
  if (!pulse) {
    LOG_ERR("Pulse is NULL");
    return;
  }

  if (!learn_state.active) {
    return;
  }

  /* 过滤过短的脉冲（噪声） */
  if (pulse->duration_us < MIN_PULSE_US) {
    return;
  }

  /* 关键检查：缓冲区是否存在 */
  if (!learn_state.current_signal.timings) {
    LOG_ERR("Timing buffer is NULL - learning not initialized!");
    /* 尝试恢复：停止学习 */
    learn_state.active = false;
    ir_hal_rx_stop();
    if (learn_state.callback) {
      learn_state.callback(IR_LEARN_ERROR, NULL, learn_state.user_data);
    }
    return;
  }

  /* 第一个脉冲 - 开始录制 */
  if (learn_state.edge_count == 0) {
    LOG_INF("Signal detected, recording...");
    learn_state.start_time_us = k_cyc_to_us_floor32(k_cycle_get_32());

    if (learn_state.callback) {
      learn_state.callback(IR_LEARN_RECEIVING, NULL, learn_state.user_data);
    }
  }

  /* 检查是否超出缓冲区 */
  if (learn_state.edge_count >= IR_LEARNING_MAX_EDGES) {
    LOG_ERR("Buffer overflow (%u edges), stopping", learn_state.edge_count);
    k_timer_stop(&learn_state.end_timer);
    signal_end_handler(NULL);
    return;
  }

  /* 记录时序 */
  learn_state.current_signal.timings[learn_state.edge_count++] =
      pulse->duration_us;

  /* 调试：每10个脉冲打印一次 */
  if (learn_state.edge_count % 10 == 0) {
    LOG_DBG("Recorded %u edges", learn_state.edge_count);
  }

  /* 重置信号结束定时器 */
  k_timer_start(&learn_state.end_timer, K_MSEC(SIGNAL_END_TIMEOUT_MS),
                K_NO_WAIT);
}

/* 初始化学习模块 */
int ir_learning_init(void) {
  memset(&learn_state, 0, sizeof(learn_state));

  /* 初始化定时器 */
  k_timer_init(&learn_state.timeout_timer, learning_timeout_handler, NULL);
  k_timer_init(&learn_state.end_timer, signal_end_handler, NULL);

  /* 分配时序缓冲区 - 关键！必须在这里分配 */
  learn_state.current_signal.timings =
      k_malloc(IR_LEARNING_MAX_EDGES * sizeof(uint32_t));

  if (!learn_state.current_signal.timings) {
    LOG_ERR("Failed to allocate timing buffer");
    return -ENOMEM;
  }

  /* 清零缓冲区 */
  memset(learn_state.current_signal.timings, 0,
         IR_LEARNING_MAX_EDGES * sizeof(uint32_t));

#ifdef CONFIG_FILE_SYSTEM
  /* 创建存储目录 */
  struct fs_dir_t dir;
  fs_dir_t_init(&dir);
  int ret = fs_opendir(&dir, LEARNING_STORAGE_PATH);
  if (ret < 0) {
    ret = fs_mkdir(LEARNING_STORAGE_PATH);
    if (ret < 0 && ret != -EEXIST) {
      LOG_WRN("Failed to create storage directory: %d", ret);
    }
  } else {
    fs_closedir(&dir);
  }
#endif

  LOG_INF("IR Learning initialized, buffer at %p",
          (void *)learn_state.current_signal.timings);
  return 0;
}

/* 开始学习 */
int ir_learning_start(const char *signal_name, ir_learn_callback_t callback,
                      void *user_data, uint32_t timeout_ms) {
  if (learn_state.active) {
    LOG_ERR("Learning already in progress");
    return -EBUSY;
  }

  /* 检查缓冲区是否已分配 */
  if (!learn_state.current_signal.timings) {
    LOG_ERR("Learning not initialized! Call ir_learning_init() first");
    return -EINVAL;
  }

  /* 重置信号数据（但保留timings指针） */
  uint32_t *timings_backup = learn_state.current_signal.timings;
  memset(&learn_state.current_signal, 0, sizeof(ir_learned_signal_t));
  learn_state.current_signal.timings = timings_backup;

  /* 清零缓冲区内容 */
  memset(learn_state.current_signal.timings, 0,
         IR_LEARNING_MAX_EDGES * sizeof(uint32_t));

  learn_state.edge_count = 0;

  if (signal_name) {
    strncpy(learn_state.current_signal.name, signal_name,
            sizeof(learn_state.current_signal.name) - 1);
  }

  learn_state.callback = callback;
  learn_state.user_data = user_data;
  learn_state.active = true;

  LOG_DBG("Learning buffer check: timings=%p, count=%u",
          (void *)learn_state.current_signal.timings, learn_state.edge_count);

  /* 启动HAL接收 */
  int ret = ir_hal_rx_start(learning_rx_callback, NULL);
  if (ret < 0) {
    LOG_ERR("Failed to start RX: %d", ret);
    learn_state.active = false;
    return ret;
  }

  /* 启动超时定时器 */
  uint32_t timeout = timeout_ms > 0 ? timeout_ms : LEARNING_TIMEOUT_DEFAULT_MS;
  k_timer_start(&learn_state.timeout_timer, K_MSEC(timeout), K_NO_WAIT);

  LOG_INF("Learning started: '%s', timeout: %u ms",
          signal_name ? signal_name : "(unnamed)", timeout);

  if (callback) {
    callback(IR_LEARN_WAITING, NULL, user_data);
  }

  return 0;
}

/* 停止学习 */
int ir_learning_stop(void) {
  if (!learn_state.active) {
    return 0;
  }

  learn_state.active = false;
  k_timer_stop(&learn_state.timeout_timer);
  k_timer_stop(&learn_state.end_timer);
  ir_hal_rx_stop();

  LOG_INF("Learning stopped");
  return 0;
}

/* 重放学习的信号 */
int ir_learning_replay(const ir_learned_signal_t *signal,
                       uint32_t repeat_count) {
  if (!signal || !signal->valid) {
    LOG_ERR("Invalid signal");
    return -EINVAL;
  }

  LOG_INF("Replaying signal: '%s' (%u edges, %u repeats)", signal->name,
          signal->timing_count, repeat_count);

  /* 使用检测到的载波频率，默认38kHz */
  uint32_t carrier = signal->carrier_freq > 0 ? signal->carrier_freq : 38000;

  int ret = ir_hal_tx_start(carrier);
  if (ret < 0) {
    return ret;
  }

  for (uint32_t r = 0; r < repeat_count; r++) {
    /* 发送所有时序 */
    for (uint16_t i = 0; i < signal->timing_count; i++) {
      bool is_mark = (i % 2 == 0); // 偶数索引为mark
      ir_hal_tx_pulse(signal->timings[i], is_mark);
    }

    /* 重复间隔 */
    if (r < repeat_count - 1) {
      k_usleep(108000); // 标准间隔
    }
  }

  ir_hal_tx_stop();

  LOG_INF("Replay completed");
  return 0;
}

#ifdef CONFIG_FILE_SYSTEM
/* 保存学习的信号 */
int ir_learning_save(const ir_learned_signal_t *signal, const char *name) {
  if (!signal || !signal->valid || !name) {
    return -EINVAL;
  }

  char filepath[128];
  snprintf(filepath, sizeof(filepath), "%s/%s.dat", LEARNING_STORAGE_PATH,
           name);

  struct fs_file_t file;
  fs_file_t_init(&file);

  int ret = fs_open(&file, filepath, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
  if (ret < 0) {
    LOG_ERR("Failed to open file: %d", ret);
    return ret;
  }

  /* 写入头部信息 */
  fs_write(&file, signal->name, sizeof(signal->name));
  fs_write(&file, &signal->timing_count, sizeof(signal->timing_count));
  fs_write(&file, &signal->carrier_freq, sizeof(signal->carrier_freq));
  fs_write(&file, &signal->total_duration_us,
           sizeof(signal->total_duration_us));

  /* 写入时序数据 */
  fs_write(&file, signal->timings, signal->timing_count * sizeof(uint32_t));

  fs_close(&file);

  LOG_INF("Signal saved: %s (%u bytes)", name,
          signal->timing_count * sizeof(uint32_t));
  return 0;
}

/* 从存储加载信号 */
int ir_learning_load(ir_learned_signal_t *signal, const char *name) {
  if (!signal || !name) {
    return -EINVAL;
  }

  char filepath[128];
  snprintf(filepath, sizeof(filepath), "%s/%s.dat", LEARNING_STORAGE_PATH,
           name);

  struct fs_file_t file;
  fs_file_t_init(&file);

  int ret = fs_open(&file, filepath, FS_O_READ);
  if (ret < 0) {
    LOG_ERR("Failed to open file: %d", ret);
    return ret;
  }

  /* 读取头部 */
  fs_read(&file, signal->name, sizeof(signal->name));
  fs_read(&file, &signal->timing_count, sizeof(signal->timing_count));
  fs_read(&file, &signal->carrier_freq, sizeof(signal->carrier_freq));
  fs_read(&file, &signal->total_duration_us, sizeof(signal->total_duration_us));

  /* 分配并读取时序数据 */
  if (!signal->timings) {
    signal->timings = k_malloc(IR_LEARNING_MAX_EDGES * sizeof(uint32_t));
    if (!signal->timings) {
      fs_close(&file);
      return -ENOMEM;
    }
  }

  fs_read(&file, signal->timings, signal->timing_count * sizeof(uint32_t));

  fs_close(&file);
  signal->valid = true;

  LOG_INF("Signal loaded: %s", name);
  return 0;
}

/* 删除信号 */
int ir_learning_delete(const char *name) {
  if (!name) {
    return -EINVAL;
  }

  char filepath[128];
  snprintf(filepath, sizeof(filepath), "%s/%s.dat", LEARNING_STORAGE_PATH,
           name);

  int ret = fs_unlink(filepath);
  if (ret < 0) {
    LOG_ERR("Failed to delete: %d", ret);
    return ret;
  }

  LOG_INF("Signal deleted: %s", name);
  return 0;
}

/* 列出所有信号 */
int ir_learning_list(char *buf, size_t buf_size) {
  if (!buf) {
    return -EINVAL;
  }

  struct fs_dir_t dir;
  fs_dir_t_init(&dir);

  int ret = fs_opendir(&dir, LEARNING_STORAGE_PATH);
  if (ret < 0) {
    return ret;
  }

  size_t offset = 0;
  offset += snprintf(buf + offset, buf_size - offset, "Learned signals:\n");

  struct fs_dirent entry;
  while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != '\0') {
    if (entry.type == FS_DIR_ENTRY_FILE) {
      /* 去除 .dat 后缀 */
      char *dot = strrchr(entry.name, '.');
      if (dot)
        *dot = '\0';

      offset += snprintf(buf + offset, buf_size - offset, "  %s\n", entry.name);
    }
  }

  fs_closedir(&dir);
  return 0;
}
#else
int ir_learning_save(const ir_learned_signal_t *signal, const char *name) {
  LOG_ERR("File system not enabled");
  return -ENOTSUP;
}

int ir_learning_load(ir_learned_signal_t *signal, const char *name) {
  LOG_ERR("File system not enabled");
  return -ENOTSUP;
}

int ir_learning_delete(const char *name) {
  LOG_ERR("File system not enabled");
  return -ENOTSUP;
}

int ir_learning_list(char *buf, size_t buf_size) {
  if (buf && buf_size > 0) {
    snprintf(buf, buf_size, "File system not enabled\n");
  }
  return -ENOTSUP;
}
#endif

/* 导出为原始格式 */
int ir_learning_export_raw(const ir_learned_signal_t *signal, char *buf,
                           size_t buf_size) {
  if (!signal || !signal->valid || !buf) {
    return -EINVAL;
  }

  size_t offset = 0;
  offset += snprintf(buf + offset, buf_size - offset, "# IR Signal: %s\n",
                     signal->name);
  offset +=
      snprintf(buf + offset, buf_size - offset, "# Edges: %u, Carrier: %u Hz\n",
               signal->timing_count, signal->carrier_freq);
  offset +=
      snprintf(buf + offset, buf_size - offset, "# Format: duration_us\n");

  for (uint16_t i = 0; i < signal->timing_count && offset < buf_size; i++) {
    offset +=
        snprintf(buf + offset, buf_size - offset, "%u\n", signal->timings[i]);
  }

  return 0;
}

/* 分析信号 */
int ir_learning_analyze(const ir_learned_signal_t *signal,
                        ir_signal_analysis_t *analysis) {
  if (!signal || !signal->valid || !analysis) {
    return -EINVAL;
  }

  memset(analysis, 0, sizeof(ir_signal_analysis_t));

  uint64_t mark_sum = 0, space_sum = 0;
  uint32_t mark_count = 0, space_count = 0;

  analysis->min_pulse = UINT32_MAX;
  analysis->max_pulse = 0;
  analysis->pulse_count = signal->timing_count;

  for (uint16_t i = 0; i < signal->timing_count; i++) {
    uint32_t duration = signal->timings[i];

    if (duration < analysis->min_pulse) {
      analysis->min_pulse = duration;
    }
    if (duration > analysis->max_pulse) {
      analysis->max_pulse = duration;
    }

    if (i % 2 == 0) { // Mark
      mark_sum += duration;
      mark_count++;
    } else { // Space
      space_sum += duration;
      space_count++;
    }
  }

  if (mark_count > 0) {
    analysis->avg_mark = mark_sum / mark_count;
  }
  if (space_count > 0) {
    analysis->avg_space = space_sum / space_count;
  }

  /* 估算载波频率 (基于最短mark脉冲) */
  if (analysis->min_pulse > 0) {
    analysis->estimated_freq = 1000000 / (analysis->min_pulse * 2);
    /* 四舍五入到常见频率 */
    if (analysis->estimated_freq > 35000 && analysis->estimated_freq < 41000) {
      analysis->estimated_freq = 38000;
    } else if (analysis->estimated_freq > 33000 &&
               analysis->estimated_freq < 37000) {
      analysis->estimated_freq = 36000;
    } else if (analysis->estimated_freq > 38000 &&
               analysis->estimated_freq < 42000) {
      analysis->estimated_freq = 40000;
    }
  }

  LOG_INF("Analysis: avg_mark=%u, avg_space=%u, freq=%u Hz", analysis->avg_mark,
          analysis->avg_space, analysis->estimated_freq);

  return 0;
}

/* 比较两个信号 */
int ir_learning_compare(const ir_learned_signal_t *sig1,
                        const ir_learned_signal_t *sig2, uint8_t *similarity) {
  if (!sig1 || !sig2 || !sig1->valid || !sig2->valid || !similarity) {
    return -EINVAL;
  }

  /* 如果长度差异太大，相似度为0 */
  if (abs((int)sig1->timing_count - (int)sig2->timing_count) > 10) {
    *similarity = 0;
    return 0;
  }

  uint32_t min_len = sig1->timing_count < sig2->timing_count
                         ? sig1->timing_count
                         : sig2->timing_count;

  uint32_t matches = 0;
  uint32_t tolerance = 200; // 200us容差

  for (uint32_t i = 0; i < min_len; i++) {
    uint32_t t1 = sig1->timings[i];
    uint32_t t2 = sig2->timings[i];
    uint32_t diff = t1 > t2 ? t1 - t2 : t2 - t1;

    if (diff <= tolerance) {
      matches++;
    }
  }

  *similarity = (matches * 100) / min_len;
  return 0;
}