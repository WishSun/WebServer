/*************************************************************************
	> File Name: threadpool.h
	> Author: WishSun
	> Mail: WishSun_Cn@163.com
	> Created Time: 2018年07月03日 星期二 17时35分30秒
 ************************************************************************/

#ifndef _THREADPOOL_H
#define _THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "./locker.h"


/*线程池类，将它定义为模板是为了代码复用，模板参数T是任务类*/
template< typename T >
class threadpool
{
private:
    /*工作线程线程函数，它不断从工作队列中取出任务并执行之*/
    static void* worker( void *arg );

    /*被worker调用*/
    void run(); 

private:
    int m_thread_number;         /*线程池中的线程数*/
    int m_max_requests;          /*请求队列中允许的最大请求数*/
    pthread_t *m_threads;        /*描述线程池的数组，其大小为m_thread_number*/
    std::list< T* > m_workqueue; /*使用STL中的链表来做请求队列*/ 

    locker m_queuelocker;        /*保护请求队列的互斥锁*/
    sem    m_queuestat;          /*是否有任务需要处理*/
    bool   m_stop;               /*是否结束线程*/

public:
    /*参数thread_number是线程池中线程的数量，max_requests是
     *请求队列中最多允许的、等待处理的请求的数量*/
    threadpool( int thread_number = 8, int max_requests = 10000 );
    ~threadpool();

    /*往请求队列中添加任务*/
    bool append( T *request );
};




/* 线程池构造函数*/
template< typename T >
threadpool< T >::threadpool(int thread_number, int max_requests)
               :m_thread_number( thread_number ), m_max_requests( max_requests ),
                m_stop( false ), m_threads(NULL)
{
    if(( thread_number <= 0 ) || (max_requests <= 0))   
    {
        throw std::exception();
    }

    /* 创建m_thread_number个线程描述符标识线程*/
    m_threads = new pthread_t[ m_thread_number ];
    if( !m_threads )
    {
        throw std::exception();
    }

    /*创建thread_number个线程， 并将它们都设置为脱离线程*/
    for( int i = 0; i < thread_number; ++i )
    {
        printf("create the %dth thread\n", i);

        /* 在C++程序中使用pthread_create函数时，该函数的第3个参数必须
         * 指向一个静态函数(worker), 而要在一个静态函数中使用类的动态
         * 成员(包括成员变量和成员函数), 可以使用一种方法: 将类的对象
         * 作为参数传递给该静态函数，然后再静态函数中引用这个对象，并
         * 调用其静态方法
         */
        if( pthread_create( m_threads + i, NULL, worker, this) != 0)
        {
            throw std::exception();
        }

        if( pthread_detach( m_threads[i] ) )
        {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

/* 线程池析构函数*/
template< typename T >
threadpool< T >::~threadpool()
{
    delete [] m_threads;
    m_stop = true;
}

/* 向任务队列中添加任务*/
template< typename T >
bool threadpool< T >::append( T *request )
{
    /*操作工作队列时一定要加锁，因为它是所有线程共享的*/
    m_queuelocker.lock();

    /*超过任务数量上限，则不添加该任务*/
    if( m_workqueue.size() > m_max_requests )
    {
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back( request );
    m_queuelocker.unlock();

    /*唤醒当前正在等待任务的一个线程*/
    m_queuestat.post();
    return true;
}

/* 线程函数入口
 * 接受的参数是线程池对象, 因为worker函数是静态的不能调用非静态成员函数，
 * 所以从参数获取对象，通过对象来调用非静态成员函数
 */
template< typename T >
void* threadpool< T >::worker( void *arg )
{
    threadpool *pool = (threadpool *)arg;
    pool->run();

    return pool;
}

/* 线程函数核心*/
template< typename T >
void threadpool< T >::run()
{
    while( ! m_stop )
    {
        /*等待任务*/
        m_queuestat.wait();

        /*等到了任务，现在要在任务队列中取任务，对任务队列操作必须加锁*/
        m_queuelocker.lock();
        if ( m_workqueue.empty() )
        {/*其他线程抢先一步取走了任务*/
            m_queuelocker.unlock();
            continue;
        }

        /*取任务*/
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if( ! request )
        {
            continue;
        }

        /*执行任务*/
        request->process();
    }
}
#endif
