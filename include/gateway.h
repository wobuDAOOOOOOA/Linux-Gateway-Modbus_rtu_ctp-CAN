#ifndef GATEWAY_H
#define GATEWAY_H

/*************************************************
* 工业网关全局资源管理头文件
* 功能：统一所有线程、句柄、状态、业务变量
* 存放：结构体完整定义 + 全局变量声明
* 工程规范：所有模块(mqtt/rtu/main)统一包含此头文件
**************************************************/
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <modbus/modbus.h>
#include <signal.h>        // for sig_atomic_t

#define MAX_TCP_DEVICES 4
#define MAX_RTU_DEVICES 4

// CAN ID 分配规则（标准帧 11位，范围 0x000 ~ 0x7FF）
#define CAN_ID_BASE_RTU    0x100   // RTU设备: 0x100 + 设备下标
#define CAN_ID_BASE_TCP    0x200   // TCP设备: 0x200 + 设备下标
#define CAN_ID_BASE_ALARM  0x300   // 告警:   0x300 + 设备下标

// ====================== 工业级网关资源管理器 核心结构体 ======================
typedef struct {
    char ip[64];
    int  port;
    int  slave_id;
    int  timeout_ms;
    int   read_count;
    int  read_addr;
    uint16_t regs[32];
    modbus_t *ctx;
    int last_reported_status;
    int status;
    char alarm_msg[128];
    time_t tcp_fail_time;
    int collect_enable;
    int last_collect_state;
} tcp_device_config_t;

typedef struct {
    // ---------- 配置参数 ----------
    char port[64];
    int  baudrate;
    int  slave_id;
    int  read_addr;
    int  read_count;

    // ---------- 运行时状态 ----------
    modbus_t *ctx;
    uint16_t regs[32];

    int  status;                 // 0=正常, 1=采集关闭, 2=离线故障
    int  last_reported_status;
    int  last_collect_state;
    char alarm_msg[128];
    time_t fail_time;

    int  collect_enable;         // 1=运行, 0=停止

    char name[32];
} rtu_device_t;

typedef enum {
    MQTT_DISCONNECTED,
    MQTT_CONNECTED,
    MQTT_FLUSHING,
} mqtt_state_t;

typedef struct {
    // 线程句柄
    pthread_t threads[15];

    // 同步互斥锁
    pthread_mutex_t data_mutex;
    pthread_mutex_t bus_mutex;
    pthread_mutex_t rtu_bus_mutex;

    pthread_rwlock_t config_lock;          // ★ 新增：读写锁保护配置数组

    // 全局运行状态机
    uint8_t running;
    volatile sig_atomic_t collect_stop;    // ★ 新增：控制采集线程启停

    // 业务数据缓存
    uint16_t regs[2];
    unsigned short rtu_data[64];
    float latest_temperature;
    float latest_humidity;
    float press;

    _Bool mqtt_connect_states;

    rtu_device_t rtu_devices[MAX_RTU_DEVICES];
    int rtu_device_count;

    tcp_device_config_t tcp_devices[MAX_TCP_DEVICES];
    int tcp_device_count;

    mqtt_state_t mqtt_state;

} gateway_manager_t;

// 全局唯一网关实例
extern gateway_manager_t mgr;
extern volatile sig_atomic_t g_config_reload_flag;  // ★ 新增：热加载标志

#endif