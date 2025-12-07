/**
 * @file main.c
 * @brief IR遥控应用 - 使用IRDB数据库 + 自学习功能
 */

#include "ir_learning.h"
#include "ir_service.h"
#include <stdlib.h> // 添加：atoi
#include <string.h> // 添加：strcmp, strcpy
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>


LOG_MODULE_REGISTER(ir_app, LOG_LEVEL_INF);

/* Zephyr已经定义了ARRAY_SIZE，不需要重复定义 */

/* 嵌入式IRDB数据 - Samsung TV (7,7) */
static const char samsung_tv_7_7[] = "Power,1,7,7,2\n"
                                     "Source,1,7,7,1\n"
                                     "Menu,1,7,7,26\n"
                                     "Up,1,7,7,96\n"
                                     "Down,1,7,7,97\n"
                                     "Left,1,7,7,101\n"
                                     "Right,1,7,7,98\n"
                                     "Enter,1,7,7,104\n"
                                     "Back,1,7,7,88\n"
                                     "Vol+,1,7,7,7\n"
                                     "Vol-,1,7,7,11\n"
                                     "Mute,1,7,7,15\n"
                                     "Ch+,1,7,7,18\n"
                                     "Ch-,1,7,7,16\n"
                                     "1,1,7,7,4\n"
                                     "2,1,7,7,5\n"
                                     "3,1,7,7,6\n"
                                     "4,1,7,7,8\n"
                                     "5,1,7,7,9\n"
                                     "6,1,7,7,10\n"
                                     "7,1,7,7,12\n"
                                     "8,1,7,7,13\n"
                                     "9,1,7,7,14\n"
                                     "0,1,7,7,17\n";

/* Sony TV IRDB数据 */
static const char sony_tv[] = "Power,15,1,0,21\n"
                              "Vol+,15,1,0,18\n"
                              "Vol-,15,1,0,19\n"
                              "Ch+,15,1,0,16\n"
                              "Ch-,15,1,0,17\n"
                              "Mute,15,1,0,20\n"
                              "Input,15,1,0,37\n"
                              "1,15,1,0,0\n"
                              "2,15,1,0,1\n"
                              "3,15,1,0,2\n"
                              "4,15,1,0,3\n"
                              "5,15,1,0,4\n"
                              "6,15,1,0,5\n"
                              "7,15,1,0,6\n"
                              "8,15,1,0,7\n"
                              "9,15,1,0,8\n"
                              "0,15,1,0,9\n";

/* 接收回调 */
static void rx_callback(const irdb_entry_t *entry, void *user_data) {
  LOG_INF("Received: %s", entry->function_name);
  LOG_INF("  Protocol: %u, Device: %u.%u, Function: %u", entry->protocol,
          entry->device, entry->subdevice, entry->function);

  /* 根据接收到的命令执行操作 */
  if (strcmp(entry->function_name, "Power") == 0) {
    LOG_INF(">> Power button action");
  } else if (strcmp(entry->function_name, "Vol+") == 0) {
    LOG_INF(">> Volume up action");
  } else if (strcmp(entry->function_name, "Vol-") == 0) {
    LOG_INF(">> Volume down action");
  }
}

/* 发送测试 */
static void test_send(void) {
  LOG_INF("=== Send Test ===");

  const char *test_commands[] = {"Power", "Vol+", "Vol-", "Ch+", "Mute"};

  for (int i = 0; i < ARRAY_SIZE(test_commands); i++) {
    LOG_INF("Sending: %s", test_commands[i]);

    int ret = ir_service_send_command(test_commands[i], 1);
    if (ret < 0) {
      LOG_ERR("Failed to send %s: %d", test_commands[i], ret);
    }

    k_sleep(K_MSEC(500));
  }
}

/* 接收测试 */
static void test_receive(uint32_t duration_sec) {
  LOG_INF("=== Receive Test ===");
  LOG_INF("Point remote at receiver and press buttons...");

  int ret = ir_service_start_receive(rx_callback, NULL);
  if (ret < 0) {
    LOG_ERR("Failed to start receive: %d", ret);
    return;
  }

  k_sleep(K_SECONDS(duration_sec));

  ir_service_stop_receive();
  LOG_INF("Receive test completed");
}

/* 主函数 */
int main(void) {
  LOG_INF("========================================");
  LOG_INF("  IR Remote Control with IRDB");
  LOG_INF("  + Self-Learning Feature");
  LOG_INF("  nRF52840 + Zephyr RTOS");
  LOG_INF("========================================");

  /* 初始化服务 */
  int ret = ir_service_init();
  if (ret < 0) {
    LOG_ERR("Service init failed: %d", ret);
    return ret;
  }

  /* 初始化学习模块 */
  ret = ir_learning_init();
  if (ret < 0) {
    LOG_ERR("Learning init failed: %d", ret);
    return ret;
  }

  /* 方式1: 从嵌入式数据加载 */
  LOG_INF("Loading Samsung TV database (embedded)...");
  ret = ir_service_load_embedded_csv(samsung_tv_7_7, "Samsung", "TV");
  if (ret < 0) {
    LOG_ERR("Failed to load database: %d", ret);
    return ret;
  }

  /* 显示数据库信息 */
  char list_buf[1024];
  ir_service_list_functions(list_buf, sizeof(list_buf));
  LOG_INF("\n%s", list_buf);

  k_sleep(K_SECONDS(2));

  /* 运行测试 */
  while (1) {
    LOG_INF("\n>>> Test Cycle Start <<<\n");

    /* 发送测试 */
    test_send();
    k_sleep(K_SECONDS(2));

    /* 接收测试 */
    test_receive(30);
    k_sleep(K_SECONDS(2));

    LOG_INF("\n>>> Test Cycle Complete <<<\n");

/* 可选: 切换到Sony遥控器 */
#if 0
        LOG_INF("Switching to Sony TV database...");
        ir_service_load_embedded_csv(sony_tv, "Sony", "TV");
        ir_service_list_functions(list_buf, sizeof(list_buf));
        LOG_INF("\n%s", list_buf);
#endif
  }

  return 0;
}

/* Shell命令接口 */
#ifdef CONFIG_SHELL

#include <zephyr/shell/shell.h>

/* 加载数据库命令 */
static int cmd_load(const struct shell *shell, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(shell, "Usage: ir load <samsung|sony>");
    return -EINVAL;
  }

  int ret;
  if (strcmp(argv[1], "samsung") == 0) {
    ret = ir_service_load_embedded_csv(samsung_tv_7_7, "Samsung", "TV");
  } else if (strcmp(argv[1], "sony") == 0) {
    ret = ir_service_load_embedded_csv(sony_tv, "Sony", "TV");
  } else {
    shell_error(shell, "Unknown remote: %s", argv[1]);
    return -EINVAL;
  }

  if (ret == 0) {
    shell_print(shell, "Database loaded successfully");
  } else {
    shell_error(shell, "Failed to load: %d", ret);
  }

  return ret;
}

/* 发送命令 */
static int cmd_send(const struct shell *shell, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(shell, "Usage: ir send <function> [repeat]");
    return -EINVAL;
  }

  const char *function = argv[1];
  uint32_t repeat = argc > 2 ? atoi(argv[2]) : 1;

  shell_print(shell, "Sending: %s (x%u)", function, repeat);

  int ret = ir_service_send_command(function, repeat);
  if (ret < 0) {
    shell_error(shell, "Send failed: %d", ret);
    return ret;
  }

  shell_print(shell, "Sent successfully");
  return 0;
}

/* 接收命令 */
static int cmd_receive(const struct shell *shell, size_t argc, char **argv) {
  uint32_t duration = argc > 1 ? atoi(argv[1]) : 10;

  shell_print(shell, "Receiving for %u seconds...", duration);

  int ret = ir_service_start_receive(rx_callback, NULL);
  if (ret < 0) {
    shell_error(shell, "Failed to start: %d", ret);
    return ret;
  }

  k_sleep(K_SECONDS(duration));
  ir_service_stop_receive();

  shell_print(shell, "Receive completed");
  return 0;
}

/* 列出功能 */
static int cmd_list(const struct shell *shell, size_t argc, char **argv) {
  char buf[1024];

  int ret = ir_service_list_functions(buf, sizeof(buf));
  if (ret < 0) {
    shell_error(shell, "No database loaded");
    return ret;
  }

  shell_print(shell, "%s", buf);
  return 0;
}

/* 从文件加载（需要文件系统支持） */
#ifdef CONFIG_FILE_SYSTEM
static int cmd_load_file(const struct shell *shell, size_t argc, char **argv) {
  if (argc < 4) {
    shell_error(
        shell,
        "Usage: ir loadfile <manufacturer> <device_type> <device,subdevice>");
    shell_error(shell, "Example: ir loadfile Samsung TV 7,7");
    return -EINVAL;
  }

  const char *manufacturer = argv[1];
  const char *device_type = argv[2];

  uint8_t device, subdevice;
  if (sscanf(argv[3], "%hhu,%hhu", &device, &subdevice) != 2) {
    shell_error(shell, "Invalid device,subdevice format");
    return -EINVAL;
  }

  ir_service_config_t config = {.load_method = IRDB_LOAD_FILESYSTEM,
                                .device = device,
                                .subdevice = subdevice};

  strncpy(config.manufacturer, manufacturer, sizeof(config.manufacturer) - 1);
  strncpy(config.device_type, device_type, sizeof(config.device_type) - 1);

  shell_print(shell, "Loading: %s/%s/%u,%u.csv", manufacturer, device_type,
              device, subdevice);

  int ret = ir_service_load_remote(&config);
  if (ret < 0) {
    shell_error(shell, "Load failed: %d", ret);
    return ret;
  }

  shell_print(shell, "Loaded successfully");
  return 0;
}
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(
    ir_cmds, SHELL_CMD(load, NULL, "Load embedded database", cmd_load),
    SHELL_CMD(send, NULL, "Send IR command", cmd_send),
    SHELL_CMD(receive, NULL, "Receive IR signals", cmd_receive),
    SHELL_CMD(list, NULL, "List functions", cmd_list),
#ifdef CONFIG_FILE_SYSTEM
    SHELL_CMD(loadfile, NULL, "Load from file", cmd_load_file),
#endif
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(ir, &ir_cmds, "IR remote control (IRDB)", NULL);

#endif /* CONFIG_SHELL */