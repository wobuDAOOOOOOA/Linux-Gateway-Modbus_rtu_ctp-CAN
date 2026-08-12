#include "data_cache.h"
#include "mqtt_huawei.h"
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include "gateway.h"
static data_cache_t g_cache;
static pthread_mutex_t g_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

// ====================== 内部函数 ======================
static void cache_push_record(data_record_t *rec)
{
    pthread_mutex_lock(&g_cache_mutex);
    
    if (g_cache.count >= CACHE_SIZE) {
        g_cache.tail = (g_cache.tail + 1) % CACHE_SIZE;
        g_cache.count--;
        g_cache.full = true;
        printf("【缓存】缓冲区已满，覆盖最旧数据\n");
    }
    
    int idx = g_cache.head;
    memcpy(&g_cache.records[idx], rec, sizeof(data_record_t));    //这里还是真正的写入，所有在这个函数内部加锁
    
    g_cache.head = (g_cache.head + 1) % CACHE_SIZE;
    g_cache.count++;
    
    pthread_mutex_unlock(&g_cache_mutex);
    printf("【缓存】当前缓存数量: %d\n", g_cache.count);
}

// ====================== 公开函数 ======================
void data_cache_init(void)
{
    memset(&g_cache, 0, sizeof(g_cache));
    printf("【缓存】环形缓冲区初始化完成，容量: %d\n", CACHE_SIZE);
}

// 缓存遥测数据（温度+湿度+大气压）
int data_cache_push_telemetry(float temp, float humi, float press)
{
    data_record_t rec;
    memset(&rec, 0, sizeof(rec));
    
    rec.type = CACHE_TYPE_TELEMETRY;   
    rec.timestamp = time(NULL);
    rec.temperature = temp;
    rec.humidity = humi;
    rec.pressure = press;
    
    cache_push_record(&rec);   
    return 0;
}

// 缓存RTU告警
int data_cache_push_alarm_rtu(const char *type, const char *module, const char *msg)
{
    data_record_t rec;
    memset(&rec, 0, sizeof(rec));
    
    rec.type = CACHE_TYPE_ALARM_RTU;
    rec.timestamp = time(NULL);
    snprintf(rec.alarm_type, sizeof(rec.alarm_type), "%s", type);
    snprintf(rec.alarm_module, sizeof(rec.alarm_module), "%s", module);
    snprintf(rec.alarm_msg, sizeof(rec.alarm_msg), "%s", msg);
    
    cache_push_record(&rec);
    return 0;
}

// 缓存TCP告警
int data_cache_push_alarm_tcp(const char *type, const char *module, const char *msg)
{
    data_record_t rec;
    memset(&rec, 0, sizeof(rec));
    
    rec.type = CACHE_TYPE_ALARM_TCP;
    rec.timestamp = time(NULL);
    snprintf(rec.alarm_type, sizeof(rec.alarm_type), "%s", type);
    snprintf(rec.alarm_module, sizeof(rec.alarm_module), "%s", module);
    snprintf(rec.alarm_msg, sizeof(rec.alarm_msg), "%s", msg);
    
    cache_push_record(&rec);
    return 0;
}

// 补传所有缓存数据
void data_cache_flush(void)
{
    pthread_mutex_lock(&g_cache_mutex);
    
    if (g_cache.count == 0) {
        pthread_mutex_unlock(&g_cache_mutex);
        return;
    }
    
    printf("【缓存】开始补传 %d 条缓存数据...\n", g_cache.count);
    
    while (g_cache.count > 0) {
        data_record_t *rec = &g_cache.records[g_cache.tail];
        for (int i = 0; i < mgr.tcp_device_count; i++) {

        switch (rec->type) {
            case CACHE_TYPE_TELEMETRY:
                mqtt_publish_data(rec->temperature, rec->humidity, rec->pressure);
                break;
            case CACHE_TYPE_ALARM_RTU:
                mqtt_publish_alarm(rec->alarm_type,i, rec->alarm_module, rec->alarm_type,rec->alarm_msg);
                break;
            case CACHE_TYPE_ALARM_TCP:
                mqtt_publish_alarm(rec->alarm_type,i, rec->alarm_module, rec->alarm_type,rec->alarm_msg);
                break;
            default:
                break;
        }
    }
        
        g_cache.tail = (g_cache.tail + 1) % CACHE_SIZE;
        g_cache.count--;
    }
    
    g_cache.full = false;
    pthread_mutex_unlock(&g_cache_mutex);
    
    printf("【缓存】所有缓存数据已补传完成\n");
}

int data_cache_get_count(void)
{
    pthread_mutex_lock(&g_cache_mutex);
    int count = g_cache.count;
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
    bool full = g_cache.full;
    pthread_mutex_unlock(&g_cache_mutex);
    return full;
}

