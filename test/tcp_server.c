#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 5020

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buf[256];
    int ret;

    // 1. 创建 socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    printf("1. socket 创建成功\n");

    // 2. 绑定 IP 和端口
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;  // 监听所有网卡
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        return 1;
    }
    printf("2. bind 绑定 0.0.0.0:%d 成功\n", PORT);

    // 3. 开始监听
    if (listen(server_fd, 1) < 0) {
        perror("listen");
        return 1;
    }
    printf("3. listen 监听中，等待客户端连接...\n");

    // 4. 接受客户端连接（阻塞等待）
    client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        perror("accept");
        return 1;
    }
    printf("4. accept 客户端已连接: %s:%d\n", 
           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    // 5. 收发数据
    while (1) {
        memset(buf, 0, sizeof(buf));
        ret = recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (ret <= 0) {
            printf("客户端断开连接\n");
            break;
        }
        printf("收到: %s\n", buf);

        // 回复客户端
        send(client_fd, "hello from rv1106\n", 19, 0);
    }

    // 6. 关闭连接
    close(client_fd);
    close(server_fd);
    printf("服务端关闭\n");
    return 0;
}