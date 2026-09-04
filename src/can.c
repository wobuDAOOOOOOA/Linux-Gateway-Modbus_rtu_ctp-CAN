#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include "gateway.h" 
#include "log.h"
#include "config.h"
#include "can.h"  //can_status_t 定义

//extern gateway_config_t cfg;

static int s = -1;
static struct ifreq ifr;
static struct sockaddr_can addr;
static struct can_frame frame;


// ====================== CAN 状态（供外部读取） ======================
static can_status_t g_can_status = {0};




// /////////////
// /////////////
// // ===== 各类型处理函数 =====
//  void handle_rtu_frame(uint32_t can_id, struct can_frame *frame)
// {
//     int dev_idx = can_id - CAN_ID_BASE_RTU;
//     if (dev_idx >= 0 && dev_idx < mgr.rtu_device_count) {
//         pthread_mutex_lock(&mgr.data_mutex);
//         for (int i = 0; i < frame->can_dlc && i < 8; i++) {
//             mgr.rtu_devices[dev_idx].regs[i] = frame->data[i];
//             printf("RTU设备{%d} 存入数值：%d\n",dev_idx,mgr.rtu_devices[dev_idx].regs[i]);
                            
//         }
//         pthread_mutex_unlock(&mgr.data_mutex);
//     }
// }
//  void handle_tcp_frame(uint32_t can_id, struct can_frame *frame)
// {
//     int dev_idx = can_id - CAN_ID_BASE_TCP;
//     if (dev_idx >= 0 && dev_idx < mgr.tcp_device_count) {
//         pthread_mutex_lock(&mgr.data_mutex);
//         for (int i = 0; i < frame->can_dlc && i < 8; i++) {
//             mgr.tcp_devices[dev_idx].regs[i] = frame->data[i];
//         printf("TCP设备{%d} 存入数值：%d\n",dev_idx,mgr.tcp_devices[dev_idx].regs[i]);

//         }
//         pthread_mutex_unlock(&mgr.data_mutex);
//     }
// }
















// ====================== 内部状态更新函数 ======================
static void can_reset_stats(void)
{
    g_can_status.is_in_fault = 0;
    g_can_status.total_reconnect_count = 0;
    g_can_status.first_fail_time = 0;
    snprintf(g_can_status.last_alarm_msg, sizeof(g_can_status.last_alarm_msg), "CAN正常");
    LOG_INFO("CAN:状态已重置");
}

static void can_mark_fault_start(void)
{
    time_t now = time(NULL);
    int duration = 0;

    if (g_can_status.is_in_fault == 0) {
        g_can_status.first_fail_time = now;
        g_can_status.is_in_fault = 1;
        g_can_status.total_reconnect_count = 0;
        LOG_WARN("CAN:=== 进入故障状态 ===");
        LOG_WARN("CAN:首次故障时间: %s", ctime(&now));
    }

    g_can_status.total_reconnect_count++;
    duration = (int)(time(NULL) - g_can_status.first_fail_time);

    snprintf(g_can_status.last_alarm_msg, sizeof(g_can_status.last_alarm_msg),
             "CAN故障中: 重连%d次, 持续%d秒(约%.1f分钟)",
             g_can_status.total_reconnect_count,
             duration,
             duration / 60.0);

    // ★★★ 用 LOG_ERROR 打印详细状态 ★★★
    LOG_ERROR("CAN:【故障状态】重连次数=%d, 已持续=%d秒(%.1f分钟), 消息=%s",
              g_can_status.total_reconnect_count,
              duration,
              duration / 60.0,
              g_can_status.last_alarm_msg);
}

static void can_mark_fault_end(void)
{
    if (g_can_status.is_in_fault == 1) {
        int duration = (int)(time(NULL) - g_can_status.first_fail_time);
        int total_retry = g_can_status.total_reconnect_count;

        g_can_status.is_in_fault = 0;
        snprintf(g_can_status.last_alarm_msg, sizeof(g_can_status.last_alarm_msg),
                 "CAN已恢复, 本次故障重连%d次, 持续%d秒",
                 total_retry, duration);

        // ★★★ 用 LOG_ERROR 打印恢复信息 ★★★
        LOG_ERROR("CAN:【恢复状态】总重连=%d次, 总持续=%d秒(%.1f分钟), 消息=%s",
                  total_retry,
                  duration,
                  duration / 60.0,
                  g_can_status.last_alarm_msg);

        LOG_INFO("CAN:故障恢复，总重连次数 %d", total_retry);

        // 重置统计，避免重复上报
        g_can_status.total_reconnect_count = 0;
        g_can_status.first_fail_time = 0;
    }
}

// ====================== 获取CAN状态（供外部调用） ======================
void can_get_status(can_status_t *status)
{
    if (status) {
        memcpy(status, &g_can_status, sizeof(can_status_t));
    }
}

