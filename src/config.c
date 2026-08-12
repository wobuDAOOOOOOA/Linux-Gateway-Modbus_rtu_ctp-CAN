#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

gateway_config_t cfg;

void config_set_default(gateway_config_t *cfg_ptr)
{
    // RTU默认配置
    strcpy(cfg_ptr->modbus_port, "/dev/ttyS3");
    cfg_ptr->modbus_baudrate = 4800;
    cfg_ptr->modbus_slave_id = 1;

    // TCP默认配置
    strcpy(cfg_ptr->modbus_ip, "192.168.2.1");
    cfg_ptr->modbus_tcp_port = 5020;


    // ===== ★★★ TCP设备数组默认配置（新增） ★★★ =====
    // 设备0：默认启用，使用上面的IP/端口
    strcpy(cfg_ptr->tcp_ip[0], "192.168.2.1");
    cfg_ptr->tcp_port[0] = 5020;
    cfg_ptr->tcp_slave_id[0] = 1;
    cfg_ptr->tcp_read_addr[0] = 0;
    cfg_ptr->tcp_read_count[0] = 10;
    cfg_ptr->tcp_enable[0] = 1;   // 启用

    // 设备1：默认启用，使用另一个IP（示例）
    strcpy(cfg_ptr->tcp_ip[1], "192.168.2.1");
    cfg_ptr->tcp_port[1] = 5020;
    cfg_ptr->tcp_slave_id[1] = 1;
    cfg_ptr->tcp_read_addr[1] = 0;
    cfg_ptr->tcp_read_count[1] = 10;
    cfg_ptr->tcp_enable[1] = 1;   // 默认禁用，需要手动在配置文件中改为1

    // 设备2、3：默认全部禁用
    for (int i = 2; i < MAX_TCP_DEVICES; i++) {
        cfg_ptr->tcp_enable[i] = 0;
        cfg_ptr->tcp_ip[i][0] = '\0';
        cfg_ptr->tcp_port[i] = 5020;
        cfg_ptr->tcp_slave_id[i] = 1;
        cfg_ptr->tcp_read_addr[i] = 0;
        cfg_ptr->tcp_read_count[i] = 10;
    }


    // CAN默认配置
    strcpy(cfg_ptr->can_interface, "vcan0");

    // MQTT默认配置
    strcpy(cfg_ptr->mqtt_broker, "117.78.5.125");
    cfg_ptr->mqtt_port = 1883;
    strcpy(cfg_ptr->mqtt_topic, "$oc/devices/69fe6602cbb0cf6bb958fad5_TempHumi/sys/properties/report");
    strcpy(cfg_ptr->mqtt_client_id, "69fe6602cbb0cf6bb958fad5_TempHumi_0_0_2026080900");
    strcpy(cfg_ptr->mqtt_username, "69fe6602cbb0cf6bb958fad5_TempHumi");
    strcpy(cfg_ptr->mqtt_password, "70a70b5b04c6f8492368d0dd380297d1c9e953083b7c37e1eca8f08cc66b0f55");   //这个才是密码
    //服务名 topic格式 属性名
    strcpy(cfg_ptr->mqtt_cmd_topic, "$oc/devices/+/sys/command/#");
    strcpy(cfg_ptr->service_id, "Telemetry");
    strcpy(cfg_ptr->prop_temp, "temperature");
    strcpy(cfg_ptr->prop_hum, "humidity");
    strcpy(cfg_ptr->prop_press, "press");


    // ★★★ 继电器默认配置（新增） ★★★
    strcpy(cfg_ptr->relay_ip, "192.168.0.10");
    cfg_ptr->relay_port = 502;
    cfg_ptr->relay_slave_id = 1;


    for (int i = 0; i < MAX_RTU_DEVICES; i++) {
    if (i == 0) {
        strcpy(cfg_ptr->rtu_port[i], "/dev/ttyS3");
        cfg_ptr->rtu_baudrate[i] = 4800;
        cfg_ptr->rtu_slave_id[i] = 1;
        cfg_ptr->rtu_read_addr[i] = 0;
        cfg_ptr->rtu_read_count[i] = 2;
        cfg_ptr->rtu_enable[i] = 1;
    } else {
        cfg_ptr->rtu_enable[i] = 0;  // 默认禁用
        cfg_ptr->rtu_port[i][0] = '\0';
    }
}
}

