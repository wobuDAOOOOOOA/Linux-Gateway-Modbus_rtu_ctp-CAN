#ifndef DATA_CACHE_H
#define DATA_CACHE_H

#include <stdbool.h>
#include <time.h>
#include <sqlite3.h>

#define CACHE_DB_PATH "/root/gateway_cache.db"
#define CACHE_MAX_RECORDS 500

typedef enum {
    CACHE_TYPE_TELEMETRY = 0,
    CACHE_TYPE_ALARM_RTU,
    CACHE_TYPE_ALARM_TCP
} cache_data_type_t;

typedef struct {
    cache_data_type_t type;
    time_t timestamp;
    float temperature;
    float humidity;
    float pressure;
    char json_payload[1024];
    char alarm_type[32];
    char alarm_module[32];
    char alarm_msg[128];
} data_record_t;

typedef struct {
    sqlite3 *db;
    int max_records;
} data_cache_t;

void data_cache_init(void);
//int data_cache_push_telemetry(float temp, float humi, float press);
int data_cache_push_telemetry_json(const char *json_payload);
int data_cache_push_alarm_rtu(const char *type, const int device_ptr, const char *module, const char *msg);
int data_cache_push_alarm_tcp(const char *type, const char *module, const char *msg);
void data_cache_flush(void);
int data_cache_get_count(void);
bool data_cache_is_empty(void);
bool data_cache_is_full(void);

#endif