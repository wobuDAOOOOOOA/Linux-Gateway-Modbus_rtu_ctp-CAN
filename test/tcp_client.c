#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "192.168.2.100"
#define SERVER_PORT 5020

int main() {
    int sock;   
    struct sockaddr_in server_addr;
    char buf[256];
    int ret;

    // 1. 创建 socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }
    printf("1. socket 创建成功\n");

    // 2. 连接服务端
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        return 1;
    }
    printf("2. 已连接到服务端 %s:%d\n", SERVER_IP, SERVER_PORT);

    // 3. 发送数据
    send(sock, "hello from client\n", 18, 0);
    printf("3. 已发送数据\n");

    // 4. 接收回复
    memset(buf, 0, sizeof(buf));
    ret = recv(sock, buf, sizeof(buf) - 1, 0);
    if (ret > 0) {
        printf("4. 收到服务端回复: %s\n", buf);
    }

    // 5. 关闭
    close(sock);
    printf("5. 连接关闭\n");
    return 0;
}