#ifndef __THREADPOOL_H__
#define __THREADPOOL_H__

#include "../lock/locker.h"

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"

template <typename T>
class threadpool
{
public:
    /**
     * 构造函数
    */
    threadpool(int thread_number = 8, int max_requests = 10000);
    /**
     * 析构函数
    */
    ~threadpool();
    /**
     * 向请求队列添加任务
    */
    bool append(T *request);

private:
    /**
     * 工作线程运行的函数，它不断从工作队列中取出任务并执行之
    */
    static void *word(void *arg);
    void run();

private:
    int m_thread_number;        // 线程池中的线程数
    int m_max_requests;         // 请求队列中允许的最大请求数
    pthread_t *m_threads;       // 描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; // 请求队列
    locker m_queuelocker;       // 保护请求队列的互斥锁
    sem m_queuestat;            // 用信号量表示是否有任务需要处理
    bool m_stop;                // 是否结束线程
};

/**
 * 构造函数
 * 首先检查输入数据合法性，然后给线程池的线程数组分配大小，最后创建线程池中的线程
*/
template <typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) : m_thread_number(thread_number),
                                                                 m_max_requests(max_requests),
                                                                 m_stop(false),
                                                                 m_threads(NULL)
{
    if ((thread_number <= 0) || (max_requests <= 0))
    {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
    {
        throw std::exception();
    }

    // 创建m_pthread_number个线程，并将其设置为脱离线程
    // 设置为脱离线程的目的在于，在该状态下，线程主动与主控线程断开关系，线程结束后，不产生僵尸线程
    for (int i = 0; i < thread_number; i++)
    {
        printf("创建第 %d 个线程\n", i);
        if (pthread_create(m_threads + i, NULL, word, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        // 设置为脱离线程
        if (pthread_detach(m_threads[i]) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

/**
 * 析构函数
 * 释放线程池数组的内存，将结束标志置为true
*/
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
    m_stop = true;
}

/**
 * 向工作队列中添加请求
 * 如果工作队列已满，则添加失败
 * 添加成功后工作队列的信号量加一
*/
template <typename T>
bool threadpool<T>::append(T *request)
{
    // 操作工作队列前保证加锁，因为工作队列是被所有线程所共享的
    m_queuelocker.lock();
    if (m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); // 信号量加一
    return true;
}

/**
 * 线程的运行函数
 * 传入参数arg是this指针，指针指向了线程池本身
 * （因为这是一个静态函数，在静态函数中使用了动态成员，包括成员变量和成员函数）
*/
template <typename T>
void *threadpool<T>::word(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

/**
 * 工作线程处理的任务的函数
*/
template <typename T>
void threadpool<T>::run()
{
    printf("线程开始处理任务\n");
    while (!m_stop)
    {
        m_queuestat.wait(); // 处理了队列中的一件任务，信号量减一
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
        {
            continue;
        }
        printf("取出队列中第一个任务开始处理\n");
        request->process();
    }
}

#endif