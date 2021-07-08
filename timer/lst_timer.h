#ifndef _LST_TIMER_H_
#define _LST_TIMER_H_

#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUFFER_SIZE 64
//连接资源结构体成员需要用到定时器类
class util_timer;   // 前置声明

// 连接资源
struct client_data
{
    // 客户端socket地址
    sockaddr_in address;

    // socket文件描述符
    int sockfd;

    // 读缓存
    char buf[BUFFER_SIZE];

    // 定时器
    util_timer* timer;
};

// 定时器类
class util_timer
{
public:
    util_timer() : prev( NULL ), next( NULL ){}

public:
    // 超时时间
    time_t expire; 
    // 任务回调函数
    void (*cb_func)( client_data* );
    // 连接资源，由定时器的执行者传递给回调函数
    client_data* user_data;
    // 指向前一个定时器
    util_timer* prev;
    // 指向后一个定时器
    util_timer* next;
};

// 定时器链表
class sort_timer_lst
{
public:
    sort_timer_lst() : head( NULL ), tail( NULL ) {}
    //常规销毁链表
    ~sort_timer_lst()
    {
        util_timer* tmp = head;
        while( tmp )
        {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }

    //添加定时器，内部调用私有成员add_timer
    void add_timer( util_timer* timer )
    {
        if( !timer )
        {
            return;
        }
        if( !head )
        {
            head = tail = timer;
            return; 
        }
        //如果新的定时器超时时间小于当前头部结点
        //直接将当前定时器结点作为头部结点
        if( timer->expire < head->expire )
        {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        //否则调用私有成员，调整内部结点
        add_timer( timer, head );
    }

    //调整定时器，任务发生变化时，调整定时器在链表中的位置
    void adjust_timer( util_timer* timer )
    {
        if( !timer )
        {
            return;
        }
        util_timer* tmp = timer->next;
        //被调整的定时器在链表尾部
        //定时器超时值仍然小于下一个定时器超时值，不调整
        if( !tmp || ( timer->expire < tmp->expire ) )
        {
            return;
        }

        //被调整定时器是链表头结点，将定时器取出，重新插入
        if( timer == head )
        {
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;
            add_timer( timer, head );
        }
        //被调整定时器在内部，将定时器取出，重新插入
        else
        {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer( timer, timer->next );
        }
    }

    // 删除定时器
    void del_timer( util_timer* timer )
    {
        if( !timer )
        {
            return;
        }

        //链表中只有一个定时器，需要删除该定时器
        if( ( timer == head ) && ( timer == tail ) )
        {
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }

        //被删除的定时器为头结点
        if( timer == head )
        {
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }

        // 被删除的定时器为尾结点
        if( timer == tail )
        {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }

        // 被删除的定时器在链表内部
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }

    // SIGALRM信号每次被触发就在其信号处理函数（如果使用统一事件源，则是主函数）中执行一次tick函数
    // 以处理链表上到期的任务
    void tick()
    {
        if( !head )
        {
            return;
        }
        // printf( "timer tick\n" );

        //获取系统当前时间
        time_t cur = time( NULL );
        util_timer* tmp = head;

        //遍历定时器链表
        while( tmp )
        {
            //链表容器为升序排列
            //当前时间小于定时器的超时时间，后面的定时器也没有到期
            if( cur < tmp->expire )
            {
                break;
            }

            //当前定时器到期，则调用回调函数，执行定时事件
            tmp->cb_func( tmp->user_data );

            //将处理后的定时器从链表容器中删除，并重置头结点
            head = tmp->next;
            if( head )
            {
                head->prev = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    //私有成员，被公有成员add_timer和adjust_time调用
    //主要用于调整链表内部结点
    void add_timer( util_timer* timer, util_timer* lst_head )
    {
        util_timer* prev = lst_head;
        util_timer* tmp = prev->next;
        //遍历当前结点之后的链表，按照超时时间找到目标定时器对应的位置，常规双向链表插入操作
        while( tmp )
        {
            if( timer->expire < tmp->expire )
            {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }

        // 没找到比目标定时器时间长的节点，放到链表的尾部
        if( !tmp )
        {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
        
    }

private:
    // 首尾节点
    util_timer* head;
    util_timer* tail;
};

#endif