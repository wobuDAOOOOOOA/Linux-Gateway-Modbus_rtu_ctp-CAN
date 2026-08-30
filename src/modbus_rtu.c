#include <stdio.h>
#include <modbus/modbus.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "log.h"
#include "config.h"
#include "gateway.h"

#define RTU_MAX_RETRY     3
#define RTU_BASE_DELAY    5

extern gateway_config_t cfg;
extern gateway_manager_t mgr;

// ====================== 连接函数 ======================
modbus_t* modbus_rtu_connect(const char *port, int baudrate, int slave_id)
{
    modbus_t *ctx = modbus_new_rtu(port, baudrate, 'N', 8, 1);
    if (ctx == NULL) {
        LOG_ERROR("RTU:创建上下文失败");
        return NULL;
    }

    modbus_set_response_timeout(ctx, 0, 500000);
    modbus_set_slave(ctx, slave_id);

    if (modbus_connect(ctx) == -1) {
        LOG_ERROR("RTU:连接失败 %s", modbus_strerror(errno));
        modbus_free(ctx);
        return NULL;
    }

    LOG_INFO("RTU:连接成功 (端口=%s, 波特率=%d, 从站=%d)", port, baudrate, slave_id);
    return ctx;
}

// ====================== 原版（保留，给不需要热加载的场景用） ======================
int modbus_rtu_device_read(rtu_device_t *dev, int addr, int nb, uint16_t *dest)
{
    int retry = 0;
    int rc;
    srand((unsigned)time(NULL) ^ (unsigned)pthread_self());

    while (retry < RTU_MAX_RETRY) {
        if (dev->ctx == NULL) {
            LOG_ERROR("RTU:句柄为空，第%d次重连, 设备从站=%d", retry + 1, dev->slave_id);
            dev->ctx = modbus_rtu_connect(dev->port, dev->baudrate, dev->slave_id);
            if (dev->ctx == NULL) {
                int delay = RTU_BASE_DELAY * (1 << retry);
                int jitter = rand() % 4 - 2;
                delay += jitter;
                if (delay < 1) delay = 1;

                retry++;
                if (retry < RTU_MAX_RETRY) {
                    LOG_ERROR("RTU:重连失败，等待%ds后重试", delay);
                    sleep(delay);
                }
                continue;
            }
        }

        rc = modbus_read_registers(dev->ctx, addr, nb, dest);
        if (rc != -1) {
            LOG_INFO("RTU:读取成功，获取%d个寄存器", rc);
            return rc;
        }

        LOG_ERROR("RTU:读取失败 err=%s，销毁连接准备重试，从站=%d", modbus_strerror(errno), dev->slave_id);
        modbus_close(dev->ctx);
        modbus_free(dev->ctx);
        dev->ctx = NULL;
        retry++;

        if (retry < RTU_MAX_RETRY) {
            int delay = RTU_BASE_DELAY * (1 << retry);
            int jitter = rand() % 4 - 2;
            delay += jitter;
            if (delay < 1) delay = 1;
            LOG_ERROR("RTU:第%d次重试，等待%ds", retry, delay);
            sleep(delay);
        }
    }

    LOG_ERROR("RTU:读取重试耗尽，最大重试次数%d", RTU_MAX_RETRY);
    return -1;
}

// ====================== ★★★ 新版：带显式参数（配合热加载 + 局部变量） ★★★ ======================
int modbus_rtu_device_read_with_params(const char *port, int baudrate, int slave_id,
                                       int addr, int nb, modbus_t **ctx, uint16_t *dest)
{
    int retry = 0;
    int rc;
    srand((unsigned)time(NULL) ^ (unsigned)pthread_self());

    while (retry < RTU_MAX_RETRY) {
        // 1. 句柄为空 → 重连（用传入的参数，不从 dev 读）
        if (*ctx == NULL) {
            LOG_ERROR("RTU:句柄为空，第%d次重连, 从站=%d", retry + 1, slave_id);
            *ctx = modbus_rtu_connect(port, baudrate, slave_id);
            if (*ctx == NULL) {
                int delay = RTU_BASE_DELAY * (1 << retry);
                int jitter = rand() % 4 - 2;
                delay += jitter;
                if (delay < 1) delay = 1;

                retry++;
                if (retry < RTU_MAX_RETRY) {
                    LOG_ERROR("RTU:重连失败，等待%ds后重试", delay);
                    sleep(delay);
                }
                continue;
            }
        }

        // 2. 执行读取
                pthread_mutex_lock(&mgr.rtu_bus_mutex);

        rc = modbus_read_registers(*ctx, addr, nb, dest);
                pthread_mutex_unlock(&mgr.rtu_bus_mutex);

        if (rc != -1) {
            LOG_INFO("RTU:读取成功，获取%d个寄存器", rc);
            return rc;
        }

        // 3. 读取失败，销毁连接
        LOG_ERROR("RTU:读取失败 err=%s，销毁连接，从站=%d", modbus_strerror(errno), slave_id);
        modbus_close(*ctx);
        modbus_free(*ctx);
        *ctx = NULL;
        retry++;

        if (retry < RTU_MAX_RETRY) {
            int delay = RTU_BASE_DELAY * (1 << retry);
            int jitter = rand() % 4 - 2;
            delay += jitter;
            if (delay < 1) delay = 1;
            LOG_ERROR("RTU:第%d次重试，等待%ds", retry, delay);
            sleep(delay);
        }
    }

    LOG_ERROR("RTU:所有重试耗尽，从站=%d 进入冷休眠", slave_id);
    return -1;
}