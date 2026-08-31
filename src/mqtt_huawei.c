#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <mosquitto.h>
#include "mqtt_huawei.h"
#include "relay.h"
#include "log.h"
#include "config.h"
#include "gateway.h"
#include "data_cache.h"
//extern gateway_config_t cfg;   //声明我要在这个文件使用config.h开放给我的cfg结构体
static int mqtt_connected_flag = 0;

// 全局MQTT句柄
struct mosquitto *g_mosq = NULL;


//extern gateway_manager_t mgr;  //用mgr访问device结构体的采集使能成员的值，在下发函数中修改使用他

// ====================== 重连成功后重新订阅 ======================
static void mqtt_resubscribe(void)
{
    int ret = mosquitto_subscribe(g_mosq, NULL, MQTT_SUB_CMD_TOPIC, 1);
    if(ret == MOSQ_ERR_SUCCESS) {
        printf("MQTT: 重连后重新订阅成功\n");
    } else {
        fprintf(stderr, "MQTT: 重连后重新订阅失败, 错误码: %d\n", ret);
    }
}
//to do cJSON解析库
// ====================== MQTT下行指令接收回调 ======================
void mqtt_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
    if(msg == NULL || msg->payload == NULL || msg->payloadlen <= 0) return;

    // 解析 request_id
    char request_id[64] = {0};
    const char *topic = msg->topic;
    const char *prefix = "request_id=";
    char *p = strstr(topic, prefix);
    if(p != NULL) {
        p += strlen(prefix);
        char *end = strchr(p, '/');
        if(end == NULL) end = p + strlen(p);
        int len = end - p;
        if(len > 0 && len < (int)sizeof(request_id)) {
            memcpy(request_id, p, len);
        }
    }

    char payload_buf[512] = {0};
    int copy_len = msg->payloadlen < (int)sizeof(payload_buf)-1 ? msg->payloadlen : (int)sizeof(payload_buf)-1;
    memcpy(payload_buf, msg->payload, copy_len);

    printf("收到云端下发指令: %s\n", payload_buf);

    // ===== RTU 控制 =====
    if(strstr(payload_buf, "rtu_start") != NULL || strstr(payload_buf, "rtu_stop") != NULL) {
        int is_start = (strstr(payload_buf, "rtu_start") != NULL);
        int is_all = (strstr(payload_buf, "all") != NULL);
        int device_idx = -1;
        // 解析设备编号
        if(!is_all) {
            char *last_char = payload_buf + strlen(payload_buf) - 1;  //这里不应该判断request id吗，待会打印看看

            device_idx = *last_char - '0';
            if(device_idx < 0 || device_idx >= mgr.rtu_device_count) {
                LOG_ERROR("MQTT下发: 无效的RTU设备编号 %d", device_idx);
                mqtt_send_command_response(request_id, -1, "invalid rtu device index");
                return;
            }
        }

        // 执行操作
        if(is_all) {
            for(int i = 0; i < mgr.rtu_device_count; i++) {
                mgr.rtu_devices[i].collect_enable = is_start ? 1 : 0;
                LOG_INFO("MQTT下发: %s RTU设备%d采集", is_start ? "开启" : "关闭", i);
            }
        } else {
            mgr.rtu_devices[device_idx].collect_enable = is_start ? 1 : 0;
            LOG_INFO("MQTT下发: %s RTU设备%d采集", is_start ? "开启" : "关闭", device_idx);
        }

        // RTU 控制继电器（保持兼容）
        if(is_start) {
            modbus_relay_off(0);
        } else {
            modbus_relay_on(0);
        }

        mqtt_send_command_response(request_id, 0, NULL);
        return;
    }

    // ===== TCP 控制（支持多设备） =====
    if(strstr(payload_buf, "tcp_start") != NULL || strstr(payload_buf, "tcp_stop") != NULL) {
        int is_start = (strstr(payload_buf, "tcp_start") != NULL);
        int is_all = (strstr(payload_buf, "all") != NULL);
        int device_idx = -1;

        // 解析设备编号
        if(!is_all) {
            char *last_char = payload_buf + strlen(payload_buf) - 1;
            device_idx = *last_char - '0';
            if(device_idx < 0 || device_idx >= mgr.tcp_device_count) {
                LOG_ERROR("MQTT下发: 无效的TCP设备编号 %d", device_idx);
                mqtt_send_command_response(request_id, -1, "invalid tcp device index");
                return;
            }
        }

        // 执行操作
        if(is_all) {
            for(int i = 0; i < mgr.tcp_device_count; i++) {
                mgr.tcp_devices[i].collect_enable = is_start ? 1 : 0;
                LOG_INFO("MQTT下发: %s TCP设备%d采集", is_start ? "开启" : "关闭", i);
            }
        } else {
            mgr.tcp_devices[device_idx].collect_enable = is_start ? 1 : 0;
            LOG_INFO("MQTT下发: %s TCP设备%d采集", is_start ? "开启" : "关闭", device_idx);
        }

        mqtt_send_command_response(request_id, 0, NULL);
        return;
    }

    // ===== 未知指令处理 =====
    LOG_WARN("MQTT下发: 未识别的指令 %s", payload_buf);
    mqtt_send_command_response(request_id, -1, "unknown command");
}

