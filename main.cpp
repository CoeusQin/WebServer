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
#define TIMESLOT 5             //最小超时单位

//这三个函数在http_conn.cpp中定义，改变链接属性
extern int addfd(int epollfd, int fd, bool one_shot);
extern int removed(int epollfd, int fd);
extern int setnonblocking(int fd);

//设置定时器相关参数
static int pipefd[2];
// static sort_timer_lst timer_lst;

//信号处理函数
void sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    //可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;
    //将信号值从管道写端写入，传输字符类型，而非整型
    send(pipefd[1], (char *)&msg, 1, 0);
    //将原来的errno赋值为当前的errno
    errno = save_errno;
}

// 设置信号函数
void addsig(int sig, void( handler )(int), bool restart = true)
{
    //创建sigaction结构体变量
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));

    //信号处理函数中仅仅发送信号值，不做对应逻辑处理
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
        //将所有信号添加到信号集中
    sigfillset(&sa.sa_mask);

    //执行sigaction函数
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
    // struct linger tmp={1,0};
    // // SO_LINGER若有数据待发送，延迟关闭
    // setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));

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

    //创建管道套接字
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);

    //设置管道写端为非阻塞
    // send是将信息发送给套接字缓冲区，
    // 如果缓冲区满了，则会阻塞，这时候会进一步增加信号处理函数的执行时间，为此，将其修改为非阻塞。
    setnonblocking(pipefd[1]);

    // 设置管道读端为ET非阻塞
    addfd(epollfd, pipefd[0], false);

    // 传递给主循环的信号值，此处只关注SIGALRM和SIGTERM
    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);

    // 循环条件
    bool stop_server = false;

    // 超时标识
    bool timeout = false;

    // 每隔TIMESLOT时间出发SIGALRM
    alarm(TIMESLOT);

    while (!stop_server)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR))
        {
            printf("epoll failure\n");
            break;
        }
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
            // 处理信号
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                //从管道读端读出信号值，成功返回字节数，失败返回-1
                //正常情况下，这里的ret返回值总是1，只有14和15两个ASCII码对应的字符
                ret = recv(pipefd[0], signals, sizeof(signals), 0);

                if (ret == -1)
                {
                    // handle the error
                    continue;
                }
                else if (ret == 0)
                {
                    continue;
                }
                else
                {
                    // 处理信号值对应的逻辑，因此此处只关注SIGALRM和SIGTERM
                    for (int i = 0; i < ret; i++)
                    {
                        switch (signals[i])
                        {
                            case SIGALRM:
                            {
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                            }
                        }
                    }
                }

            }
            // 处理异常情况
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 如果有一场情况直接关闭客户连接
                users[sockfd].close_conn();
            }
            // 处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                // 根据读的结果，决定将任务添加到线程池，还是关闭连接
                if (users[sockfd].read())
                {
                    pool->append(users+sockfd);
                }
                else
                {
                    users[sockfd].close_conn();
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
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

