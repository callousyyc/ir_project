/**
 * @file ir_learning_app.c
 * @brief IR自学习功能应用示例
 */

#include "ir_learning.h"
#include "ir_service.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(ir_learn_app, LOG_LEVEL_INF);

/* 学习状态回调 */
static void learning_callback(ir_learn_status_t status,
                              const ir_learned_signal_t *signal,
                              void *user_data) {
  switch (status) {
  case IR_LEARN_IDLE:
    LOG_INF("Learning: Idle");
    break;

  case IR_LEARN_WAITING:
    LOG_INF("Learning: Waiting for signal...");
    LOG_INF("Please press the button on your remote control");
    break;

  case IR_LEARN_RECEIVING:
    LOG_INF("Learning: Receiving signal...");
    break;

  case IR_LEARN_COMPLETED:
    if (signal) {
      LOG_INF("Learning: Completed!");
      LOG_INF("  Name: %s", signal->name);
      LOG_INF("  Edges: %u", signal->timing_count);
      LOG_INF("  Duration: %u us", signal->total_duration_us);

      /* 分析信号 */
      ir_signal_analysis_t analysis;
      if (ir_learning_analyze(signal, &analysis) == 0) {
        LOG_INF("  Analysis:");
        LOG_INF("    Avg mark: %u us", analysis.avg_mark);
        LOG_INF("    Avg space: %u us", analysis.avg_space);
        LOG_INF("    Estimated freq: %u Hz", analysis.estimated_freq);
      }
    }
    break;

  case IR_LEARN_TIMEOUT:
    LOG_WRN("Learning: Timeout - no signal detected");
    break;

  case IR_LEARN_ERROR:
    LOG_ERR("Learning: Error occurred");
    break;
  }
}

/* 自学习测试函数 */
void test_ir_learning(void) {
  LOG_INF("=== IR Learning Test ===");

  /* 1. 初始化 */
  int ret = ir_learning_init();
  if (ret < 0) {
    LOG_ERR("Failed to init learning: %d", ret);
    return;
  }

  /* 2. 学习Power按键 */
  LOG_INF("\n--- Learning 'Power' button ---");
  ret = ir_learning_start("Power", learning_callback, NULL, 10000);
  if (ret < 0) {
    LOG_ERR("Failed to start learning: %d", ret);
    return;
  }

  /* 等待学习完成 */
  k_sleep(K_SECONDS(12));

  /* 3. 如果成功，保存信号 */
  ir_learned_signal_t power_signal;
  memset(&power_signal, 0, sizeof(power_signal));
  power_signal.timings = k_malloc(IR_LEARNING_MAX_EDGES * sizeof(uint32_t));

  /* 这里假设学习成功，实际应该在回调中处理 */

  /* 4. 学习Volume Up */
  LOG_INF("\n--- Learning 'Volume Up' button ---");
  k_sleep(K_SECONDS(2));

  ret = ir_learning_start("VolumeUp", learning_callback, NULL, 10000);
  k_sleep(K_SECONDS(12));

  LOG_INF("\n=== Learning Test Complete ===");
}

#ifdef CONFIG_SHELL

/* Shell命令: learn - 学习新信号 */
static int cmd_learn(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(sh, "Usage: learn <signal_name> [timeout_ms]");
    return -EINVAL;
  }

  const char *name = argv[1];
  uint32_t timeout = argc > 2 ? atoi(argv[2]) : 5000;

  shell_print(sh, "Starting learning mode...");
  shell_print(sh, "Signal name: %s", name);
  shell_print(sh, "Timeout: %u ms", timeout);
  shell_print(sh, "Point your remote and press the button NOW!");

  int ret = ir_learning_start(name, learning_callback, (void *)sh, timeout);
  if (ret < 0) {
    shell_error(sh, "Failed to start learning: %d", ret);
    return ret;
  }

  return 0;
}

/* Shell命令: replay - 重放学习的信号 */
static int cmd_replay(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(sh, "Usage: replay <signal_name> [repeat]");
    return -EINVAL;
  }

  const char *name = argv[1];
  uint32_t repeat = argc > 2 ? atoi(argv[2]) : 1;

  /* 从存储加载信号 */
  ir_learned_signal_t signal;
  memset(&signal, 0, sizeof(signal));
  signal.timings = k_malloc(IR_LEARNING_MAX_EDGES * sizeof(uint32_t));

  if (!signal.timings) {
    shell_error(sh, "Memory allocation failed");
    return -ENOMEM;
  }

  int ret = ir_learning_load(&signal, name);
  if (ret < 0) {
    shell_error(sh, "Failed to load signal: %d", ret);
    k_free(signal.timings);
    return ret;
  }

  shell_print(sh, "Replaying: %s (%u times)", name, repeat);

  ret = ir_learning_replay(&signal, repeat);
  k_free(signal.timings);

  if (ret < 0) {
    shell_error(sh, "Replay failed: %d", ret);
    return ret;
  }

  shell_print(sh, "Replay completed");
  return 0;
}

/* Shell命令: save - 保存当前学习的信号 */
static int cmd_save(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(sh, "Usage: save <name>");
    return -EINVAL;
  }

  /* 这里需要访问最近学习的信号 */
  shell_error(sh, "Not implemented - use learning callback to auto-save");
  return -ENOTSUP;
}

/* Shell命令: list - 列出所有学习的信号 */
static int cmd_list_learned(const struct shell *sh, size_t argc, char **argv) {
  char buf[1024];

  int ret = ir_learning_list(buf, sizeof(buf));
  if (ret < 0) {
    shell_error(sh, "Failed to list signals: %d", ret);
    return ret;
  }

  shell_print(sh, "%s", buf);
  return 0;
}

