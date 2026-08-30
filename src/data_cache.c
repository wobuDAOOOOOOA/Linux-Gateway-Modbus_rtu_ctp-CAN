#include "data_cache.h"
#include "mqtt_huawei.h"
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <sqlite3.h>
#include "gateway.h"
#include "config.h"

static int g_flushing = 0;

static data_cache_t g_cache;
static pthread_mutex_t g_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

// ====================== SQLite 内部操作 ======================

static int db_exec(sqlite3 *db, const char *sql)
{
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        printf("【缓存】SQL执行失败: %s, 错误: %s\n", sql, err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    return 0;
}

static int db_insert_record(sqlite3 *db, data_record_t *rec)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "INSERT INTO cache (type, timestamp, temperature, humidity, pressure, "
        "json_payload, alarm_type, alarm_module, alarm_msg) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        printf("【缓存】插入准备失败: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, rec->type);
    sqlite3_bind_int64(stmt, 2, rec->timestamp);
    sqlite3_bind_double(stmt, 3, rec->temperature);
    sqlite3_bind_double(stmt, 4, rec->humidity);
    sqlite3_bind_double(stmt, 5, rec->pressure);
    sqlite3_bind_text(stmt, 6, rec->json_payload, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, rec->alarm_type, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, rec->alarm_module, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, rec->alarm_msg, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        printf("【缓存】插入失败: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    return 0;
}

static int db_get_count(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT COUNT(*) FROM cache;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

static int db_trim_oldest(sqlite3 *db, int max_records)
{
    int count = db_get_count(db);
    if (count <= max_records) {
        return 0;
    }

    int to_delete = count - max_records;
    char sql[256];
    snprintf(sql, sizeof(sql),
             "DELETE FROM cache WHERE id IN "
             "(SELECT id FROM cache ORDER BY id ASC LIMIT %d);",
             to_delete);
    return db_exec(db, sql);
}

// ====================== 公开函数 ======================

void data_cache_init(void)
{
    memset(&g_cache, 0, sizeof(g_cache));
    g_cache.max_records = CACHE_MAX_RECORDS;

    int rc = sqlite3_open(CACHE_DB_PATH, &g_cache.db);
    if (rc != SQLITE_OK) {
        printf("【缓存】打开数据库失败: %s\n", sqlite3_errmsg(g_cache.db));
        return;
    }

    const char *sql =
        "CREATE TABLE IF NOT EXISTS cache ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "type INTEGER,"
        "timestamp INTEGER,"
        "temperature REAL,"
        "humidity REAL,"
        "pressure REAL,"
        "json_payload TEXT,"
        "alarm_type TEXT,"
        "alarm_module TEXT,"
        "alarm_msg TEXT"
        ");";

    db_exec(g_cache.db, sql);
    printf("【缓存】SQLite缓存初始化完成，最大容量: %d\n", g_cache.max_records);
}

// int data_cache_push_telemetry(float temp, float humi, float press)
// {
//     data_record_t rec;
//     memset(&rec, 0, sizeof(rec));

//     rec.type = CACHE_TYPE_TELEMETRY;
//     rec.timestamp = time(NULL);
//     rec.temperature = temp;
//     rec.humidity = humi;
//     rec.pressure = press;

//     pthread_mutex_lock(&g_cache_mutex);
//     db_insert_record(g_cache.db, &rec);
//     db_trim_oldest(g_cache.db, g_cache.max_records);
//     pthread_mutex_unlock(&g_cache_mutex);

//     printf("【缓存】遥测数据已入库，当前缓存: %d\n", db_get_count(g_cache.db));
//     return 0;
// }

int data_cache_push_telemetry_json(const char *json_payload)
{
    data_record_t rec;
    memset(&rec, 0, sizeof(rec));

    rec.type = CACHE_TYPE_TELEMETRY;
    rec.timestamp = time(NULL);
    snprintf(rec.json_payload, sizeof(rec.json_payload), "%s", json_payload);

    pthread_mutex_lock(&g_cache_mutex);
    db_insert_record(g_cache.db, &rec);
    db_trim_oldest(g_cache.db, g_cache.max_records);
    pthread_mutex_unlock(&g_cache_mutex);

    printf("【缓存】JSON遥测数据已入库，当前缓存: %d\n", db_get_count(g_cache.db));
    return 0;
}

int data_cache_push_alarm_rtu(const char *type, const int device_ptr, const char *module, const char *msg)
{
    data_record_t rec;
    memset(&rec, 0, sizeof(rec));

    rec.type = CACHE_TYPE_ALARM_RTU;
    rec.timestamp = time(NULL);
    snprintf(rec.alarm_type, sizeof(rec.alarm_type), "%s", type);
    snprintf(rec.alarm_module, sizeof(rec.alarm_module), "%s", module);
    snprintf(rec.alarm_msg, sizeof(rec.alarm_msg), "%s", msg);

    pthread_mutex_lock(&g_cache_mutex);
    db_insert_record(g_cache.db, &rec);
    db_trim_oldest(g_cache.db, g_cache.max_records);
    pthread_mutex_unlock(&g_cache_mutex);

    return 0;
}

int data_cache_push_alarm_tcp(const char *type, const char *module, const char *msg)
{
    data_record_t rec;
    memset(&rec, 0, sizeof(rec));

    rec.type = CACHE_TYPE_ALARM_TCP;
    rec.timestamp = time(NULL);
    snprintf(rec.alarm_type, sizeof(rec.alarm_type), "%s", type);
    snprintf(rec.alarm_module, sizeof(rec.alarm_module), "%s", module);
    snprintf(rec.alarm_msg, sizeof(rec.alarm_msg), "%s", msg);

    pthread_mutex_lock(&g_cache_mutex);
    db_insert_record(g_cache.db, &rec);
    db_trim_oldest(g_cache.db, g_cache.max_records);
    pthread_mutex_unlock(&g_cache_mutex);

    return 0;
}

void data_cache_flush(void)
{
        g_flushing = 1;

    pthread_mutex_lock(&g_cache_mutex);

    int count = db_get_count(g_cache.db);
    if (count == 0) {
        pthread_mutex_unlock(&g_cache_mutex);
        return;
    }

    printf("【缓存】开始补传 %d 条缓存数据...\n", count);

    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "SELECT type, timestamp, temperature, humidity, pressure, "
        "json_payload, alarm_type, alarm_module, alarm_msg "
        "FROM cache ORDER BY id ASC;";

    if (sqlite3_prepare_v2(g_cache.db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("【缓存】查询失败: %s\n", sqlite3_errmsg(g_cache.db));
        pthread_mutex_unlock(&g_cache_mutex);
        return;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        data_record_t rec;
        memset(&rec, 0, sizeof(rec));

        rec.type = sqlite3_column_int(stmt, 0);
        rec.timestamp = sqlite3_column_int64(stmt, 1);
        rec.temperature = sqlite3_column_double(stmt, 2);
        rec.humidity = sqlite3_column_double(stmt, 3);
        rec.pressure = sqlite3_column_double(stmt, 4);
        snprintf(rec.json_payload, sizeof(rec.json_payload), "%s", sqlite3_column_text(stmt, 5));
        snprintf(rec.alarm_type, sizeof(rec.alarm_type), "%s", sqlite3_column_text(stmt, 6));
        snprintf(rec.alarm_module, sizeof(rec.alarm_module), "%s", sqlite3_column_text(stmt, 7));
        snprintf(rec.alarm_msg, sizeof(rec.alarm_msg), "%s", sqlite3_column_text(stmt, 8));

        switch (rec.type) {
            case CACHE_TYPE_TELEMETRY:
                if (g_mosq) {
                    mosquitto_publish(g_mosq, NULL, cfg.mqtt_topic,
                                      strlen(rec.json_payload), rec.json_payload, 1, false);
                }
                break;
            case CACHE_TYPE_ALARM_RTU:
                mqtt_publish_alarm(rec.alarm_type, 0, rec.alarm_module, rec.alarm_type, rec.alarm_msg);
                break;
            case CACHE_TYPE_ALARM_TCP:
                mqtt_publish_alarm(rec.alarm_type, 0, rec.alarm_module, rec.alarm_type, rec.alarm_msg);
                break;
            default:
                break;
        }
    }

    sqlite3_finalize(stmt);

    db_exec(g_cache.db, "DELETE FROM cache;");

    pthread_mutex_unlock(&g_cache_mutex);
    g_flushing = 0;

    printf("【缓存】所有缓存数据已补传完成\n");
}

int data_cache_get_count(void)
{
    pthread_mutex_lock(&g_cache_mutex);
    int count = db_get_count(g_cache.db);
    pthread_mutex_unlock(&g_cache_mutex);
    return count;
}

bool data_cache_is_empty(void)
{
    return data_cache_get_count() == 0;
}

bool data_cache_is_full(void)
{
    pthread_mutex_lock(&g_cache_mutex);
    int count = db_get_count(g_cache.db);
    pthread_mutex_unlock(&g_cache_mutex);
    return count >= g_cache.max_records;
}
int data_cache_is_flushing(void)
{
    return g_flushing;
}