// ====================== CAN 初始化 ======================
int can_Init(void)
{
    LOG_INFO("CAN:初始化接口 %s", cfg.can_interface);

    can_reset_stats();

    if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        LOG_ERROR("CAN:创建socket失败");
        return -1;
    }

    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    strcpy(ifr.ifr_name, cfg.can_interface);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        LOG_ERROR("CAN:获取接口索引失败");
        close(s);
        s = -1;
        return -1;
    }

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("CAN:bind失败");
        close(s);
        s = -1;
        return -1;
    }

    int recv_own_msgs = 1;
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &recv_own_msgs, sizeof(recv_own_msgs));
    LOG_INFO("CAN:初始化完成，自收发已开启");

    return 0;
}

// ====================== CAN 重连 ======================
static int can_reinit(void)
{
    LOG_INFO("CAN:尝试重新初始化...");

    can_mark_fault_start();

    if (s >= 0) {
        close(s);
        s = -1;
    }

    if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        LOG_ERROR("CAN:重连socket创建失败");
        return -1;
    }

    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    strcpy(ifr.ifr_name, cfg.can_interface);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        LOG_ERROR("CAN:重连获取接口索引失败");
        close(s);
        s = -1;
        return -1;
    }

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("CAN:重连bind失败");
        close(s);
        s = -1;
        return -1;
    }

    int recv_own_msgs = 1;
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &recv_own_msgs, sizeof(recv_own_msgs));

    can_mark_fault_end();
    LOG_INFO("CAN:重连成功，自收发已开启");

    return 0;
}

// ====================== CAN 发送 ======================
int can_send(uint16_t id, uint16_t dlc, unsigned short *data)
{
    int retry = 0;
    struct can_frame tx_frame;
    srand((unsigned)time(NULL));

    while (retry < CAN_MAX_RETRY) {
        if (s < 0) {
            LOG_ERROR("CAN:socket无效，尝试重连");
            if (can_reinit() < 0) {
                int delay = CAN_BASE_DELAY * (1 << retry);
                int jitter = rand() % 4 - 2;
                delay += jitter;
                if (delay < 1) delay = 1;
                retry++;
                if (retry < CAN_MAX_RETRY) {
                    LOG_ERROR("CAN:重连失败，等待%ds", delay);
                    sleep(delay);
                }
                continue;
            }
        }

        tx_frame.can_id = id;
        tx_frame.can_dlc = dlc;
        for (int i = 0; i < dlc; i++) {
            tx_frame.data[i] = data[i];
        }

        if (write(s, &tx_frame, sizeof(tx_frame)) == sizeof(tx_frame)) {
            LOG_INFO("CAN:发送成功 ID=0x%x, DLC=%d  data:%D", id, dlc,data[0]);
            return 0;
        }

        LOG_ERROR("CAN:发送失败，尝试重连");
        if (s >= 0) {
            close(s);
            s = -1;
        }

        retry++;
        if (retry < CAN_MAX_RETRY) {
            int delay = CAN_BASE_DELAY * (1 << retry);
            int jitter = rand() % 4 - 2;
            delay += jitter;
            if (delay < 1) delay = 1;
            LOG_ERROR("CAN:等待%ds后重试", delay);
            sleep(delay);
        }
    }

    LOG_ERROR("CAN:发送失败，重试耗尽");
    return -1;
}

// ====================== CAN 接收 ======================
int can_receive(uint32_t *can_id, unsigned char *buffer, int *len)
{
    if (s < 0) {
        LOG_WARN("CAN:接收时socket无效");
        can_reinit();
        return -1;
    }

        int nbytes = read(s, &frame, sizeof(frame));

    if (nbytes < 0) {
        switch (errno) {
            case EAGAIN:
          //  case EWOULDBLOCK:
                return 1;  // 没有数据，正常

            case EINTR:
                return 1;  // 被信号中断，上层继续读

            case EBADF:
                LOG_ERROR("CAN: socket 无效，触发重连");
                can_reinit();
                return -1;

            case ENETDOWN:
            case ENETUNREACH:
                LOG_ERROR("CAN: 网络不可用，触发重连");
                can_reinit();
                return -1;

            default:
                LOG_ERROR("CAN: 未知错误 %d (%s)，触发重连", errno, strerror(errno));
                can_reinit();
                return -1;
        }
    }

    if (nbytes == sizeof(frame)) {
        // 收到数据，如果当前是故障状态则标记恢复
        if (g_can_status.is_in_fault) {
            can_mark_fault_end();
        }
        
        // ★★★ 把 can_id 传出去 ★★★
        if (can_id) {
            *can_id = frame.can_id;
        }
        
        if (len) {
            *len = frame.can_dlc;
        }
        
        if (buffer) {
            for (int i = 0; i < frame.can_dlc && i < 8; i++) {
                buffer[i] = frame.data[i];
            }
        }
        
        LOG_DEBUG("CAN:收到 ID=0x%x, DLC=%d, data:%d", 
                  frame.can_id, frame.can_dlc, frame.data[0]);
        return 0;
    }

    return 1;
}

// ====================== CAN 清理 ======================
void can_cleanup(void)
{
    if (s >= 0) {
        close(s);
        s = -1;
        LOG_INFO("CAN:资源已清理");
    }
}
