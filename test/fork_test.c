#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main() {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        // 子进程
        printf("我是子进程，我的PID是%d，我的父进程PID是%d\n", getpid(), getppid());
        exit(0);
    } else {
        // 父进程
        printf("我是父进程，我的PID是%d，子进程PID是%d\n", getpid(), pid);
        wait(NULL);  // 回收子进程，防止僵尸进程
    }

    return 0;
}