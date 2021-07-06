#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./lock/locker.h"
#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

#define MAX_FD 65535        // 最大文件描述符
#define MAX_EVENT_NUMBER 10000      // 最大事件数

//这三个函数在http_conn.cpp中定义，改变链接属性
extern int addfd(int epollfd, int fd, bool one_shot);
extern int removed(int epollfd, int fd);

// 信号处理函数
void sig_handker(int sig)
{
    // 为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
}

// 设置信号函数
void addsig(int sig, void( handler )(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

// 打印错误信息函数
void show_error(int connfd, const char* info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char *argv[])
{
    if (argc <= 2)
    {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);

    // 忽略SIGPIPE信号
    addsig(SIGPIPE, SIG_IGN);

    // 创建线程池
    threadpool<http_conn> *pool = NULL;
    try
    {
        pool = new threadpool<http_conn>();
    }
    catch(...)
    {
        return 1;
    }

    // 预先为每个可能的客户连接分配一个http_conn对象
    http_conn *users = new http_conn[MAX_FD];
    assert(users);
    int user_count = 0;

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    struct linger tmp={1,0};
    // SO_LINGER若有数据待发送，延迟关闭
    setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);

    ret = listen(listenfd, 5);
    assert(ret >= 0);

    // 内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);

    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while (true)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR))
        {
            printf("epoll failure\n");
            break;
        }
        printf("epoll监听到的事件数量：%d\n", number);
        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            // 处理新到的客户连接
            if (sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
                if (connfd < 0)
                {
                    printf("errno is : %d\n", errno);
                    continue;
                }
                if (http_conn::m_user_count >= MAX_FD)
                {
                    show_error(connfd, "Internal server busy");
                    continue;
                }
                printf("客户端%s:%d连接成功\n", inet_ntoa(client_address.sin_addr), client_address.sin_port);
                // 初始化客户连接
                users[connfd].init(connfd, client_address);
            }
            // 处理异常情况
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                printf("出现了异常\n");
                // 如果有一场情况直接关闭客户连接
                users[sockfd].close_conn();
            }
            // 处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                printf("客户连接上有读事件\n");
                // 根据读的结果，决定将任务添加到线程池，还是关闭连接
                if (users[sockfd].read())
                {
                    printf("添加任务到线程池\n");
                    pool->append(users+sockfd);
                }
                else
                {
                    users[sockfd].close_conn();
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                // printf("客户连接上有写事件\n");
                // 根据写的结果，决定是否关闭连接
                if (!users[sockfd].write())
                {
                    users[sockfd].close_conn();
                }
            }
            else { }
        }
    }

    close(epollfd);
    close(listenfd);
    delete[] users;
    delete pool;
    return 0;



    
}

