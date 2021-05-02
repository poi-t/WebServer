#include "thread_pool.h"


template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) : m_stop(false) {
    m_thread_number = thread_number > 0 ? thread_number : 8;
    m_max_requests = max_requests > 0 ? max_requests : 5000;
    m_threads = new pthread_t[m_thread_number];
    if(m_threads == NULL) {
        throw std::exception();
    }

    for(int i = 0; i < thread_number; ++i) {
        //创建线程，并设置为脱离
        if(pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete [] m_threads;
            throw std::exception();
        }

        if(pthread_detach(m_threads[i]) != 0) {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T* request) {
    m_queuelocker.lock();
    
    if(m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg) {
    threadpool* pool = (threadpool*) arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run() {
    while(!m_stop) {
        m_queuestat.wait();
        m_queuelocker.lock();

        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if(request == NULL) {
            continue;
        }
        request->process();
    }
}
