#ifndef _MQTT_HUAWEI_H_
#define _MQTT_HUAWEI_H_

#include <mosquitto.h>

// // 华为云设备固定参数
// #define MQTT_USERNAME   "69fe6602cbb0cf6bb958fad5_TempHumi"
// #define MQTT_PASSWORD   "cf059d90b349820ece3aa4487f92cd846758b82f5d4434fc078d117469b0364f"
 //#define MQTT_CLIENT_ID  "69fe6602cbb0cf6bb958fad5_TempHumi_0_0_2026070207"  //todo 这样到时候要改客户端id到配置里面

// 华为云下行命令订阅主题
#define MQTT_SUB_CMD_TOPIC  "$oc/devices/+/sys/command/#"

// 物模型上报字段
//#define SERVICE_ID      "Telemetry"
//#define PROP_TEMP       "temperature"
//#define PROP_HUM        "humidity"
//#define PROP_PRESS      "press"
#define PAYLOAD_TEMPLATE "{\"services\":[{\"service_id\":\"%s\",\"properties\":{\"%s\":%.1f,\"%s\":%.1f,\"%s\":%.1f}}]}"



// 基础MQTT接口
int mqtt_Init(void);
int mqtt_publish_data(float temperature, float humidity , float press);
void mqtt_disconnect_and_cleanup(void);

int MQTT_publish(float temperature, float humidity, float press);
//int mqtt_publish_TCP_alarm(const char *Modbus_TCP_alarm_type, const char *Modbus_TCP_alarm_module, const char *Modbus_TCP_alarm_msg);
//int mqtt_publish_RTU_alarm(const char *Modbus_RTU_alarm_type, const char *Modbus_RTU_alarm_module, const char *Modbus_RTU_alarm_msg);
int mqtt_publish_CAN_alarm(const char *CAN_alarm_type, const char *CAN_alarm_module, const char *CAN_alarm_msg);

void mosquitto_connect_callback_set(g_mosq, on_connect);
void mosquitto_disconnect_callback_set(g_mosq, on_disconnect);
// MQTT下行消息回调
void mqtt_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg);
// // 云端RTU启停控制接口
// void cloud_rtu_start(gateway_manager_t *mgr);
// void cloud_rtu_stop(gateway_manager_t *mgr);
int mqtt_publish_alarm(const char *device_type, int device_index,
                       const char *alarm_type, const char *alarm_module,
                       const char *alarm_msg);
#endif