/* Shell命令: delete - 删除学习的信号 */
static int cmd_delete(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(sh, "Usage: delete <signal_name>");
    return -EINVAL;
  }

  const char *name = argv[1];

  int ret = ir_learning_delete(name);
  if (ret < 0) {
    shell_error(sh, "Failed to delete: %d", ret);
    return ret;
  }

  shell_print(sh, "Signal deleted: %s", name);
  return 0;
}

/* Shell命令: analyze - 分析信号 */
static int cmd_analyze(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(sh, "Usage: analyze <signal_name>");
    return -EINVAL;
  }

  const char *name = argv[1];

  /* 加载信号 */
  ir_learned_signal_t signal;
  memset(&signal, 0, sizeof(signal));
  signal.timings = k_malloc(IR_LEARNING_MAX_EDGES * sizeof(uint32_t));

  if (!signal.timings) {
    shell_error(sh, "Memory allocation failed");
    return -ENOMEM;
  }

  int ret = ir_learning_load(&signal, name);
  if (ret < 0) {
    shell_error(sh, "Failed to load signal: %d", ret);
    k_free(signal.timings);
    return ret;
  }

  /* 分析 */
  ir_signal_analysis_t analysis;
  ret = ir_learning_analyze(&signal, &analysis);

  if (ret == 0) {
    shell_print(sh, "Signal Analysis: %s", name);
    shell_print(sh, "  Pulse count: %u", analysis.pulse_count);
    shell_print(sh, "  Avg mark: %u us", analysis.avg_mark);
    shell_print(sh, "  Avg space: %u us", analysis.avg_space);
    shell_print(sh, "  Min pulse: %u us", analysis.min_pulse);
    shell_print(sh, "  Max pulse: %u us", analysis.max_pulse);
    shell_print(sh, "  Estimated carrier: %u Hz", analysis.estimated_freq);
  }

  k_free(signal.timings);
  return ret;
}

/* Shell命令: compare - 比较两个信号 */
static int cmd_compare(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 3) {
    shell_error(sh, "Usage: compare <signal1> <signal2>");
    return -EINVAL;
  }

  ir_learned_signal_t sig1, sig2;
  memset(&sig1, 0, sizeof(sig1));
  memset(&sig2, 0, sizeof(sig2));

  sig1.timings = k_malloc(IR_LEARNING_MAX_EDGES * sizeof(uint32_t));
  sig2.timings = k_malloc(IR_LEARNING_MAX_EDGES * sizeof(uint32_t));

  if (!sig1.timings || !sig2.timings) {
    shell_error(sh, "Memory allocation failed");
    if (sig1.timings)
      k_free(sig1.timings);
    if (sig2.timings)
      k_free(sig2.timings);
    return -ENOMEM;
  }

  /* 加载两个信号 */
  int ret1 = ir_learning_load(&sig1, argv[1]);
  int ret2 = ir_learning_load(&sig2, argv[2]);

  if (ret1 < 0 || ret2 < 0) {
    shell_error(sh, "Failed to load signals");
    k_free(sig1.timings);
    k_free(sig2.timings);
    return -EINVAL;
  }

  /* 比较 */
  uint8_t similarity;
  int ret = ir_learning_compare(&sig1, &sig2, &similarity);

  if (ret == 0) {
    shell_print(sh, "Comparing: %s vs %s", argv[1], argv[2]);
    shell_print(sh, "Similarity: %u%%", similarity);

    if (similarity > 90) {
      shell_print(sh, "Result: Signals are nearly identical");
    } else if (similarity > 70) {
      shell_print(sh, "Result: Signals are similar");
    } else if (similarity > 50) {
      shell_print(sh, "Result: Signals have some similarities");
    } else {
      shell_print(sh, "Result: Signals are different");
    }
  }

  k_free(sig1.timings);
  k_free(sig2.timings);
  return ret;
}

/* Shell命令: export - 导出信号为文本格式 */
static int cmd_export(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(sh, "Usage: export <signal_name>");
    return -EINVAL;
  }

  const char *name = argv[1];

  /* 加载信号 */
  ir_learned_signal_t signal;
  memset(&signal, 0, sizeof(signal));
  signal.timings = k_malloc(IR_LEARNING_MAX_EDGES * sizeof(uint32_t));

  if (!signal.timings) {
    shell_error(sh, "Memory allocation failed");
    return -ENOMEM;
  }

  int ret = ir_learning_load(&signal, name);
  if (ret < 0) {
    shell_error(sh, "Failed to load signal: %d", ret);
    k_free(signal.timings);
    return ret;
  }

  /* 导出 */
  char *export_buf = k_malloc(8192);
  if (export_buf) {
    ir_learning_export_raw(&signal, export_buf, 8192);
    shell_print(sh, "%s", export_buf);
    k_free(export_buf);
  }

  k_free(signal.timings);
  return 0;
}

/* 学习命令组 */
SHELL_STATIC_SUBCMD_SET_CREATE(
    learn_cmds, SHELL_CMD(learn, NULL, "Learn a new signal", cmd_learn),
    SHELL_CMD(replay, NULL, "Replay learned signal", cmd_replay),
    SHELL_CMD(list, NULL, "List learned signals", cmd_list_learned),
    SHELL_CMD(delete, NULL, "Delete learned signal", cmd_delete),
    SHELL_CMD(analyze, NULL, "Analyze signal", cmd_analyze),
    SHELL_CMD(compare, NULL, "Compare two signals", cmd_compare),
    SHELL_CMD(export, NULL, "Export signal to raw format", cmd_export),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(irlearn, &learn_cmds, "IR learning commands", NULL);

#endif /* CONFIG_SHELL */