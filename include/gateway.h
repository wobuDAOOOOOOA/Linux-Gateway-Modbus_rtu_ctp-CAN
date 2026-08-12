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
    int  timeout_ms;      // 可选，超时时间
    uint16_t regs[32];
    modbus_t *ctx;
    int last_reported_status;
    int status ;
    char alarm_msg[128];
    time_t tcp_fail_time;
    int collect_enable;
    int last_collect_state
    
} tcp_device_config_t;
// gateway.h
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
    int  last_reported_status;   // 上次上报的状态
    int  last_collect_state;
    char alarm_msg[128];
    time_t fail_time;

    int  collect_enable;         // 1=运行, 0=停止

    char name[32];               // 设备名称
} rtu_device_t;


typedef struct {
    // 线程句柄
    pthread_t threads[15];

    // // 通信句柄
    // modbus_t *rtu_ctx;
    // modbus_t *tcp_ctx;    旧框架，删除

    // 同步互斥锁
    pthread_mutex_t data_mutex;
    pthread_mutex_t bus_mutex;
    pthread_mutex_t read_mutex;
    // 全局运行状态机
    uint8_t running;

    // 业务数据缓存
    uint16_t regs[2];
    unsigned short rtu_data[64];
    float latest_temperature;
    float latest_humidity;
    float press;
    // // 重连计数
    // int tcp_retry;   //重连数放在modbus xx.c中define定义（删除）
    // int rtu_retry;
    
    // // rtu云端控制线程采集 1=开启采集  0=关闭采集
    // int rtu_collect_enable;        //由设备就结构体管理（删除）
    // int tcp_collect_enable;

    //   // ★★★ 新增：各模块状态（供MQTT线程读取上报） ★★★
    // int rtu_status;      // 0=正常, 1=采集关闭, 2=离线故障          //由设备就结构体管理（删除）
    // int tcp_status;      // 0=正常, 1=采集关闭, 2=离线故障
    // int can_status;      // 0=正常, 1=故障

       _Bool mqtt_connect_states;     //用于接收mqtt_connect（）;的返回值（删除）
    
    // time_t rtu_fail_time;
    // time_t tcp_fail_time;
    // time_t can_fail_time;     //由设备就结构体管理（删除）

    // char rtu_alarm_msg[128]; 
    // char tcp_alarm_msg[128]; //由设备就结构体管理（删除）
    // char can_alarm_msg[128];
    rtu_device_t rtu_devices[MAX_RTU_DEVICES];      //管理设备结构体数组，成员下标变化，就指向变化的那个设备结构体。
   int rtu_device_count;
 
    tcp_device_config_t tcp_devices[MAX_TCP_DEVICES];
    int tcp_device_count;

} gateway_manager_t;

// 全局唯一网关实例（所有文件共用）
// 写在这里：所有include本头文件的.c都能识别、不用重复extern
extern gateway_manager_t mgr;

#endif
