#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <linux/can.h>
#include <sys/socket.h>
#include"gateway.h"
#include"modbus_tcp.h"
#include"modbus_rtu.h"
#include"can.h"          
#include"mqtt_huawei.h" 
#include"log.h"      //日志分级
#include"config.h"  //配置文件外置
#include"relay.h"  //下发测试modbus_tcp继电器模块
#include"bmp280.h" //bmp280 测试用，轮询在mqtt线程，删掉不影响
#include"data_cache.h" //本地环形缓冲区缓存 //掉电丢失数据
#define ture 1

  // 全局唯一实体定义（全工程仅此一处）
 gateway_manager_t mgr;
// ====================== 资源初始化函数 ======================
static void gateway_manager_init(gateway_manager_t *mgr)
{
    memset(mgr, 0, sizeof(gateway_manager_t));

    // 初始化锁
    pthread_mutex_init(&mgr->data_mutex, NULL);
    pthread_mutex_init(&mgr->bus_mutex, NULL);
   // pthread_mutex_init(&mgr->read_mutex, NULL);

       // 开启运行开关
    mgr->running = ture;
    // mgr->rtu_collect_enable=1;
    // mgr->tcp_collect_enable=1;
    // 初始化默认寄存器数据
    // mgr->regs[0] = 1;
    // mgr->regs[1] = 2;
}
// ====================== 退出清理函数 ======================

void gateway_cleanup(void)
{
    LOG_INFO("执行网关全资源释放...");

    // ===== 1. 释放所有 TCP 设备资源 =====
    for (int i = 0; i < mgr.tcp_device_count; i++) {
        if (mgr.tcp_devices[i].ctx != NULL) {
            modbus_close(mgr.tcp_devices[i].ctx);
            modbus_free(mgr.tcp_devices[i].ctx);
            mgr.tcp_devices[i].ctx = NULL;
            LOG_DEBUG("TCP设备%d: 已释放Modbus上下文", i);
        }
    }

    // ===== 2. 释放所有 RTU 设备资源 =====
    for (int i = 0; i < mgr.rtu_device_count; i++) {
        if (mgr.rtu_devices[i].ctx != NULL) {
            modbus_close(mgr.rtu_devices[i].ctx);
            modbus_free(mgr.rtu_devices[i].ctx);
            mgr.rtu_devices[i].ctx = NULL;
            LOG_DEBUG("RTU设备%d: 已释放Modbus上下文", i);
        }
    }

    // ===== 3. 销毁互斥锁 =====
    pthread_mutex_destroy(&mgr.data_mutex);
    pthread_mutex_destroy(&mgr.bus_mutex);

    LOG_INFO("所有资源释放完成！");
}
void init_tcp_devices(void)
{
    // 1. 先把整个数组清空（防止残留垃圾数据）
    //    相当于：把 tcp_devices[0] ~ tcp_devices[3] 全部置零
    memset(mgr.tcp_devices, 0, sizeof(mgr.tcp_devices));
   
    // 2. 从配置文件 cfg 里读取数据，填充数组
    //    比如你配置了 tcp_enable[0]=1, tcp_ip[0]="192.168.1.100" ...
    int count = 0;

    for (int i = 0; i < MAX_TCP_DEVICES; i++) {
        // 如果配置里这个设备是禁用状态，就跳过
        if (cfg.tcp_enable[i] == 0) {
            continue;
        }

        
        // tcp_devices[count] 就是第 count 个设备
        // 用 & 取地址，用 -> 访问结构体成员
        tcp_device_config_t *dev = &mgr.tcp_devices[count];

        // 3. 把配置拷贝到数组元素里
        strcpy(dev->ip, cfg.tcp_ip[i]);
        dev->port = cfg.tcp_port[i];
        dev->slave_id = cfg.tcp_slave_id[i];
        dev->timeout_ms = 500;
        dev->ctx = NULL;      // 连接句柄先设为空，读函数会自动创建

        // 4. 计数加1，指向下一个数组位置
        count++;
    }

    // 5. 记录实际启用的设备数量
    mgr.tcp_device_count = count;
}
// main.c - 新增初始化函数
void init_rtu_devices(void)
{
    memset(mgr.rtu_devices, 0, sizeof(mgr.rtu_devices));
    int count = 0;

    for (int i = 0; i < MAX_RTU_DEVICES; i++) {
        if (cfg.rtu_enable[i] == 0) continue;

        rtu_device_t *dev = &mgr.rtu_devices[count];
        strcpy(dev->port, cfg.rtu_port[i]);
        dev->baudrate = cfg.rtu_baudrate[i];
        dev->slave_id = cfg.rtu_slave_id[i];
        dev->read_addr = cfg.rtu_read_addr[i];
        dev->read_count = cfg.rtu_read_count[i];
        dev->collect_enable = 1;
        dev->ctx = NULL;
        snprintf(dev->name, sizeof(dev->name), "RTU_Dev_%d", i);

        count++;
    }

    mgr.rtu_device_count = count;
}
void *modbus_tcp_read_generic(void *arg)
{
    int idx = *(int *)arg;
    free(arg);

    tcp_device_config_t *dev = &mgr.tcp_devices[idx];

    // 初始化采集状态
    dev->collect_enable = 1;
    dev->last_collect_state = 1;  // 初始为开启

    while (mgr.running) {
        // ===== 采集开关状态变化检测 =====
        if (!dev->collect_enable) {
            // 采集被关闭
            if (dev->last_collect_state == 1) {
                // 状态切换：开启 → 关闭
                dev->status = 1;  // 采集关闭
                snprintf(dev->alarm_msg, sizeof(dev->alarm_msg), "TCP采集已关闭");

                // 释放连接资源
                if (dev->ctx != NULL) {
                    modbus_close(dev->ctx);
                    modbus_free(dev->ctx);
                    dev->ctx = NULL;
                }
                LOG_INFO("TCP设备%d: 云端指令关闭采集，已释放连接资源", idx);
                dev->last_collect_state = 0;
            }
            sleep(2);
            continue;
        }

        // ===== 采集恢复：关闭 → 开启 =====
        if (dev->last_collect_state == 0) {
            LOG_INFO("TCP设备%d: 云端指令开启采集，恢复正常采集任务", idx);
            dev->last_collect_state = 1;
            // 恢复时清除之前的故障状态（如果之前是离线故障）
            if (dev->status == 2 || dev->status == 1) {
                dev->status = 0;
                dev->tcp_fail_time = 0;
                snprintf(dev->alarm_msg, sizeof(dev->alarm_msg), "TCP已恢复");
            }
        }

        // ===== 执行采集 =====
        printf("IP=%s, 端口=%d, 从站=%d\n", dev->ip, dev->port, dev->slave_id);

        if (modbus_tcp_device_read(dev, &dev->ctx, 0, 10, dev->regs) == -1) {
            LOG_ERROR("TCP设备%d: 所有热重试失败，60s冷休眠后重试", idx);
            // ★★★ 只有在之前不是故障状态时才更新故障信息 ★★★
            if (dev->status != 2) {
                dev->status = 2;  // 离线故障
                dev->tcp_fail_time = time(NULL);
                char time_str[32];
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&dev->tcp_fail_time));
                snprintf(dev->alarm_msg, sizeof(dev->alarm_msg),
                         "TCP离线,首次故障: %s", time_str);
            }
            sleep(60);
            continue;
        }

        // ===== 读取成功：如果当前是故障状态，恢复 =====
        if (dev->status == 2) {
            dev->status = 0;
            dev->tcp_fail_time = 0;
            snprintf(dev->alarm_msg, sizeof(dev->alarm_msg), "TCP已恢复");
            LOG_INFO("TCP设备%d: 故障恢复", idx);
        }

        // 打印采集数据
        for (int i = 0; i < 2; i++) {
            LOG_INFO("TCP_REG[%d] = %d", i, dev->regs[i]);
        }

        // 发送到 CAN 总线
        pthread_mutex_lock(&mgr.bus_mutex);
        if (can_send(0x123, 2, dev->regs) != 0) {
            LOG_WARN("TCP:CAN数据发送失败");
        }
        pthread_mutex_unlock(&mgr.bus_mutex);

        sleep(1);
    }

    LOG_INFO("TCP采集线程全局退出");
    return NULL;
}


void *modbus_rtu_read_generic(void *arg)
{
    int idx = *(int *)arg;
    free(arg);

    rtu_device_t *dev = &mgr.rtu_devices[idx];
//   // ===== 直接硬编码赋值所有成员（测试用） =====
//     // 配置参数
//     strcpy(dev->port, "/dev/ttyS3");
//     dev->baudrate = 4800;
//     dev->slave_id = 1;
//     dev->read_addr = 0;
//     dev->read_count = 2;

//     // 运行时状态
//     dev->collect_enable = 1;
//     dev->status = 0;
//     dev->last_collect_state = 1;
//     dev->last_reported_status = -1;
//     dev->fail_time = 0;
//     dev->ctx = NULL;
    snprintf(dev->alarm_msg, sizeof(dev->alarm_msg), "RTU初始化完成");
    snprintf(dev->name, sizeof(dev->name), "RTU_Dev_%d", idx);

    LOG_INFO("RTU设备%d: 串口=%s, 波特率=%d, 从站=%d, 读地址=%d, 读数量=%d",
             idx, dev->port, dev->baudrate, dev->slave_id,
             dev->read_addr, dev->read_count);
    uint32_t can_id = CAN_ID_BASE_RTU + idx;  // RTU设备0→0x100, 设备1→0x101

    while (mgr.running) {
        // ===== 采集开关状态变化检测 =====
        if (!dev->collect_enable) {
            if (dev->last_collect_state == 1) {
                // 状态切换：开启 → 关闭
                dev->status = 1;
                snprintf(dev->alarm_msg, sizeof(dev->alarm_msg), "RTU采集已关闭");

                if (dev->ctx != NULL) {
                    modbus_close(dev->ctx);
                    modbus_free(dev->ctx);
                    dev->ctx = NULL;
                }
                LOG_INFO("RTU设备%d: 云端指令关闭采集，已释放串口资源", idx);
                dev->last_collect_state = 0;
            }
            sleep(2);
            continue;
        }

        // ===== 采集恢复：关闭 → 开启 =====
        if (dev->last_collect_state == 0) {
            LOG_INFO("RTU设备%d: 云端指令开启采集，恢复正常采集任务", idx);
            dev->last_collect_state = 1;
            if (dev->status == 2|| dev->status == 1) {
                dev->status = 0;
                dev->fail_time = 0;
                snprintf(dev->alarm_msg, sizeof(dev->alarm_msg), "RTU已恢复");
            }
        }

        // ===== 执行采集 =====
        printf("RTU设备%d: 串口=%s, 波特率=%d, 从站=%d\n",
               idx, dev->port, dev->baudrate, dev->slave_id);

        if (modbus_rtu_device_read(dev, 0, 2, dev->regs) == -1) {
            LOG_ERROR("RTU设备%d: 所有热重试失败，60s冷休眠后重试", idx);
            if (dev->status != 2) {
                dev->status = 2;
                dev->fail_time = time(NULL);
                char time_str[32];
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&dev->fail_time));
                snprintf(dev->alarm_msg, sizeof(dev->alarm_msg),
                         "RTU离线，首次故障: %s", time_str);
            }
            sleep(60);
            continue;
        }

        // ===== 读取成功：如果当前是故障状态，恢复 =====
        if (dev->status == 2) {
            dev->status = 0;
            dev->fail_time = 0;
            snprintf(dev->alarm_msg, sizeof(dev->alarm_msg), "RTU已恢复");
            LOG_INFO("RTU设备%d: 故障恢复", idx);
        }

        // 打印采集数据
        for (int i = 0; i < 2; i++) {
            LOG_INFO("RTU_DATA[%d] = %d .从站地址：%d", i, dev->regs[i],dev->slave_id);
        }

        // 发送到 CAN 总线
        pthread_mutex_lock(&mgr.bus_mutex);
        if (can_send(can_id, 2, dev->regs) != 0) {
            LOG_WARN("RTU设备%d: CAN数据发送失败", idx);
        }
        pthread_mutex_unlock(&mgr.bus_mutex);

        sleep(1);
    }

    LOG_INFO("RTU采集线程全局退出");
    return NULL;
}

void *can_receive_pthread(void *arg) {
    unsigned char data[8];
    int len;
        uint32_t can_id;

    while (mgr.running) {
      
      //  pthread_mutex_lock(&mgr.bus_mutex);
        if ( can_receive(&can_id, data, &len) == 0) {
            // 构造一个临时 can_frame 给 handler 用
            struct can_frame frame;
            frame.can_id = can_id;
            frame.can_dlc = len;
            for (int i = 0; i < len && i < 8; i++) {
                frame.data[i] = data[i];
            }
            
            // 查表路由
            for (int i = 0; i < ROUTE_TABLE_SIZE; i++) {
                if (can_id >= route_table[i].base && can_id < route_table[i].max) {
                    route_table[i].handler(can_id, &frame);
                    break;
                }
            }
        }
    }
    return NULL;
}

void *MQTT_pthread(void *arg) {
    int idx = *(int *)arg;
    free(arg);  // 释放传入的索引内存（可选，但推荐）
    int last_rtu_status = -1, last_tcp_status = -1;
    int last_can_fault = -1;
    int last_can_reconnect_count = -1;  // ★★★ 新增：记录上次重连次数 ★★★


    // 记录上一次采集状态，用于去重日志
    // gateway_manager_t *mgr = &mgr;  // 或者直接用全局 mgr
    while (mgr.running) {
        mgr.press = BMP280_READ();
        mgr.mqtt_connect_states = mqtt_is_connected();

        if (mgr.mqtt_connect_states) {
            data_cache_flush();
            pthread_mutex_lock(&mgr.data_mutex);
            MQTT_publish(mgr.latest_temperature, mgr.latest_humidity, mgr.press);
          pthread_mutex_unlock(&mgr.data_mutex);
        } else {
            data_cache_push_telemetry(mgr.latest_temperature, mgr.latest_humidity, mgr.press);
         //   data_cache_push_alarm_rtu("RTU",);
            //data_cache_push_alarm_rtu("RTU",); //本地缓冲和上报还没适配好
            printf("【缓存】MQTT离线，数据已缓存\n");
        }

   // ===== RTU 设备状态上报 =====
        for (int i = 0; i < mgr.rtu_device_count; i++) {
            rtu_device_t *dev = &mgr.rtu_devices[i];
            if (dev->status != dev->last_reported_status) {
                if (dev->status == 0) {
                    mqtt_publish_alarm("RTU", i, "running", "RTU", "RTU采集运行中");
                } else if (dev->status == 1) {
                    mqtt_publish_alarm("RTU", i, "stopped", "RTU", "RTU采集已关闭");
                } else if (dev->status == 2) {
                    mqtt_publish_alarm("RTU", i, "offline", "RTU", dev->alarm_msg);
                }
                dev->last_reported_status = dev->status;
            }
        }


// ===== TCP 设备状态上报（遍历所有设备） =====
for (int i = 0; i < mgr.tcp_device_count; i++) {
    tcp_device_config_t *dev = &mgr.tcp_devices[i];
    if (dev->status != dev->last_reported_status) {
        if (dev->status == 0) {
            mqtt_publish_alarm("TCP", i, "running", "TCP", "TCP采集运行中");
        } else if (dev->status == 1) {
            mqtt_publish_alarm("TCP", i, "stopped", "TCP", "TCP采集已关闭");
        } else if (dev->status == 2) {
            mqtt_publish_alarm("TCP", i, "offline", "TCP", dev->alarm_msg);
        }
        dev->last_reported_status = dev->status;
    }
}
        // ===== ★★★ CAN 状态上报（修正版） ★★★ =====
        can_status_t can_status;
        can_get_status(&can_status);

        // 条件1：状态变化（正常↔故障）
        // 条件2：故障中且重连次数变化
        if (can_status.is_in_fault != last_can_fault ||
            (can_status.is_in_fault == 1 && can_status.total_reconnect_count != last_can_reconnect_count)) {
            
            if (can_status.is_in_fault == 0) {
                mqtt_publish_CAN_alarm("can_recovered", "CAN", can_status.last_alarm_msg);
            } else {
                mqtt_publish_CAN_alarm("can_fault", "CAN", can_status.last_alarm_msg);
            }
            last_can_fault = can_status.is_in_fault;
            last_can_reconnect_count = can_status.total_reconnect_count;
        }

        sleep(1);
    }
    return NULL;
}

// ====================== 主函数 ======================
int main(void) {

    // rtu初始化随机种子
    srand((unsigned)time(NULL));
    // 注册全局清理
    atexit(gateway_cleanup);
    // 初始化全局mgr资源
    gateway_manager_init(&mgr);
    // 加载配置
    config_set_default(&cfg);
    config_load("./gateway.conf", &cfg);
    printf("Modbus port: %s\n", cfg.modbus_port);
    printf("can port: %s\n", cfg.can_interface);

    // 底层外设初始化
    mqtt_Init();
    can_Init();
    modbus_relay_init();
    BMP280_READ_Init();
    data_cache_init();
    init_tcp_devices();
    init_rtu_devices();


// 动态创建 RTU 线程
for (int i = 0; i < mgr.rtu_device_count; i++) {
    rtu_device_t  *dev = &mgr.rtu_devices[i];
        LOG_INFO("主程序:启动RTU设备 %s (%D:%d)", dev->port, dev->slave_id, dev->baudrate);

    int *idx_ptr = malloc(sizeof(int));
    *idx_ptr = i;
    pthread_create(&mgr.threads[10 + i], NULL, modbus_rtu_read_generic, idx_ptr);
}// ===== 动态创建 TCP 设备采集线程 =====

for (int i = 0; i < mgr.tcp_device_count; i++) {
    tcp_device_config_t *dev = &mgr.tcp_devices[i];
    LOG_INFO("主程序:启动TCP设备 %s (%s:%d)", dev->ip, dev->ip, dev->port);
    
    // 每个线程传入设备索引，线程内部根据索引找到对应的设备
    int *idx_ptr = malloc(sizeof(int));
    *idx_ptr = i;
    pthread_create(&mgr.threads[2 + i], NULL, modbus_tcp_read_generic, idx_ptr);
}   

    pthread_create(&mgr.threads[1], NULL, can_receive_pthread, &mgr);
    pthread_create(&mgr.threads[0], NULL, MQTT_pthread, &mgr);

    // 主线程循环
    while(1)
    {
        LOG_DEBUG("主线程运行中...");
        sleep(1);
    }
}
