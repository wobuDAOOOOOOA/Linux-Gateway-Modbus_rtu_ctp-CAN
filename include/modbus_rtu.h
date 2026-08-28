#ifndef _MODBUS_RTU_H_
#define _MODBUS_RTU_H_
#include <modbus/modbus.h>
#include <errno.h>

// modbus_t* modbus_RTU_bconnect(void);
// int modbus_rtu_robust_read(modbus_t **RTU_ctx,int addr, int nb, uint16_t *dest);
modbus_t* modbus_rtu_connect(const char *port, int baudrate, int slave_id);
int modbus_rtu_device_read(rtu_device_t *dev, int addr, int nb, uint16_t *dest);
int modbus_rtu_device_read_with_params(const char *port, int baudrate, int slave_id,
                                       int addr, int nb, modbus_t **ctx, uint16_t *dest);


extern  int8_t RTU_rc;
#endif