int config_load(const char *filename, gateway_config_t *cfg_ptr)
{
    config_set_default(cfg_ptr);

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("config file not found, use default config\n");
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '#' || line[0] == '\0')
            continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *value = eq + 1;

        while (*key == ' ' || *key == '\t') key++;
        while (*value == ' ' || *value == '\t') value++;

        char *end = key + strlen(key) - 1;
        while (end >= key && (*end == ' ' || *end == '\t')) { *end = '\0'; end--; }
        end = value + strlen(value) - 1;
        while (end >= value && (*end == ' ' || *end == '\t')) { *end = '\0'; end--; }

        // RTU配置
        if (strcmp(key, "modbus_port") == 0) strcpy(cfg_ptr->modbus_port, value);
        else if (strcmp(key, "modbus_baudrate") == 0) cfg_ptr->modbus_baudrate = atoi(value);
        else if (strcmp(key, "modbus_slave_id") == 0) cfg_ptr->modbus_slave_id = atoi(value);

        // TCP单设备配置
        else if (strcmp(key, "modbus_ip") == 0) strcpy(cfg_ptr->modbus_ip, value);
        else if (strcmp(key, "modbus_tcp_port") == 0) cfg_ptr->modbus_tcp_port = atoi(value);

        // CAN配置
        else if (strcmp(key, "can_interface") == 0) strcpy(cfg_ptr->can_interface, value);

        // MQTT配置
        else if (strcmp(key, "mqtt_broker") == 0) strcpy(cfg_ptr->mqtt_broker, value);
        else if (strcmp(key, "mqtt_port") == 0) cfg_ptr->mqtt_port = atoi(value);
        else if (strcmp(key, "mqtt_topic") == 0) strcpy(cfg_ptr->mqtt_topic, value);
        else if (strcmp(key, "mqtt_client_id") == 0) strcpy(cfg_ptr->mqtt_client_id, value);
        else if (strcmp(key, "mqtt_username") == 0) strcpy(cfg_ptr->mqtt_username, value);
        else if (strcmp(key, "mqtt_password") == 0) strcpy(cfg_ptr->mqtt_password, value);
          //MQTT服务名 上报topic 属性名等
        else if (strcmp(key, "mqtt_cmd_topic") == 0) strcpy(cfg_ptr->mqtt_cmd_topic, value);
        else if (strcmp(key, "service_id") == 0) strcpy(cfg_ptr->service_id, value);
        else if (strcmp(key, "prop_temp") == 0) strcpy(cfg_ptr->prop_temp, value);
        else if (strcmp(key, "prop_hum") == 0) strcpy(cfg_ptr->prop_hum, value);
        else if (strcmp(key, "prop_press") == 0) strcpy(cfg_ptr->prop_press, value);


        // ★★★ 继电器配置 ★★★
        else if (strcmp(key, "relay_ip") == 0) strcpy(cfg_ptr->relay_ip, value);
        else if (strcmp(key, "relay_port") == 0) cfg_ptr->relay_port = atoi(value);
        else if (strcmp(key, "relay_slave_id") == 0) cfg_ptr->relay_slave_id = atoi(value);

        // ★★★ TCP设备数组配置（新增） ★★★
        for (int i = 0; i < MAX_TCP_DEVICES; i++) {
            char key_ip[32], key_port[32], key_slave[32], key_addr[32], key_count[32], key_enable[32];
            snprintf(key_ip, sizeof(key_ip), "tcp_ip_%d", i);
            snprintf(key_port, sizeof(key_port), "tcp_port_%d", i);
            snprintf(key_slave, sizeof(key_slave), "tcp_slave_id_%d", i);
            snprintf(key_addr, sizeof(key_addr), "tcp_read_addr_%d", i);
            snprintf(key_count, sizeof(key_count), "tcp_read_count_%d", i);
            snprintf(key_enable, sizeof(key_enable), "tcp_enable_%d", i);

            if (strcmp(key, key_ip) == 0) {
                strcpy(cfg_ptr->tcp_ip[i], value);
            } else if (strcmp(key, key_port) == 0) {
                cfg_ptr->tcp_port[i] = atoi(value);
            } else if (strcmp(key, key_slave) == 0) {
                cfg_ptr->tcp_slave_id[i] = atoi(value);
            } else if (strcmp(key, key_addr) == 0) {
                cfg_ptr->tcp_read_addr[i] = atoi(value);
            } else if (strcmp(key, key_count) == 0) {
                cfg_ptr->tcp_read_count[i] = atoi(value);
            } else if (strcmp(key, key_enable) == 0) {
                cfg_ptr->tcp_enable[i] = atoi(value);
            }
        }
        // config_load 中添加解析（参照 TCP 写法）
for (int i = 0; i < MAX_RTU_DEVICES; i++) {
    char key_port[32], key_baud[32], key_slave[32], key_addr[32], key_count[32], key_enable[32];
    snprintf(key_port, sizeof(key_port), "rtu_port_%d", i);
    snprintf(key_baud, sizeof(key_baud), "rtu_baudrate_%d", i);
    snprintf(key_slave, sizeof(key_slave), "rtu_slave_id_%d", i);
    snprintf(key_addr, sizeof(key_addr), "rtu_read_addr_%d", i);
    snprintf(key_count, sizeof(key_count), "rtu_read_count_%d", i);
    snprintf(key_enable, sizeof(key_enable), "rtu_enable_%d", i);

    if (strcmp(key, key_port) == 0) strcpy(cfg_ptr->rtu_port[i], value);
    else if (strcmp(key, key_baud) == 0) cfg_ptr->rtu_baudrate[i] = atoi(value);
    else if (strcmp(key, key_slave) == 0) cfg_ptr->rtu_slave_id[i] = atoi(value);
    else if (strcmp(key, key_addr) == 0) cfg_ptr->rtu_read_addr[i] = atoi(value);
    else if (strcmp(key, key_count) == 0) cfg_ptr->rtu_read_count[i] = atoi(value);
    else if (strcmp(key, key_enable) == 0) cfg_ptr->rtu_enable[i] = atoi(value);


}
    }

    fclose(fp);
    return 0;
}