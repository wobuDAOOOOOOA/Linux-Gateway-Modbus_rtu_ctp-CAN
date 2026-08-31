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
#include <signal.h>

#define MAX_TCP_DEVICES 4
#define MAX_RTU_DEVICES 4
#define MAX_CAN_DEVICES 4

// ====================== TCP 设备结构体 ======================
typedef struct {
    char ip[64];
    int  port;
    int  slave_id;
    int  timeout_ms;
    int  read_addr;
    int  read_count;
    uint16_t regs[32];
    modbus_t *ctx;

    int collect_enable;
    int last_collect_state;
    int status;                 // 0=正常, 1=采集关闭, 2=离线故障
    int last_reported_status;
    char alarm_msg[128];
    time_t tcp_fail_time;
} tcp_device_config_t;

// ====================== RTU 设备结构体 ======================
typedef struct {
    char port[64];
    int  baudrate;
    int  slave_id;
    int  read_addr;
    int  read_count;

    modbus_t *ctx;
    uint16_t regs[32];

    int collect_enable;
    int last_collect_state;
    int status;
    int last_reported_status;
    char alarm_msg[128];
    time_t fail_time;

    char name[32];
} rtu_device_t;

// ====================== CAN 设备结构体 ======================
typedef struct {
    uint32_t can_id;
    uint8_t data[8];
    uint8_t dlc;
} can_device_t;

// ====================== MQTT 状态机 ======================
typedef enum {
    MQTT_DISCONNECTED,
    MQTT_CONNECTED,
    MQTT_FLUSHING,
} mqtt_state_t;

// ====================== 全局网关管理器 ======================
typedef struct {
    pthread_t threads[15];

    pthread_mutex_t data_mutex;
    pthread_mutex_t bus_mutex;
    pthread_mutex_t rtu_bus_mutex;

    pthread_rwlock_t config_lock;

    uint8_t running;
    volatile sig_atomic_t collect_stop;

    float latest_temperature;
    float latest_humidity;
    float press;

    _Bool mqtt_connect_states;
    mqtt_state_t mqtt_state;

    rtu_device_t rtu_devices[MAX_RTU_DEVICES];
    int rtu_device_count;

    tcp_device_config_t tcp_devices[MAX_TCP_DEVICES];
    int tcp_device_count;

    can_device_t can_devices[MAX_CAN_DEVICES];
    int can_device_count;

} gateway_manager_t;

extern gateway_manager_t mgr;
extern volatile sig_atomic_t g_config_reload_flag;

#endif