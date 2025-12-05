/**
 * @file irdb_loader.h
 * @brief IRDB数据加载器 - 支持多种加载方式
 */

#ifndef IRDB_LOADER_H
#define IRDB_LOADER_H

#include "irdb_protocol.h"
#include <stddef.h>


/* 加载方式 */
typedef enum {
  IRDB_LOAD_EMBEDDED,   // 嵌入式存储（编译时包含）
  IRDB_LOAD_FILESYSTEM, // 从文件系统加载
  IRDB_LOAD_HTTP,       // 从HTTP服务器加载（需要网络）
  IRDB_LOAD_EXTERNAL,   // 外部存储（SD卡等）
} irdb_load_method_t;

/* IRDB路径构建 */
void irdb_build_path(char *path_out, size_t path_size, const char *manufacturer,
                     const char *device_type, uint8_t device,
                     uint8_t subdevice);

/* 从嵌入式数据加载 */
int irdb_load_embedded(irdb_database_t *db, const char *csv_data);

/* 从文件系统加载 */
int irdb_load_from_file(irdb_database_t *db, const char *filepath);

/* 从HTTP加载（需要网络支持） */
int irdb_load_from_http(irdb_database_t *db, const char *manufacturer,
                        const char *device_type, uint8_t device,
                        uint8_t subdevice);

/* 缓存管理 */
typedef struct {
  char path[128];
  irdb_database_t database;
  uint32_t last_access;
  bool valid;
} irdb_cache_entry_t;

#define IRDB_CACHE_SIZE 4

int irdb_cache_get(const char *path, irdb_database_t **db_out);
int irdb_cache_put(const char *path, const irdb_database_t *db);
void irdb_cache_clear(void);

#endif /* IRDB_LOADER_H */