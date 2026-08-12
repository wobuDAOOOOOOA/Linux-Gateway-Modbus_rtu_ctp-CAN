# 交叉编译工具链
CC = arm-rockchip830-linux-uclibcgnueabihf-gcc

# 目录
SRC_DIR = src
INC_DIR = include
LIB_DIR = /home/wdz/libmodbus_arm/lib
MOSQ_DIR = /home/wdz/mosquitto-2.0.18/lib

# 源文件
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/modbus_rtu.c \
       $(SRC_DIR)/modbus_tcp.c \
       $(SRC_DIR)/can.c \
       $(SRC_DIR)/mqtt_huawei.c \
       $(SRC_DIR)/config.c \
       $(SRC_DIR)/relay.c  \
       $(SRC_DIR)/bmp280.c \
       $(SRC_DIR)/data_cache.c \
       


# 输出
TARGET = gateway

# 编译选项
CFLAGS = -Wall -g -O0 -I$(INC_DIR) -I/home/wdz/libmodbus_arm/include -I/home/wdz/mosquitto-2.0.18/include
LDFLAGS = -L$(LIB_DIR) -L$(MOSQ_DIR) -lmodbus -lmosquitto -lpthread

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean