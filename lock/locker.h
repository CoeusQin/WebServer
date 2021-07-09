#ifndef __LOCKER_H__
#define __LOCKER_H__

#include <exception>
#include <pthread.h>
#include <semaphore.h>

/**
 * 封装信号量的类
*/
class sem
{
private:
    sem_t m_sem;

public:
    /**
     * 构造函数，初始化信号量
    */
    sem()
    {
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            // 构造函数没有返回值，通过抛出异常来报告错误
            throw std::exception();
        }
    }
    // 重载构造函数，建议信号量初始值为num
    sem(int num)
    {
        if(sem_init(&m_sem, 0, num) != 0)
        {
            // 构造函数没有返回值，通过抛出异常来报告错误
            throw std::exception();
        }
    }
    /**
     * 析构函数，销毁信号量
    */
    ~sem()
    {
        sem_destroy(&m_sem);
    }

    /**
     * 等待信号量
     * 信号量的值减一
    */
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }

    /**
     * 增加信号量
     * 信号量的值加一
    */
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }
};


/**
 * 封装互斥锁的类
*/
class locker
{
private:
    pthread_mutex_t m_mutex;

public:
    // 创建并初始化互斥锁
    locker()
    {
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }
    // 销毁互斥锁
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }

    // 获取互斥锁
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0; // 给互斥锁加锁
    }

    // 销毁互斥锁
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0; // 给互斥锁解锁
    }
};

/**
 * 封装条件变量类
*/
class cond
{
private:
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;

public:
    /**
     * 创建并初始化条件变量
    */
    cond();
    ~cond();

    // 等待条件变量
    bool wait()
    {
        int res = 0;
        pthread_mutex_lock(&m_mutex);
        res = pthread_cond_wait(&m_cond, &m_mutex);
        pthread_mutex_unlock(&m_mutex);
        return res == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        pthread_mutex_lock(m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        pthread_mutex_unlock(m_mutex);
        return ret == 0;
    }
    // 唤醒等待条件变量的线程
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }
};

cond::cond()
{
    if (pthread_mutex_init(&m_mutex, NULL) != 0)
    {
        throw std::exception();
    }
    if (pthread_cond_init(&m_cond, NULL) != 0)
    {
        // 构造函数出现问题，立即释放已经成功分配的资源
        pthread_mutex_destroy(&m_mutex);
        throw std::exception();
    }
}

cond::~cond()
{
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond);
}

#endif