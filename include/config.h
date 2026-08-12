#ifndef CONFIG_H
#define CONFIG_H

// ====================== 多设备最大数量定义 ======================
#define MAX_TCP_DEVICES 4    // TCP设备最大数量
#define MAX_RTU_DEVICES 4  // RTU设备最大数量

typedef struct {
    // ===== RTU配置（单设备） =====
    char modbus_port[64];
    int  modbus_baudrate;
    int  modbus_slave_id;

    // ===== TCP配置（单设备，兼容旧代码） =====
    char modbus_ip[64];
    int  modbus_tcp_port;

    // ===== ★★★ TCP设备数组配置（新增） ★★★ =====
    // 每个设备有独立的 IP、端口、从站ID、起始地址、读取数量、使能开关
    // 这些字段会从配置文件中读取，格式：tcp_ip_0=192.168.2.1
    char tcp_ip[MAX_TCP_DEVICES][64];
    int  tcp_port[MAX_TCP_DEVICES];
    int  tcp_slave_id[MAX_TCP_DEVICES];
    int  tcp_read_addr[MAX_TCP_DEVICES];   // 起始寄存器地址
    int  tcp_read_count[MAX_TCP_DEVICES];  // 读取寄存器数量
    int  tcp_enable[MAX_TCP_DEVICES];      // 1=启用, 0=禁用

 // ★★★ RTU设备数组配置（新增） ★★★
    char rtu_port[MAX_RTU_DEVICES][64];
    int  rtu_baudrate[MAX_RTU_DEVICES];
    int  rtu_slave_id[MAX_RTU_DEVICES];
    int  rtu_read_addr[MAX_RTU_DEVICES];
    int  rtu_read_count[MAX_RTU_DEVICES];
    int  rtu_enable[MAX_RTU_DEVICES];      // 1=启用, 0=禁用
    // ===== CAN配置 =====
    char can_interface[16];

    // ===== MQTT配置 =====
    char mqtt_broker[64];
    int  mqtt_port;
    char mqtt_topic[256];
    char mqtt_client_id[256];
    char mqtt_username[256];
    char mqtt_password[256];

    char mqtt_cmd_topic[128];   // 下行命令订阅主题
    char service_id[64];        // 物模型服务ID
    char prop_temp[64];         // 温度属性名
    char prop_hum[64];          // 湿度属性名
    char prop_press[64];        // 气压属性名
    char prop_PAYLOAD_TEMPLATE[256];  
    // ===== 继电器配置 =====
    char relay_ip[64];
    int  relay_port;
    int  relay_slave_id;

} gateway_config_t;

extern gateway_config_t cfg;

int config_load(const char *filename, gateway_config_t *cfg);

#endif