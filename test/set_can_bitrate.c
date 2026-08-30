#include <stdio.h>
#include <stdlib.h>
#include <libsocketcan.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("用法: %s <波特率>\n", argv[0]);
        return 1;
    }
    int bitrate = atoi(argv[1]);
    if (can_set_bitrate("can0", bitrate) != 0) {
        printf("设置波特率失败\n");
        return 1;
    }
    printf("can0 波特率已设置为 %d\n", bitrate);
    return 0;
}