/*************************************************************************
	> File Name: locker.h
	> Author: WishSun
	> Mail: WishSun_Cn@163.com
	> Created Time: 2018年07月03日 星期二 16时57分44秒
 ************************************************************************/

#ifndef _LOCKER_H
#define _LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

/*封装(线程间)信号量的类-------------------------------------------------*/
class sem 
{
private:
    sem_t m_sem;

public:
    /*创建并初始化信号量*/
    sem()
    {
        /*初始化匿名信号量m_sem，
         *第二个参数0代表此信号量为局部信号量，只在本进程中使用
         *第三个参数0代表此信号量的初值为0
         */
        if(sem_init(&m_sem, 0, 0) != 0)  
        {
            /*构造函数没有返回值，通过抛出异常来报告错误*/
            throw std::exception();
        }
    }

    /*销毁信号量*/
    ~sem()
    {
        sem_destroy(&m_sem);
    }

    /*等待信号量*/
    bool wait()
    {
        /*将信号量-1，相当于P操作，如果m_sem为0，则阻塞，直至m_sem非0*/
        return sem_wait(&m_sem) == 0;
    }

    /*增加信号量*/
    bool post()
    {
        /*将信号量+1，相当于V操作，如果m_sem的值大于0时，将唤醒正在调用sem_wait的等待线程*/
        return sem_post(&m_sem) == 0;
    }
};



/* 封装互斥锁的类------------------------------------------------------*/
class locker
{
private:
    pthread_mutex_t  m_mutex;

public:
    /*创建并初始化互斥锁*/
    locker()
    {
        if(pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }

    /*销毁互斥锁*/
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }

    /*获取互斥锁*/
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    /*释放互斥锁*/
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
};


/* 封装条件变量的类--------------------------------------------------*/
class cond
{
private:
    pthread_mutex_t m_mutex;
    pthread_cond_t  m_cond;

public:
    /*创建并初始化条件变量*/
    cond()
    {
        if(pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
        if(pthread_cond_init(&m_cond, NULL) != 0)
        {
            /*构造函数一旦出了问题，就应该立即释放已成功分配了的资源*/
            pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }

    /*销毁条件变量*/
    ~cond()
    {
        pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_cond);
    }

    /*等待条件变量*/
    bool wait()
    {
        int ret = 0;

        /*在调用pthread_cond_wait函数前，必须确保互斥锁mutex已经加锁, 
         *否则将导致不可预期的结果
         */
        pthread_mutex_lock(&m_mutex);

        /*pthread_cond_wait函数执行时，首先把调用线程放入条件变量的等
         *待队列中，然后将互斥锁mutex解锁(为了能让别的线程也可以等待)。
         *当pthread_cond_wait函数成功返回时，互斥锁mutex将再次被锁上*/
        ret = pthread_cond_wait(&m_cond, &m_mutex);
        pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }

    /*唤醒等待一个条件变量的线程*/
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
};
#endif
