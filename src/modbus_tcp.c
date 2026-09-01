#include <stdio.h>
#include <modbus/modbus.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "log.h"
#include "config.h"
#include "gateway.h"
#include "modbus_tcp.h"

#define TCP_MAX_RETRY     3
#define TCP_BASE_DELAY    5

extern gateway_config_t cfg;
extern gateway_manager_t mgr;

// ====================== 连接函数 ======================
modbus_t* modbus_tcp_connect(const char *ip, int port, int slave_id)
{
    modbus_t *ctx = modbus_new_tcp(ip, port);
    if (ctx == NULL) {
        LOG_ERROR("TCP:创建上下文失败 %s", modbus_strerror(errno));
        return NULL;
    }

    modbus_set_response_timeout(ctx, 0, 500000);
    modbus_set_slave(ctx, slave_id);

    if (modbus_connect(ctx) == -1) {
        LOG_ERROR("TCP:连接失败 %s", modbus_strerror(errno));
        modbus_free(ctx);
        return NULL;
    }

    LOG_INFO("TCP:连接成功 (%s:%d, 从站=%d)", ip, port, slave_id);
    return ctx;
}

// 

// ====================== ★★★ 新版：带显式参数（配合热加载 + 局部变量） ★★★ ======================
int modbus_tcp_device_read_with_params(const char *ip, int port, int slave_id,
                                       modbus_t **ctx, int addr, int nb, uint16_t *dest)
{
    int retry = 0;
    int rc;
    srand((unsigned)time(NULL) ^ (unsigned)pthread_self());

    while (retry < TCP_MAX_RETRY) {
        // 1. 句柄为空 → 重连（用传入的参数，不从 dev 读）
        if (*ctx == NULL) {
            LOG_ERROR("TCP:句柄为空，第%d次重连, 从站=%d", retry + 1, slave_id);
            *ctx = modbus_tcp_connect(ip, port, slave_id);
            if (*ctx == NULL) {
                int delay = TCP_BASE_DELAY * (1 << retry);
                int jitter = rand() % 4 - 2;
                delay += jitter;
                if (delay < 1) delay = 1;

                retry++;
                if (retry < TCP_MAX_RETRY) {
                    LOG_ERROR("TCP:重连失败，等待%ds后重试", delay);
                    sleep(delay);
                }
                continue;
            }
        }

        // 2. 执行读取
        rc = modbus_read_registers(*ctx, addr, nb, dest);
        if (rc != -1) {
            LOG_INFO("TCP:读取成功，获取%d个寄存器", rc);
            return rc;
        }

        // 3. 读取失败，销毁连接
        LOG_ERROR("TCP:读取异常 %s，销毁TCP连接, 从站=%d", modbus_strerror(errno), slave_id);
        modbus_close(*ctx);
        modbus_free(*ctx);
        *ctx = NULL;

        retry++;
        if (retry < TCP_MAX_RETRY) {
            int delay = TCP_BASE_DELAY * (1 << retry);
            int jitter = rand() % 4 - 2;
            delay += jitter;
            if (delay < 1) delay = 1;
            LOG_ERROR("TCP:第%d次重试，等待%ds", retry, delay);
            sleep(delay);
        }
    }

    LOG_ERROR("TCP:所有重试耗尽，从站=%d 进入冷休眠", slave_id);
    return -1;
}

// ====================== 原版（保留，给不需要热加载的场景用） ======================
// int modbus_tcp_device_read(tcp_device_config_t *dev_cfg, modbus_t **ctx,
//                            int addr, int nb, uint16_t *dest)
// {
//     int retry = 0;
//     int rc;
//     srand((unsigned)time(NULL) ^ (unsigned)pthread_self());

//     while (retry < TCP_MAX_RETRY) {
//         if (*ctx == NULL) {
//             LOG_ERROR("TCP:句柄为空，第%d次重连", retry + 1);
//             *ctx = modbus_tcp_connect(dev_cfg->ip, dev_cfg->port, dev_cfg->slave_id);
//             if (*ctx == NULL) {
//                 int delay = TCP_BASE_DELAY * (1 << retry);
//                 int jitter = rand() % 4 - 2;
//                 delay += jitter;
//                 if (delay < 1) delay = 1;

//                 retry++;
//                 if (retry < TCP_MAX_RETRY) {
//                     LOG_ERROR("TCP:重连失败，等待%ds后重试", delay);
//                     sleep(delay);
//                 }
//                 continue;
//             }
//         }

//         rc = modbus_read_registers(*ctx, addr, nb, dest);
//         if (rc != -1) {
//             LOG_INFO("TCP:读取成功，获取%d个寄存器", rc);
//             return rc;
//         }

//         LOG_ERROR("TCP:读取异常 %s，销毁TCP连接", modbus_strerror(errno));
//         modbus_close(*ctx);
//         modbus_free(*ctx);
//         *ctx = NULL;

//         retry++;
//         if (retry < TCP_MAX_RETRY) {
//             int delay = TCP_BASE_DELAY * (1 << retry);
//             int jitter = rand() % 4 - 2;
//             delay += jitter;
//             if (delay < 1) delay = 1;
//             LOG_ERROR("TCP:第%d次重试，等待%ds", retry, delay);
//             sleep(delay);
//         }
//     }

//     LOG_ERROR("TCP:所有重试耗尽，进入冷休眠状态");
//     return -1;
// }