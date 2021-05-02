#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include "locker.h"

/*线程池类， T为任务类*/
template<typename T>
class threadpool {
public:
    threadpool(int thread_number = 8, int max_requests = 5000);
    ~threadpool();
    bool append(T* request);         //添加任务
    
private:
    static void* worker(void* arg);  //工作线程运行的函数，不断从工作队列中取出任务执行
    void run();                      //任务运行函数

    int m_thread_number;             //线程数量
    pthread_t* m_threads;            //线程池数组
    int m_max_requests;              //请求队列中最多允许的等待处理的请求数量
    std::list<T*> m_workqueue;       //请求队列
    locker m_queuelocker;            //保护对请求队列操作的互斥锁
    sem m_queuestat;                 //判断是否有任务需要处理的信号量
    bool m_stop;                     //判断线程是否需要停止
};


#endif
