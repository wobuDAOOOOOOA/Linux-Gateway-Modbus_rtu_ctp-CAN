#ifndef CAN_H
#define CAN_H

#include <stdint.h>
#include <time.h>

// ====================== CAN 状态定义 ======================
typedef struct {
    int is_in_fault;              // 0=正常, 1=故障中
    int total_reconnect_count;    // 本次故障累计重连次数
    time_t first_fail_time;       // 首次故障时间
    char last_alarm_msg[256];     // 最新的告警消息字符串
} can_status_t;

int can_Init(void);
int can_send(uint16_t id, uint16_t dlc, unsigned short *data);
int can_receive(uint32_t *can_id, unsigned char *buffer, int *len);
void can_cleanup(void);
// ★★★ 获取当前CAN状态（供MQTT线程调用） ★★★
void can_get_status(can_status_t *status);



//typedef void (*can_frame_handler_t)(uint32_t can_id, struct can_frame *frame);////
// //要接入真实can设备的话这里就直接不用了
// /////////////
//  void handle_rtu_frame(uint32_t can_id, struct can_frame *frame);
//  void handle_tcp_frame(uint32_t can_id, struct can_frame *frame);
// /// @brief ////////
// typedef struct {
//     uint32_t base;
//     uint32_t max;
//     can_frame_handler_t handler;
// } can_route_entry_t;
// /// @brief ////////
// static const can_route_entry_t route_table[] = {
//     {CAN_ID_BASE_RTU,   CAN_ID_BASE_RTU + MAX_RTU_DEVICES,   handle_rtu_frame},
//     {CAN_ID_BASE_TCP,   CAN_ID_BASE_TCP + MAX_TCP_DEVICES,   handle_tcp_frame},
// };
// #define ROUTE_TABLE_SIZE (sizeof(route_table) / sizeof(route_table[0]))
#define CAN_MAX_RETRY     3
#define CAN_BASE_DELAY    5
//#define CAN_ALARM_INTERVAL 30
// ////////////






#endif