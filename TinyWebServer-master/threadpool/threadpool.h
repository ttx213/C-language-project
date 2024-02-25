#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T> //类模板
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理
    connection_pool *m_connPool;  //数据库
    int m_actor_model;          //模型切换
};
template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool)
{
    // 线程数和允许的最大请求数均小等于0，出错
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    
    // 初始化线程，分配id
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i)
    {
        // 循环创建线程，1标识符，2线程属性（NULL为默认），3指定线程将运行的函数，4运行的参数
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        // 将线程进行分离后，不用单独对工作线程进行回收，它在退出时会自行释放资源，不再需要在其它线程中对其进行pthread_join() 操作
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

// 析构函数
template <typename T>
threadpool<T>::~threadpool()
{
    // 释放id空间即可，脱离线程会自行释放资源
    delete[] m_threads;
}

// 向请求队列中添加任务
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    // 互斥锁加锁
    m_queuelocker.lock();

    // 判断请求队列是否大于最大请求个数
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }

    // 设置HTTP请求状态
    request->m_state = state;

    // 向工作队列中添加任务
    m_workqueue.push_back(request);

    // 互斥锁解锁
    m_queuelocker.unlock();

    // 通过信号量提示有任务要处理 post信号量加一
    m_queuestat.post();
    return true;
}
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

//线程处理函数
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    // 将参数强制转为线程池类，调用成员方法
    threadpool *pool = (threadpool *)arg;

    // 内部访问私有成员函数run，完成线程处理
    pool->run();
    return pool;
}

// 处理HTTP请求
template <typename T>
void threadpool<T>::run()
{
     // 工作线程从请求队列中取出某个任务进行处理
    while (true)
    {
        // wait信号量减一，取出一个任务，先处理信号量能保证线程一定能取到任务，虽然任务的顺序可能会变，但不影响
        m_queuestat.wait();

        // 互斥锁加锁
        m_queuelocker.lock();

        // 请求队列为空则继续循环

        if (m_workqueue.empty())
        {
            m_queuelocker.unlock(); 
            continue;
        }

        T *request = m_workqueue.front();
        m_workqueue.pop_front();

        // 取第一个任务后互斥锁解锁
        m_queuelocker.unlock();

        // 空请求，继续循环
        if (!request)
            continue;
        
        // 模式1表示reactor
        if (1 == m_actor_model)
        {
            // 读请求
            if (0 == request->m_state)
            {
                // 读完缓存区内容或用户关闭连接，1表示缓存区正常读完
                if (request->read_once())
                {
                    // 设置improv
                    request->improv = 1;

                    // 从连接池中取出一个数据库连接
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    
                    // 处理http请求的入口
                    request->process();
                }
                
                // 未读完
                else
                {
                    request->improv = 1;
                    // 计时器标志
                    request->timer_flag = 1;
                }
            }
            else
            {
                // 写请求
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        // 模式2表示proactor
        else
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif
