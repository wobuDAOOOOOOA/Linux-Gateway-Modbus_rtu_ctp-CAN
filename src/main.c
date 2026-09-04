#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <linux/can.h>
#include <sys/socket.h>
#include <signal.h>
#include "gateway.h"
#include "modbus_tcp.h"
#include "modbus_rtu.h"
#include "can.h"
#include "mqtt_huawei.h"
#include "log.h"
#include "config.h"
#include "relay.h"
#include "bmp280.h"
#include "data_cache.h"

#define true 1
#define COLD_SLEEP_TIME 10
// 全局变量定义
gateway_manager_t mgr;
volatile sig_atomic_t g_config_reload_flag = 0;
// 工具函数
static int cold_sleep_with_check(int seconds)
{
    for (int i = 0; i < seconds; i++) {
        if (mgr.collect_stop) {
            return -1;   // 被热加载中断
        }
        sleep(1);
    }
    return 0;
}
// ====================== 资源初始化函数 ======================
static void gateway_manager_init(gateway_manager_t *mgr)
{
    memset(mgr, 0, sizeof(gateway_manager_t));

    pthread_mutex_init(&mgr->data_mutex, NULL);
    pthread_mutex_init(&mgr->bus_mutex, NULL);
    pthread_rwlock_init(&mgr->config_lock, NULL);

    mgr->running = true;
    mgr->collect_stop = 0;      // ★ 0=采集运行中
}

// ====================== 退出清理函数 ======================
void signal_handler(int sig) {
    LOG_INFO("收到信号 %d，准备退出\n", sig);
    mgr.running = 0;            // 让所有线程退出
}

void sigusr1_handler(int sig)
{
    LOG_INFO("收到SIGHUP，重新加载配置\n");
    g_config_reload_flag = 1;   // ★ 只设标志位
}

void gateway_cleanup(void)
{
    LOG_INFO("执行网关全资源释放...");

    for (int i = 0; i < mgr.tcp_device_count; i++) {
        if (mgr.tcp_devices[i].ctx != NULL) {
            modbus_close(mgr.tcp_devices[i].ctx);
            modbus_free(mgr.tcp_devices[i].ctx);
            mgr.tcp_devices[i].ctx = NULL;
            LOG_INFO("TCP设备%d: 已释放Modbus上下文", i);
        }
    }

    for (int i = 0; i < mgr.rtu_device_count; i++) {
        if (mgr.rtu_devices[i].ctx != NULL) {
            modbus_close(mgr.rtu_devices[i].ctx);
            modbus_free(mgr.rtu_devices[i].ctx);
            mgr.rtu_devices[i].ctx = NULL;
            LOG_INFO("RTU设备%d: 已释放Modbus上下文", i);
        }
    }

    pthread_mutex_destroy(&mgr.data_mutex);
    pthread_mutex_destroy(&mgr.bus_mutex);
    pthread_rwlock_destroy(&mgr.config_lock);

    LOG_INFO("所有资源释放完成！");
}

void init_tcp_devices(void)
{
    printf("config以及赋值给了所有");
    //memset(mgr.tcp_devices, 0, sizeof(mgr.tcp_devices));

    int count = 0;
    for (int i = 0; i < MAX_TCP_DEVICES; i++) {
        if (cfg.tcp_enable[i] == 0) continue;

        tcp_device_config_t *dev = &mgr.tcp_devices[count];
        strcpy(dev->ip, cfg.tcp_ip[i]);
        dev->port = cfg.tcp_port[i];
        dev->slave_id = cfg.tcp_slave_id[i];
        dev->timeout_ms = 500;
         dev->read_addr = cfg.tcp_read_addr[i];    // ★ 新增
        dev->read_count = cfg.tcp_read_count[i];  // ★ 新增
        dev->ctx = NULL;
       
        count++;
    }
    mgr.tcp_device_count = count;
}

void init_rtu_devices(void)
{
    //memset(mgr.rtu_devices, 0, sizeof(mgr.rtu_devices));
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
void init_can_devices(void)
{
    memset(mgr.can_devices, 0, sizeof(mgr.can_devices));
    mgr.can_device_count = 0;

    for (int i = 0; i < MAX_CAN_DEVICES; i++) {
        // 只要 can_devices[i] != 0 就认为有效，不再依赖 enable 字段
        if (cfg.can_devices[i] != 0) {
            mgr.can_devices[mgr.can_device_count].can_id = cfg.can_devices[i];
                memset( mgr.can_devices[i].data, 0, sizeof(mgr.can_devices[i].data));
 
                    mgr.can_devices[i].dlc = 0;
            mgr.can_device_count++;
        }
    }
}
// ====================== TCP 采集线程（含局部变量优化） ======================
void *modbus_tcp_read_generic(void *arg)
{
    int idx = *(int *)arg;
    free(arg);

    // ★ 局部变量：存放从配置复制出来的值
    char local_ip[32];
    int local_port;
    int local_slave_id;
    int local_read_addr;
    int local_read_count;
   // uint32_t can_id = CAN_ID_BASE_TCP + idx;

    // ★ 首次读取配置（读锁保护）
    pthread_rwlock_rdlock(&mgr.config_lock);
    tcp_device_config_t *dev = &mgr.tcp_devices[idx];
    strcpy(local_ip, dev->ip);
    local_port = dev->port;
    local_slave_id = dev->slave_id;
    local_read_addr = dev->read_addr;    // ★ 新增
    local_read_count = dev->read_count;  // ★ 新增
    dev->collect_enable = 1;
    dev->last_collect_state = 1;
    pthread_rwlock_unlock(&mgr.config_lock);

  //  uint16_t regs[32];
    modbus_t *ctx = NULL;
    int reload_counter = 0;

    // ★ while 条件改成 !collect_stop
    while (!mgr.collect_stop) {
        // ★ 每10次循环重读配置（热加载生效）
        reload_counter++;
        if (reload_counter >= 10) {
            reload_counter = 0;
            pthread_rwlock_rdlock(&mgr.config_lock);
            tcp_device_config_t *dev_reload = &mgr.tcp_devices[idx];
            strcpy(local_ip, dev_reload->ip);
            local_port = dev_reload->port;
            local_slave_id = dev_reload->slave_id;
            pthread_rwlock_unlock(&mgr.config_lock);
            LOG_INFO("TCP设备%d: 重新读取配置", idx);
        }

        // ===== 采集开关状态变化检测（运行时状态，不需要锁） =====
        if (!dev->collect_enable) {
            if (dev->last_collect_state == 1) {
                dev->status = 1;
                snprintf(dev->alarm_msg, sizeof(dev->alarm_msg), "TCP采集已关闭");
                if (ctx != NULL) {
                    modbus_close(ctx);
                    modbus_free(ctx);
                    ctx = NULL;
                }
                LOG_INFO("TCP设备%d: 云端指令关闭采集", idx);
                dev->last_collect_state = 0;
            }
            sleep(2);
            continue;
        }

        if (dev->last_collect_state == 0) {
            LOG_INFO("TCP设备%d: 云端指令开启采集", idx);
            dev->last_collect_state = 1;
            if (dev->status == 2 || dev->status == 1) {
                dev->status = 0;
                dev->tcp_fail_time = 0;
                snprintf(dev->alarm_msg, sizeof(dev->alarm_msg), "TCP已恢复");
            }
        }

        // ===== 执行采集（用局部变量，不用 dev->ip/port/slave_id） =====
        LOG_DEBUG("IP=%s, 端口=%d, 从站=%d\n", local_ip, local_port, local_slave_id);

        // ★ 需要 modbus_tcp_device_read_with_params 版本
        if (modbus_tcp_device_read_with_params(local_ip, local_port, local_slave_id,
                                                &ctx, local_read_addr, local_read_count, dev->regs) == -1) {
            LOG_ERROR("TCP设备%d: 所有热重试失败，60s冷休眠后重试", idx);
            if (dev->status != 2) {
                dev->status = 2;
                dev->tcp_fail_time = time(NULL);
                char time_str[32];
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&dev->tcp_fail_time));
                snprintf(dev->alarm_msg, sizeof(dev->alarm_msg),
                         "TCP离线,首次故障: %s", time_str);
            }
        if (cold_sleep_with_check(COLD_SLEEP_TIME) == -1) {
            LOG_INFO("冷休眠被热加载中断");
            break;  // 退出线程
        }
            continue;
        }

        if (dev->status == 2) {
            dev->status = 0;
            dev->tcp_fail_time = 0;
            snprintf(dev->alarm_msg, sizeof(dev->alarm_msg), "TCP已恢复");
            LOG_INFO("TCP设备%d: 故障恢复", idx);
        }

        for (int i = 0; i < local_read_count; i++) {
            LOG_DEBUG("TCP_REG[%d] = %d", i, dev->regs[i]);
        }

        // pthread_mutex_lock(&mgr.bus_mutex);
        // if (can_send(can_id, local_read_count, dev->regs) != 0) {
        //     LOG_WARN("TCP:CAN数据发送失败");
        // }
        // pthread_mutex_unlock(&mgr.bus_mutex);

        sleep(1);
    }

    LOG_INFO("TCP采集线程全局退出");
    return NULL;
}

// ====================== RTU 采集线程（含局部变量优化） ======================
void *modbus_rtu_read_generic(void *arg)
{
    int idx = *(int *)arg;
    free(arg);

    // ★ 局部变量
    char local_port[64];
    int local_baudrate;
    int local_slave_id;
    int local_read_addr;
    int local_read_count;
 //   uint32_t can_id = CAN_ID_BASE_RTU + idx;

    // ★ 首次读取配置（读锁保护）
    pthread_rwlock_rdlock(&mgr.config_lock);
    rtu_device_t *dev = &mgr.rtu_devices[idx];
    strcpy(local_port, dev->port);
    local_baudrate = dev->baudrate;
    local_slave_id = dev->slave_id;
    local_read_addr = dev->read_addr;
    local_read_count = dev->read_count;
    dev->collect_enable = 1;
    dev->last_collect_state = 1;
    pthread_rwlock_unlock(&mgr.config_lock);

   // uint16_t regs[32];
    modbus_t *ctx = NULL;
    int reload_counter = 0;

    snprintf(dev->alarm_msg, sizeof(dev->alarm_msg), "RTU初始化完成");
    snprintf(dev->name, sizeof(dev->name), "RTU_Dev_%d", idx);

    LOG_DEBUG("RTU设备%d: 串口=%s, 波特率=%d, 从站=%d, 读地址=%d, 读数量=%d",
             idx, local_port, local_baudrate, local_slave_id,
             local_read_addr, local_read_count);

    // ★ while 条件改成 !collect_stop
    while (!mgr.collect_stop) {
        // ★ 每10次循环重读配置
        reload_counter++;
        if (reload_counter >= 10) {
            reload_counter = 0;
            pthread_rwlock_rdlock(&mgr.config_lock);
            rtu_device_t *dev_reload = &mgr.rtu_devices[idx];
            strcpy(local_port, dev_reload->port);
            local_baudrate = dev_reload->baudrate;
            local_slave_id = dev_reload->slave_id;
            local_read_addr = dev_reload->read_addr;
            local_read_count = dev_reload->read_count;
            pthread_rwlock_unlock(&mgr.config_lock);
            LOG_DEBUG("RTU设备%d: 重新读取配置", idx);
        }

        // ===== 采集开关状态变化检测（运行时状态） =====
        if (!dev->collect_enable) {
            if (dev->last_collect_state == 1) {
                dev->status = 1;
                snprintf(dev->alarm_msg, sizeof(dev->alarm_msg), "RTU采集已关闭");
                if (ctx != NULL) {
                    modbus_close(ctx);
                    modbus_free(ctx);
                    ctx = NULL;
                }
                LOG_INFO("RTU设备%d: 云端指令关闭采集", idx);
                dev->last_collect_state = 0;
            }
            sleep(2);
            continue;
        }

        if (dev->last_collect_state == 0) {
            LOG_INFO("RTU设备%d: 云端指令开启采集", idx);
            dev->last_collect_state = 1;
            if (dev->status == 2 || dev->status == 1) {
                dev->status = 0;
                dev->fail_time = 0;
                snprintf(dev->alarm_msg, sizeof(dev->alarm_msg), "RTU已恢复");
            }
        }

        // ===== 执行采集（用局部变量） =====
        LOG_DEBUG("RTU设备%d: 串口=%s, 波特率=%d, 从站=%d\n",
               idx, local_port, local_baudrate, local_slave_id);

        // ★ 需要 modbus_rtu_device_read_with_params 版本
        if (modbus_rtu_device_read_with_params(local_port, local_baudrate, local_slave_id,
                                                local_read_addr, local_read_count,
                                                &ctx, dev->regs) == -1) {
            LOG_ERROR("RTU设备%d: 所有热重试失败，60s冷休眠后重试", idx);
            if (dev->status != 2) {
                dev->status = 2;
                dev->fail_time = time(NULL);
                char time_str[32];
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&dev->fail_time));
                snprintf(dev->alarm_msg, sizeof(dev->alarm_msg),
                         "RTU离线，首次故障: %s", time_str);
            }
             if (cold_sleep_with_check(COLD_SLEEP_TIME) == -1) {
            LOG_INFO("冷休眠被热加载中断");
            break;  // 退出线程
        }
            continue;
        }

        if (dev->status == 2) {
            dev->status = 0;
            dev->fail_time = 0;
            snprintf(dev->alarm_msg, sizeof(dev->alarm_msg), "RTU已恢复");
            LOG_INFO("RTU设备%d: 故障恢复", idx);
        }

        for (int i = 0; i < local_read_count; i++) {
            LOG_INFO("RTU_DATA[%d] = %d .从站地址：%d", i, dev->regs[i], local_slave_id);
        }

        // pthread_mutex_lock(&mgr.bus_mutex);
        // if (can_send(can_id, local_read_count, regs) != 0) {
        //     LOG_WARN("RTU设备%d: CAN数据发送失败", idx);
        // }
        // pthread_mutex_unlock(&mgr.bus_mutex);

        sleep(1);
    }

    LOG_INFO("RTU采集线程全局退出");
    return NULL;
}

// ====================== CAN 接收线程 ======================
void *can_receive_pthread(void *arg) {
    unsigned char data[8];
    int len;
    uint32_t can_id;
    int found;

    while (!mgr.collect_stop) {
        if (can_receive(&can_id, data, &len) == 0) {
            found = 0;
            for (int i = 0; i < mgr.can_device_count; i++) {
                if (mgr.can_devices[i].can_id == can_id) {
                    found = 1;
                    
                    // 打印接收到的数据
                    for (int j = 0; j < len; j++) {
                        LOG_DEBUG("CAN 设备[%d] ID=0x%x 数据[%d]=%d\n", i, can_id, j, data[j]);
                    }
                    
                    // 加锁保存数据
                    pthread_mutex_lock(&mgr.data_mutex);
                    memcpy(mgr.can_devices[i].data, data, len);
                    mgr.can_devices[i].dlc = len;
                    pthread_mutex_unlock(&mgr.data_mutex);
                    break;
                }
            }
            
            // 没找到匹配的设备，打印警告（可选）
            if (!found) {
                LOG_WARN("CAN: 收到未知设备ID=0x%x 的数据，已忽略\n", can_id);
            }
        }
        usleep(1000);  // 1ms，防止CPU空转
    }
    return NULL;
}

// ====================== MQTT 线程 ======================
void *MQTT_pthread(void *arg) {
    int idx = *(int *)arg;
    free(arg);

    // ★ 局部变量：存放从配置数组复制出来的状态
    int rtu_status_copy[MAX_RTU_DEVICES];
    char rtu_alarm_copy[MAX_RTU_DEVICES][128];
    int tcp_status_copy[MAX_TCP_DEVICES];
    char tcp_alarm_copy[MAX_TCP_DEVICES][128];
    
    // ★ last_reported_status 由 MQTT 线程自己维护，不依赖结构体中的字段
    int last_rtu_status[MAX_RTU_DEVICES];
    int last_tcp_status[MAX_TCP_DEVICES];
    for (int i = 0; i < MAX_RTU_DEVICES; i++) last_rtu_status[i] = -1;
    for (int i = 0; i < MAX_TCP_DEVICES; i++) last_tcp_status[i] = -1;
    
    int rtu_count, tcp_count;
    int last_can_fault = -1;
    int last_can_reconnect_count = -1;
    char json_buf[1024];

    while (mgr.running) {
        mgr.press = BMP280_READ();
        mgr.mqtt_connect_states = mqtt_is_connected();

        // ===== 1. ★ 读锁保护：复制所有需要的数据到局部变量 =====
        pthread_rwlock_rdlock(&mgr.config_lock);
        
        rtu_count = mgr.rtu_device_count;
        for (int i = 0; i < rtu_count && i < MAX_RTU_DEVICES; i++) {
            rtu_status_copy[i] = mgr.rtu_devices[i].status;
            strcpy(rtu_alarm_copy[i], mgr.rtu_devices[i].alarm_msg);
        }
        
        tcp_count = mgr.tcp_device_count;
        for (int i = 0; i < tcp_count && i < MAX_TCP_DEVICES; i++) {
            tcp_status_copy[i] = mgr.tcp_devices[i].status;
            strcpy(tcp_alarm_copy[i], mgr.tcp_devices[i].alarm_msg);
        }
        
        pthread_rwlock_unlock(&mgr.config_lock);  // ★ 立即解锁

        // ===== 2. JSON 构建和 MQTT 上报（用局部变量中的数据） =====
        build_current_json(json_buf, sizeof(json_buf));
//  if (mgr.mqtt_connect_states && !data_cache_is_flushing()) {
        if (mgr.mqtt_connect_states) {
            data_cache_flush();
            mqtt_publish_data1();
        } else {
            build_current_json(json_buf, sizeof(json_buf));
            data_cache_push_telemetry_json(json_buf);
            LOG_WARN("【缓存】MQTT离线，数据已缓存\n");
        }
        
/*TO DO data_cache_flush() 是同步执行，执行补传的时候mqtt不会去读取共享结构体，导致补传期间数据丢失
需要把补传放入一个单独的线程补传时mqtt线程读共享结构体拼接json继续放到数据库直到补传完成才正常上报*/
        // ===== 3. RTU 设备状态上报（用 rtu_status_copy，不用 mgr.rtu_devices） =====
   // ===== 3. RTU 设备状态上报（统一处理，先检测变化，再决定走MQTT还是缓存） =====
for (int i = 0; i < rtu_count && i < MAX_RTU_DEVICES; i++) {
    int current_status = rtu_status_copy[i];
    
    // ★ 只有状态发生变化时才处理
    if (current_status != last_rtu_status[i]) {
        // 1. 构造告警参数
        const char *alarm_type;
        const char *alarm_msg;
        if (current_status == 0) {
            alarm_type = "running";
            alarm_msg = "RTU采集运行中";
        } else if (current_status == 1) {
            alarm_type = "stopped";
            alarm_msg = "RTU采集已关闭";
        } else { // status == 2
            alarm_type = "offline";
            alarm_msg = rtu_alarm_copy[i];
        }

        // 2. 根据MQTT连接状态决定走哪条路
        if (mgr.mqtt_connect_states) {
            // ★ MQTT在线 → 直接上报MQTT
            mqtt_publish_alarm("RTU", i, alarm_type,"RTU", alarm_msg);
        } else {
            // ★ MQTT离线 → 入缓存，等上线后补发
            data_cache_push_alarm_rtu("RTU", i, alarm_type, alarm_msg);
            LOG_INFO("RTU设备%d 状态变化 入缓存 (状态=%s)\n", i, alarm_type);
        }

        // 3. 更新上次状态
        last_rtu_status[i] = current_status;
    }
}

    // ===== 4. TCP 设备状态上报（统一处理，先检测变化，再决定走MQTT还是缓存） =====
for (int i = 0; i < tcp_count && i < MAX_TCP_DEVICES; i++) {
    int current_status = tcp_status_copy[i];
    
    // ★ 只有状态发生变化时才处理
    if (current_status != last_tcp_status[i]) {
        // 1. 构造告警参数
        const char *alarm_type;
        const char *alarm_msg;
        if (current_status == 0) {
            alarm_type = "running";
            alarm_msg = "TCP采集运行中";
        } else if (current_status == 1) {
            alarm_type = "stopped";
            alarm_msg = "TCP采集已关闭";
        } else { // status == 2
            alarm_type = "offline";
            alarm_msg = tcp_alarm_copy[i];
        }

        // 2. 根据MQTT连接状态决定走哪条路
        if (mgr.mqtt_connect_states) {
            // ★ MQTT在线 → 直接上报MQTT
            mqtt_publish_alarm("TCP", i, alarm_type, "TCP", alarm_msg);
        } else {
            // ★ MQTT离线 → 入缓存，等上线后补发
            data_cache_push_alarm_tcp("TCP", i,  alarm_msg);
            LOG_INFO("TCP设备%d 状态变化 入缓存 (状态=%s)\n", i, alarm_type);
        }

        // 3. 更新上次状态
        last_tcp_status[i] = current_status;
    }
}

        // ===== 5. CAN 状态上报 =====
        can_status_t can_status;
        can_get_status(&can_status);

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

// ====================== 热加载核心函数 ======================
void reconfig_hot(void)
{
    LOG_WARN ("热加载开始，停止所有采集线程...");

    // 1. 停止所有采集线程
    mgr.collect_stop = 1;

    // 2. 等待所有 RTU 采集线程退出 (threads[10] ~ threads[10+MAX_RTU_DEVICES-1])
    for (int i = 0; i < MAX_RTU_DEVICES; i++) {
        if (mgr.threads[10 + i] != 0) {
            pthread_join(mgr.threads[10 + i], NULL);
            mgr.threads[10 + i] = 0;
            LOG_INFO("RTU线程 %d 已退出", i);
        }
    }
    // 等待所有 TCP 采集线程退出 (threads[2] ~ threads[2+MAX_TCP_DEVICES-1])
    for (int i = 0; i < MAX_TCP_DEVICES; i++) {
        if (mgr.threads[2 + i] != 0) {
            pthread_join(mgr.threads[2 + i], NULL);
            mgr.threads[2 + i] = 0;
            LOG_INFO("TCP线程 %d 已退出", i);
        }
    }

    LOG_INFO("所有采集线程已安全退出");

    // 3. 重新加载配置（写锁保护，防止MQTT线程同时读）
    pthread_rwlock_wrlock(&mgr.config_lock);
    init_tcp_devices();
    init_rtu_devices();
    pthread_rwlock_unlock(&mgr.config_lock);
    init_can_devices();

    LOG_INFO("配置已重新加载: RTU设备数=%d, TCP设备数=%d",
             mgr.rtu_device_count, mgr.tcp_device_count);

    // 4. 恢复采集
    mgr.collect_stop = 0;

    // 5. 重新创建所有采集线程
    for (int i = 0; i < mgr.rtu_device_count; i++) {
        int *idx_ptr = malloc(sizeof(int));
        *idx_ptr = i;
        pthread_create(&mgr.threads[10 + i], NULL, modbus_rtu_read_generic, idx_ptr);
        LOG_INFO("RTU线程 %d 已创建", i);
    }
    for (int i = 0; i < mgr.tcp_device_count; i++) {
        int *idx_ptr = malloc(sizeof(int));
        *idx_ptr = i;
        pthread_create(&mgr.threads[2 + i], NULL, modbus_tcp_read_generic, idx_ptr);
        LOG_INFO("TCP线程 %d 已创建", i);
    }
        pthread_create(&mgr.threads[1], NULL, can_receive_pthread, &mgr);
        LOG_INFO("CAN线程 %d 已创建\n");

    LOG_INFO("热加载完成！");
}

// ====================== 主函数 ======================
int main(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, sigusr1_handler);

    srand((unsigned)time(NULL));
    atexit(gateway_cleanup);

    gateway_manager_init(&mgr);

    config_set_default(&cfg);
    config_load("./gateway.conf", &cfg);
    LOG_INFO("Modbus port: %s\n", cfg.modbus_port);
    LOG_INFO("can port: %s\n", cfg.can_interface);

    mqtt_Init();
    can_Init();
    modbus_relay_init();
    BMP280_READ_Init();
    data_cache_init();

    init_tcp_devices();
    init_rtu_devices();
    init_can_devices();

    // 创建 RTU 采集线程
    for (int i = 0; i < mgr.rtu_device_count; i++) {
        int *idx_ptr = malloc(sizeof(int));
        *idx_ptr = i;
        pthread_create(&mgr.threads[10 + i], NULL, modbus_rtu_read_generic, idx_ptr);
        LOG_INFO("主程序: 启动RTU设备 %d", i);
    }

    // 创建 TCP 采集线程
    for (int i = 0; i < mgr.tcp_device_count; i++) {
        int *idx_ptr = malloc(sizeof(int));
        *idx_ptr = i;
        pthread_create(&mgr.threads[2 + i], NULL, modbus_tcp_read_generic, idx_ptr);
        LOG_INFO("主程序: 启动TCP设备 %d", i);
    }

    pthread_create(&mgr.threads[1], NULL, can_receive_pthread, &mgr);
    pthread_create(&mgr.threads[0], NULL, MQTT_pthread, &mgr);

    // 主线程循环
    while (mgr.running) {
        if (g_config_reload_flag) {
            g_config_reload_flag = 0;
            config_load("./gateway.conf", &cfg);
            reconfig_hot();
            LOG_INFO("热加载完成");
        }
        sleep(1);
    }

    return 0;
}