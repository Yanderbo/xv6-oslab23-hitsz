#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
/*
开两个管道
父进程往管道1里写数据p1[1]，从管道2中读数据p2[0]
子进程从管道1读数据p1[0]，从管道2写数据p2[1]
*/
int main(int argc, char *argv[])
{
    //创建两个通道
    int p1[2];
    int p2[2];
    pipe(p1);
    pipe(p2);
    //正常创建后，[1]为管道写入端，[0]为管道读出端

    int ret = fork();

    if(ret == 0) {
        //子进程
        char buffer[32];
        close(p1[1]);
        close(p2[0]);
        read(p1[0],buffer,4);
        close(p1[0]);
        printf("%d: received ping\n",getpid());
        write(p2[1],"pong", 4);
        close(p2[1]);
    } else { 
        //父进程
        char buffer[32];
        close(p1[0]);
        close(p2[1]);
        write(p1[1],"ping",4);
        close(p1[1]);
        read(p2[0],buffer,4);
        printf("%d: received pong\n",getpid());
        close(p2[0]);
    }
    exit(0);
}