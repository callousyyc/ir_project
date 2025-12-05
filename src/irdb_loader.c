/**
 * @file irdb_loader.c
 * @brief IRDB数据加载器实现
 */

#include "irdb_loader.h"
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_FILE_SYSTEM
#include <zephyr/fs/fs.h>
#endif

#ifdef CONFIG_HTTP_CLIENT
#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>
#endif

LOG_MODULE_REGISTER(irdb_loader, LOG_LEVEL_INF);

/* IRDB CDN基础URL */
#define IRDB_CDN_BASE "https://cdn.jsdelivr.net/gh/probonopd/irdb@master/codes"

/* 缓存数组 */
static irdb_cache_entry_t cache[IRDB_CACHE_SIZE];
static K_MUTEX_DEFINE(cache_mutex);

/* 构建IRDB路径 */
void irdb_build_path(char *path_out, size_t path_size, const char *manufacturer,
                     const char *device_type, uint8_t device,
                     uint8_t subdevice) {
  snprintf(path_out, path_size, "%s/%s/%u,%u.csv", manufacturer, device_type,
           device, subdevice);
}

/* 从嵌入式数据加载 */
int irdb_load_embedded(irdb_database_t *db, const char *csv_data) {
  if (!db || !csv_data) {
    return -EINVAL;
  }

  return irdb_parse_csv(csv_data, db);
}

#ifdef CONFIG_FILE_SYSTEM
/* 从文件系统加载 */
int irdb_load_from_file(irdb_database_t *db, const char *filepath) {
  if (!db || !filepath) {
    return -EINVAL;
  }

  struct fs_file_t file;
  fs_file_t_init(&file);

  int ret = fs_open(&file, filepath, FS_O_READ);
  if (ret < 0) {
    LOG_ERR("Failed to open file %s: %d", filepath, ret);
    return ret;
  }

  /* 获取文件大小 */
  struct fs_dirent entry;
  ret = fs_stat(filepath, &entry);
  if (ret < 0) {
    fs_close(&file);
    return ret;
  }

  /* 分配缓冲区 */
  char *buffer = k_malloc(entry.size + 1);
  if (!buffer) {
    fs_close(&file);
    return -ENOMEM;
  }

  /* 读取文件 */
  ssize_t bytes_read = fs_read(&file, buffer, entry.size);
  fs_close(&file);

  if (bytes_read < 0) {
    k_free(buffer);
    return bytes_read;
  }

  buffer[bytes_read] = '\0';

  /* 解析CSV */
  ret = irdb_parse_csv(buffer, db);
  k_free(buffer);

  if (ret == 0) {
    LOG_INF("Loaded IRDB from file: %s", filepath);
  }

  return ret;
}
#else
int irdb_load_from_file(irdb_database_t *db, const char *filepath) {
  LOG_ERR("File system support not enabled");
  return -ENOTSUP;
}
#endif

#ifdef CONFIG_HTTP_CLIENT
/* HTTP响应缓冲区 */
#define HTTP_RECV_BUF_SIZE 4096
static char http_recv_buf[HTTP_RECV_BUF_SIZE];
static size_t http_data_len;

/* HTTP响应回调 */
static void http_response_cb(struct http_response *rsp,
                             enum http_final_call final_data, void *user_data) {
  if (rsp->data_len > 0) {
    size_t copy_len =
        MIN(rsp->data_len, HTTP_RECV_BUF_SIZE - http_data_len - 1);
    memcpy(http_recv_buf + http_data_len, rsp->recv_buf, copy_len);
    http_data_len += copy_len;
  }
}