// ====================== 连接成功回调 ======================
void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
    if (rc == 0) {
        mqtt_connected_flag = 1;
        printf("MQTT: 连接成功\n");
        // ★★★ 连接成功后立即订阅 ★★★
        mqtt_resubscribe();
    } else {              //to do 进到else后用ping判断是本地网络断了还是mqtt断了，如果是本地网络断了就给mqtt_connected_flag赋值2
        mqtt_connected_flag = 0;
        printf("MQTT: 连接失败, rc=%d\n", rc);
    }
}

// ====================== 断开连接回调 ======================
void on_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
    mqtt_connected_flag = 0;
    printf("MQTT: 断开连接, rc=%d\n", rc);

    // mosquitto 库的网络连接和重连机制由其内部管理。
    // 只要你使用了 mosquitto_loop_start()，它会在后台自动处理重连，
    // 你通常不需要在 on_disconnect 中手动释放 g_mosq 本身——这个句柄应该在整个程序生命周期中保持有效，直到你主动调用 
    // mosquitto_destroy() 销毁它。
}

// ====================== 你原有全部上报函数 ======================
int mqtt_publish_data(float temperature, float humidity , float press)
{
    char payload[256];
    int ret;
 
    if (!g_mosq) {
        fprintf(stderr, "错误: MQTT客户端未初始化\n");
        return -1;
    }

    snprintf(payload, sizeof(payload), PAYLOAD_TEMPLATE, cfg.service_id, cfg.prop_temp, temperature, cfg.prop_hum, humidity ,cfg.prop_press,press);
    ret = mosquitto_publish(g_mosq, NULL, cfg.mqtt_topic, strlen(payload), payload, 1, false);
    if (ret != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "错误: 数据发布失败，错误码: %d\n", ret);
        return -1;
    }
    
    printf("成功: 已上报数据 - 温度: %.1f, 湿度: %.1f, 大气压: %.1f\n", temperature, humidity, press);
    return 0;
}

#include <time.h>