/* 从HTTP加载 */
int irdb_load_from_http(irdb_database_t *db, const char *manufacturer,
                        const char *device_type, uint8_t device,
                        uint8_t subdevice) {
  if (!db || !manufacturer || !device_type) {
    return -EINVAL;
  }

  /* 构建URL */
  char url[256];
  snprintf(url, sizeof(url), "%s/%s/%s/%u,%u.csv", IRDB_CDN_BASE, manufacturer,
           device_type, device, subdevice);

  LOG_INF("Fetching: %s", url);

  /* 初始化HTTP请求 */
  struct http_request req;
  memset(&req, 0, sizeof(req));

  req.method = HTTP_GET;
  req.url = url;
  req.protocol = "HTTP/1.1";
  req.response = http_response_cb;
  req.recv_buf = http_recv_buf;
  req.recv_buf_len = sizeof(http_recv_buf);

  http_data_len = 0;

  /* 发送HTTP请求 */
  int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock < 0) {
    LOG_ERR("Failed to create socket");
    return -errno;
  }

  /* 解析主机名和端口 */
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(443); // HTTPS

  /* 这里需要DNS解析，简化处理 */
  /* 实际项目中需要使用getaddrinfo或DNS查询 */

  /* 连接服务器 */
  int ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
  if (ret < 0) {
    close(sock);
    LOG_ERR("Connection failed");
    return -errno;
  }

  /* 发送HTTP请求 */
  ret = http_client_req(sock, &req, 5000, NULL);
  close(sock);

  if (ret < 0) {
    LOG_ERR("HTTP request failed: %d", ret);
    return ret;
  }

  /* 解析响应 */
  http_recv_buf[http_data_len] = '\0';
  ret = irdb_parse_csv(http_recv_buf, db);

  if (ret == 0) {
    LOG_INF("Loaded IRDB from HTTP: %s", url);
  }

  return ret;
}
#else
int irdb_load_from_http(irdb_database_t *db, const char *manufacturer,
                        const char *device_type, uint8_t device,
                        uint8_t subdevice) {
  LOG_ERR("HTTP client support not enabled");
  return -ENOTSUP;
}
#endif

/* 缓存查找 */
int irdb_cache_get(const char *path, irdb_database_t **db_out) {
  if (!path || !db_out) {
    return -EINVAL;
  }

  k_mutex_lock(&cache_mutex, K_FOREVER);

  for (int i = 0; i < IRDB_CACHE_SIZE; i++) {
    if (cache[i].valid && strcmp(cache[i].path, path) == 0) {
      cache[i].last_access = k_uptime_get_32();
      *db_out = &cache[i].database;
      k_mutex_unlock(&cache_mutex);
      LOG_DBG("Cache hit: %s", path);
      return 0;
    }
  }

  k_mutex_unlock(&cache_mutex);
  return -ENOENT;
}

/* 缓存添加 */
int irdb_cache_put(const char *path, const irdb_database_t *db) {
  if (!path || !db) {
    return -EINVAL;
  }

  k_mutex_lock(&cache_mutex, K_FOREVER);

  /* 查找空闲或最久未使用的条目 */
  int target_idx = 0;
  uint32_t oldest_time = UINT32_MAX;

  for (int i = 0; i < IRDB_CACHE_SIZE; i++) {
    if (!cache[i].valid) {
      target_idx = i;
      break;
    }

    if (cache[i].last_access < oldest_time) {
      oldest_time = cache[i].last_access;
      target_idx = i;
    }
  }

  /* 清理旧条目 */
  if (cache[target_idx].valid) {
    irdb_free_database(&cache[target_idx].database);
  }

  /* 复制新数据 */
  strncpy(cache[target_idx].path, path, sizeof(cache[target_idx].path) - 1);

  /* 深拷贝数据库 */
  cache[target_idx].database.entry_count = db->entry_count;
  cache[target_idx].database.entries =
      k_malloc(db->entry_count * sizeof(irdb_entry_t));

  if (!cache[target_idx].database.entries) {
    k_mutex_unlock(&cache_mutex);
    return -ENOMEM;
  }

  memcpy(cache[target_idx].database.entries, db->entries,
         db->entry_count * sizeof(irdb_entry_t));

  strncpy(cache[target_idx].database.manufacturer, db->manufacturer,
          sizeof(cache[target_idx].database.manufacturer) - 1);
  strncpy(cache[target_idx].database.device_type, db->device_type,
          sizeof(cache[target_idx].database.device_type) - 1);

  cache[target_idx].last_access = k_uptime_get_32();
  cache[target_idx].valid = true;

  k_mutex_unlock(&cache_mutex);
  LOG_DBG("Cache stored: %s", path);
  return 0;
}

/* 清空缓存 */
void irdb_cache_clear(void) {
  k_mutex_lock(&cache_mutex, K_FOREVER);

  for (int i = 0; i < IRDB_CACHE_SIZE; i++) {
    if (cache[i].valid) {
      irdb_free_database(&cache[i].database);
      cache[i].valid = false;
    }
  }

  k_mutex_unlock(&cache_mutex);
  LOG_INF("Cache cleared");
}