// ====================== 告警事件上报 ======================
int mqtt_publish_TCP_alarm(const char *Modbus_TCP_alarm_type, const char *Modbus_TCP_alarm_module, const char *Modbus_TCP_alarm_msg) {
    char payload[512];
    int ret;

    if (!g_mosq) {
        fprintf(stderr, "错误: MQTT客户端未初始化\n");
        return -1;
    }

    time_t now = time(NULL);
    char Modbus_TCP_alarm_time[32];
    strftime(Modbus_TCP_alarm_time, sizeof(Modbus_TCP_alarm_time), "%Y-%m-%d %H:%M:%S", localtime(&now));

    snprintf(payload, sizeof(payload),
        "{\"services\":[{\"service_id\":\"%s\",\"properties\":{"
        "\"Modbus_TCP_alarm_type\":\"%s\","
        "\"Modbus_TCP_alarm_module\":\"%s\","
        "\"Modbus_TCP_alarm_msg\":\"%s\","
        "\"Modbus_TCP_alarm_time\":\"%s\"}}]}",
        cfg.service_id, Modbus_TCP_alarm_type, Modbus_TCP_alarm_module, Modbus_TCP_alarm_msg, Modbus_TCP_alarm_time);

    ret = mosquitto_publish(g_mosq, NULL, cfg.mqtt_topic, strlen(payload), payload, 1, false);
    if (ret != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "错误: 告警上报失败，错误码: %d\n", ret);
        return -1;
    }

    printf("成功: 已上报告警 - 类型: %s, 模块: %s, 时间: %s, 详情: %s\n", 
           Modbus_TCP_alarm_type, Modbus_TCP_alarm_module, Modbus_TCP_alarm_time, Modbus_TCP_alarm_msg);
    return 0;
}

int mqtt_publish_RTU_alarm(const char *Modbus_RTU_alarm_type, const char *Modbus_RTU_alarm_module, const char *Modbus_RTU_alarm_msg) {
    char payload[512];
    int ret;

    if (!g_mosq) {
        fprintf(stderr, "错误: MQTT客户端未初始化\n");
        return -1;
    }

    time_t now = time(NULL);
    char Modbus_RTU_alarm_time[32];
    strftime(Modbus_RTU_alarm_time, sizeof(Modbus_RTU_alarm_time), "%Y-%m-%d %H:%M:%S", localtime(&now));

    snprintf(payload, sizeof(payload),
        "{\"services\":[{\"service_id\":\"%s\",\"properties\":{"
        "\"Modbus_RTU_alarm_type\":\"%s\","
        "\"Modbus_RTU_alarm_module\":\"%s\","
        "\"Modbus_RTU_alarm_msg\":\"%s\","
        "\"Modbus_RTU_alarm_time\":\"%s\"}}]}",
        cfg.service_id, Modbus_RTU_alarm_type, Modbus_RTU_alarm_module, Modbus_RTU_alarm_msg, Modbus_RTU_alarm_time);

    ret = mosquitto_publish(g_mosq, NULL, cfg.mqtt_topic, strlen(payload), payload, 1, false);
    if (ret != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "错误: 告警上报失败，错误码: %d\n", ret);
        return -1;
    }

    printf("成功: 已上报告警 - 类型: %s, 模块: %s, 时间: %s, 详情: %s\n", 
           Modbus_RTU_alarm_type, Modbus_RTU_alarm_module, Modbus_RTU_alarm_time, Modbus_RTU_alarm_msg);
    return 0;
}

int mqtt_publish_CAN_alarm(const char *CAN_alarm_type, const char *CAN_alarm_module, const char *CAN_alarm_msg) {
    char payload[512];
    int ret;

    if (!g_mosq) {
        fprintf(stderr, "错误: MQTT客户端未初始化\n");
        return -1;
    }

    time_t now = time(NULL);
    char CAN_alarm_time[32];
    strftime(CAN_alarm_time, sizeof(CAN_alarm_time), "%Y-%m-%d %H:%M:%S", localtime(&now));

    snprintf(payload, sizeof(payload),
        "{\"services\":[{\"service_id\":\"%s\",\"properties\":{"
        "\"CAN_alarm_type\":\"%s\","
        "\"CAN_alarm_module\":\"%s\","
        "\"CAN_alarm_msg\":\"%s\","
        "\"CAN_alarm_time\":\"%s\"}}]}",
        cfg.service_id, CAN_alarm_type, CAN_alarm_module, CAN_alarm_msg, CAN_alarm_time);

    ret = mosquitto_publish(g_mosq, NULL, cfg.mqtt_topic, strlen(payload), payload, 1, false);
    if (ret != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "错误: CAN告警上报失败，错误码: %d\n", ret);
        return -1;
    }

    printf("成功: 已上报告警 - 类型: %s, 模块: %s, 时间: %s, 详情: %s\n", 
           CAN_alarm_type, CAN_alarm_module, CAN_alarm_time, CAN_alarm_msg);
    return 0;
}

void mqtt_disconnect_and_cleanup() {
    if (g_mosq) {
        mosquitto_disconnect(g_mosq);
        mosquitto_destroy(g_mosq);
        g_mosq = NULL;
    }
    mosquitto_lib_cleanup();
    printf("信息: MQTT 客户端已断开并清理资源。\n");
}

/**
 * @brief 发送命令执行响应给华为云
 */
void mqtt_send_command_response(const char *request_id, int result_code, const char *paras)
{
    if(!g_mosq || !request_id) return;

    char response_topic[256];
    char response_payload[128];

    snprintf(response_topic, sizeof(response_topic), 
             "$oc/devices/%s/sys/commands/response/request_id=%s", 
             cfg.mqtt_username, request_id);

    if(paras != NULL) {
        snprintf(response_payload, sizeof(response_payload),
                 "{\"result_code\":%d,\"paras\":%s}", result_code, paras);
    } else {
        snprintf(response_payload, sizeof(response_payload),
                 "{\"result_code\":%d}", result_code);
    }

    int ret = mosquitto_publish(g_mosq, NULL, response_topic, 
                                strlen(response_payload), response_payload, 1, false);
    if(ret == MOSQ_ERR_SUCCESS) {
        printf("命令响应已发送: %s\n", response_payload);
    } else {
        fprintf(stderr, "命令响应发送失败，错误码: %d\n", ret);
    }
}

// ====================== MQTT初始化 ======================
int mqtt_Init()    //to do 所有的错误处理都是怎么写的。
{
    int ret;

    mosquitto_lib_init();
    
    g_mosq = mosquitto_new("69fe6602cbb0cf6bb958fad5_TempHumi_0_0_2026080901", true, NULL);
    printf("【调试】mosquitto_new 设置的 Client ID = [%s]\n", cfg.mqtt_client_id);

    if (!g_mosq) {
        fprintf(stderr, "错误: 创建 MQTT 客户端失败\n");
        return -1;
    }

    ret = mosquitto_username_pw_set(g_mosq, cfg.mqtt_username, cfg.mqtt_password);
        printf("【调试】mosquitto_new 设置的 mqtt_username = [%s]\n", cfg.mqtt_username);
    printf("【调试】mosquitto_new 设置的cfg.mqtt_password = [%s]\n", cfg.mqtt_password);

    if (ret != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "错误: 设置用户名/密码失败，错误码: %d\n", ret);
        return -1;
    }

    // ★★★★★ 注册连接/断开回调 ★★★★★
    mosquitto_connect_callback_set(g_mosq, on_connect);
    mosquitto_disconnect_callback_set(g_mosq, on_disconnect);

    // ★★★★★ 注册下行消息回调 ★★★★★
    mosquitto_message_callback_set(g_mosq, mqtt_message_callback);

    // ★★★★★ 设置重连参数 ★★★★★
    mosquitto_reconnect_delay_set(g_mosq, 1, 60, true);
    // 含义: 初始等待1秒，最大等待60秒，启用指数退避

  

    // // 连接到华为云 MQTT Broker
    // ret = mosquitto_connect(g_mosq, cfg.mqtt_broker, cfg.mqtt_port, 60);
    // if (ret != MOSQ_ERR_SUCCESS) {
    //     fprintf(stderr, "错误: 连接失败，错误码: %d。\n", ret);
    //     fprintf(stderr, "请检查: 1. 主机地址和端口 2. 设备ID/密钥是否正确 3. 网络是否连通\n");
    //     printf("MQTT: 首次连接未成功，后台将自动重连\n");

    //     return -1;
    // }
  //实现断线重连的在这里mosquitto_loop_start
   // 你启动了一个独立的后台循环线程
    mosquitto_loop_start(g_mosq);
    // 这个线程会长期运行，负责网络IO、心跳和自动重连
  // ★★★ 关键修改：再发起异步连接（不阻塞）★★★
    ret = mosquitto_connect_async(g_mosq, cfg.mqtt_broker, cfg.mqtt_port, 60);
    if (ret != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "错误: 发起异步连接请求失败，错误码: %d。\n", ret);
        return -1;
    }

    printf("成功: MQTT客户端已使用密钥方式连接华为云 IoTDA 平台，开启指令监听\n");
    printf("MQTT: 重连机制已启用(初始延时1s, 最大60s, 指数退避)\n");

    // // 默认上电开启RTU采集
    // mgr.rtu_collect_enable = 1;
    // mgr.running = 1;

    return 0;
}

int MQTT_publish(float temperature, float humidity ,float press) {
    if (!g_mosq) {
        printf("MQTT not connected, skip publish\n");
        return -1;
    }
    mqtt_publish_data(temperature, humidity ,press);
    return 0;
}
// ====================== MQTT 上报：轮询所有设备 ======================
int mqtt_publish_data1(void)
{
    if (!g_mosq) {
        LOG_ERROR("MQTT客户端未初始化");
        return -1;
    }

    // ★ 局部数组
    int rtu_regs_copy[MAX_RTU_DEVICES][8];
    int rtu_read_count_copy[MAX_RTU_DEVICES];
    int tcp_regs_copy[MAX_TCP_DEVICES][8];
    int tcp_read_count_copy[MAX_TCP_DEVICES];
    int can_regs_copy[MAX_CAN_DEVICES][8];
    int can_read_count_copy[MAX_CAN_DEVICES];
    int rtu_count = 0, tcp_count = 0, can_count = 0;

    // ===== 1. 复制数据 =====
    pthread_rwlock_rdlock(&mgr.config_lock);

    // RTU
    rtu_count = mgr.rtu_device_count;
    for (int i = 0; i < rtu_count && i < MAX_RTU_DEVICES; i++) {
        rtu_read_count_copy[i] = mgr.rtu_devices[i].read_count;
        pthread_mutex_lock(&mgr.data_mutex);
        for (int j = 0; j < rtu_read_count_copy[i] && j < 8; j++) {
            rtu_regs_copy[i][j] = mgr.rtu_devices[i].regs[j];
        }
        pthread_mutex_unlock(&mgr.data_mutex);
    }

    // TCP
    tcp_count = mgr.tcp_device_count;
    for (int i = 0; i < tcp_count && i < MAX_TCP_DEVICES; i++) {
        tcp_read_count_copy[i] = mgr.tcp_devices[i].read_count;
        pthread_mutex_lock(&mgr.data_mutex);
        for (int j = 0; j < tcp_read_count_copy[i] && j < 8; j++) {
            tcp_regs_copy[i][j] = mgr.tcp_devices[i].regs[j];
        }
        pthread_mutex_unlock(&mgr.data_mutex);
    }

    // ★ CAN
    can_count = mgr.can_device_count;
    for (int i = 0; i < can_count && i < MAX_CAN_DEVICES; i++) {
        can_read_count_copy[i] = mgr.can_devices[i].dlc;
        pthread_mutex_lock(&mgr.data_mutex);
        for (int j = 0; j < can_read_count_copy[i] && j < 8; j++) {
            can_regs_copy[i][j] = mgr.can_devices[i].data[j];
        }
        pthread_mutex_unlock(&mgr.data_mutex);
    }

    pthread_rwlock_unlock(&mgr.config_lock);

    // ===== 2. 构建 JSON =====
    char payload[MAX_PAYLOAD_SIZE];
    int offset = 0;
    int first_prop = 1;

    offset += snprintf(payload + offset, sizeof(payload) - offset,
                       "{\"services\":[{\"service_id\":\"%s\",\"properties\":{",
                       cfg.service_id);

    // ===== 3. RTU：Modbus_RTU_%d_DATA%d =====
    for (int i = 0; i < rtu_count && i < MAX_RTU_DEVICES; i++) {
        for (int j = 0; j < rtu_read_count_copy[i] && j < 8; j++) {
            if (!first_prop) {
                offset += snprintf(payload + offset, sizeof(payload) - offset, ",");
            }
            offset += snprintf(payload + offset, sizeof(payload) - offset,
                               "\"Modbus_RTU_%d_DATA%d\":%d",
                               i, j, rtu_regs_copy[i][j]);
           // printf("RTU: Modbus_RTU_%d_DATA%d = %d\n", i, j, rtu_regs_copy[i][j]);
            first_prop = 0;
        }
    }

    // ===== 4. TCP：Modbus_TCP_%d_DATA%d =====
    for (int i = 0; i < tcp_count && i < MAX_TCP_DEVICES; i++) {
        for (int j = 0; j < tcp_read_count_copy[i] && j < 8; j++) {
            if (!first_prop) {
                offset += snprintf(payload + offset, sizeof(payload) - offset, ",");
            }
            offset += snprintf(payload + offset, sizeof(payload) - offset,
                               "\"Modbus_TCP_%d_DATA%d\":%d",
                               i, j, tcp_regs_copy[i][j]);
           // printf("TCP: Modbus_TCP_%d_DATA%d = %d\n", i, j, tcp_regs_copy[i][j]);
            first_prop = 0;
        }
    }

    // ===== ★ 5. CAN：CAN_%d_DATA%d =====
    for (int i = 0; i < can_count && i < MAX_CAN_DEVICES; i++) {
        for (int j = 0; j < can_read_count_copy[i] && j < 8; j++) {
            if (!first_prop) {
                offset += snprintf(payload + offset, sizeof(payload) - offset, ",");
            }
            offset += snprintf(payload + offset, sizeof(payload) - offset,
                               "\"CAN_%d_DATA%d\":%d",
                               i, j, can_regs_copy[i][j]);
         //   printf("CAN: CAN_%d_DATA%d = %d\n", i, j, can_regs_copy[i][j]);
            first_prop = 0;
        }
    }

    // ===== 6. 收尾 =====
    offset += snprintf(payload + offset, sizeof(payload) - offset,
                       "}}]}");

    // ===== 7. 发送 =====
    int ret = mosquitto_publish(g_mosq, NULL, cfg.mqtt_topic,
                                strlen(payload), payload, 0, false);
    if (ret != MOSQ_ERR_SUCCESS) {
        LOG_ERROR("MQTT发布失败，错误码: %d，数据存入本地缓存", ret);
        data_cache_push_telemetry_json(payload);
        return -1;
    }

    LOG_INFO("MQTT上报成功");
    return 0;
}
void build_current_json(char *payload, int payload_size)
{
    int offset = 0;
    int first_prop = 1;

    // 构造 JSON 开头
    offset += snprintf(payload + offset, payload_size - offset,
                       "{\"services\":[{\"service_id\":\"%s\",\"properties\":{",
                       cfg.service_id);

    // ===== 1. 轮询所有 RTU 设备 =====
    for (int i = 0; i < mgr.rtu_device_count; i++) {
        rtu_device_t *dev = &mgr.rtu_devices[i];
        pthread_mutex_lock(&mgr.data_mutex);
        for (int j = 0; j < dev->read_count && j < 8; j++) {
            if (!first_prop) {
                offset += snprintf(payload + offset, payload_size - offset, ",");
            }
            offset += snprintf(payload + offset, payload_size - offset,
                               "\"Modbus_RTU_%d_DATA%d\":%d",
                               i, j, dev->regs[j]);
            first_prop = 0;
        }
        pthread_mutex_unlock(&mgr.data_mutex);
    }

    // ===== 2. 轮询所有 TCP 设备 =====
    for (int i = 0; i < mgr.tcp_device_count; i++) {
        tcp_device_config_t *dev = &mgr.tcp_devices[i];
        pthread_mutex_lock(&mgr.data_mutex);
        for (int j = 0; j < 2 && j < 8; j++) {
            if (!first_prop) {
                offset += snprintf(payload + offset, payload_size - offset, ",");
            }
            offset += snprintf(payload + offset, payload_size - offset,
                               "\"Modbus_TCP_%d_DATA%d\":%d",
                               i, j, dev->regs[j]);
            first_prop = 0;
        }
        pthread_mutex_unlock(&mgr.data_mutex);
    }

    // 构造 JSON 结尾
    offset += snprintf(payload + offset, payload_size - offset, "}}]}");
}
// ====================== 判断MQTT是否在线 ======================
int mqtt_is_connected(void)
{
    return mqtt_connected_flag;
}
/**
 * @brief 通用告警上报（支持多设备）
 * @param device_type  "RTU", "TCP", "CAN"
 * @param device_index  设备索引（0, 1, 2...）
 * @param alarm_type    告警类型
 * @param alarm_module  告警模块
 * @param alarm_msg     告警消息
 */
int mqtt_publish_alarm(const char *device_type, int device_index,
                       const char *alarm_type, const char *alarm_module,
                       const char *alarm_msg)
{
    char payload[512];
    char prop_alarm_type[64];
    char prop_alarm_module[64];
    char prop_alarm_msg[64];
    char prop_alarm_time[64];
    int ret;

    if (!g_mosq) {
        fprintf(stderr, "错误: MQTT客户端未初始化\n");
        return -1;
    }

    time_t now = time(NULL);
    char alarm_time[32];
    strftime(alarm_time, sizeof(alarm_time), "%Y-%m-%d %H:%M:%S", localtime(&now));

    // ★★★ 根据设备类型+索引拼装属性名 ★★★
    // 例如：TCP_0_alarm_type, TCP_1_alarm_type, RTU_0_alarm_type
    snprintf(prop_alarm_type, sizeof(prop_alarm_type), "%s_%d_alarm_type", device_type, device_index);
    snprintf(prop_alarm_module, sizeof(prop_alarm_module), "%s_%d_alarm_module", device_type, device_index);
    snprintf(prop_alarm_msg, sizeof(prop_alarm_msg), "%s_%d_alarm_msg", device_type, device_index);
    snprintf(prop_alarm_time, sizeof(prop_alarm_time), "%s_%d_alarm_time", device_type, device_index);

    snprintf(payload, sizeof(payload),
        "{\"services\":[{\"service_id\":\"%s\",\"properties\":{"
        "\"%s\":\"%s\","
        "\"%s\":\"%s\","
        "\"%s\":\"%s\","
        "\"%s\":\"%s\"}}]}",
        cfg.service_id,
        prop_alarm_type, alarm_type,
        prop_alarm_module, alarm_module,
        prop_alarm_msg, alarm_msg,
        prop_alarm_time, alarm_time);

    ret = mosquitto_publish(g_mosq, NULL, cfg.mqtt_topic, strlen(payload), payload, 1, false);
    if (ret != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "错误: 告警上报失败，错误码: %d\n", ret);
        return -1;
    }

    printf("成功: 已上报告警 [%s_%d] - 类型: %s, 模块: %s, 时间: %s\n",
           device_type, device_index, alarm_type, alarm_module, alarm_time);
    return 0;
}