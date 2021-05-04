#ifndef TIMEOUT_H
#define TIMEOUT_H

#include <stdio.h>
#include <time.h>
#include <unordered_map>

static const int TIME_OUT = 60;

// 定时器类
template<typename T>
class timer {
public:
    timer() : m_user(NULL), prev(NULL), next(NULL), m_time(-1) {}
    timer(T* user, time_t t) : m_user(user), prev(NULL), next(NULL), m_time(t) {}
    T* m_user;
    time_t m_time;
    timer* prev; 
    timer* next;
};

//定时器链表类
template<typename T>
class timer_list {
private:
    timer<T>* head;
    timer<T>* tail;
    std::unordered_map<int, timer<T>* > mp;

    void remove(timer<T>* p);
    void addHead(timer<T>* p);

public:
    timer_list();
    void delete_timeout();            //删除超时定时器
    void delete_timer(int fd);        //删除指定计数器
    void set_time(int fd, T* user);   //重设时间并更改位置
    void delete_all();                //删除所有定时器
};

template<typename T>
timer_list<T>::timer_list() {
    head = new timer<T>();
    tail = new timer<T>();
    head->next = tail;
    tail->prev = head;
}

template<typename T>
void timer_list<T>::delete_timeout() {
    while(1) {
        timer<T>* p = tail->prev;
        time_t nowtime = time(NULL);
        if(p == head || (nowtime - p->m_time) < TIME_OUT) {
            break;
        }
        
        tail->prev = p->prev;
        p->prev->next = tail;
        int ret = p->m_user->close_conn();
        mp.erase(ret);
        delete p;
    }
}

template<typename T>
void timer_list<T>::delete_timer(int fd) {
    if(mp.find(fd) != mp.end()) {
        timer<T>* p = mp[fd];
        remove(p);
        delete p;
        mp.erase(fd);
    }
}

template<typename T>
void timer_list<T>::set_time(int fd, T* user) {
    time_t nowtime = time(NULL);
    if(mp.find(fd) == mp.end()) {
        timer<T>* res = new timer<T>(user, nowtime);
        mp[fd] = res;
        addHead(res);
    } else {
        timer<T>* res = mp[fd];
        //删掉原位置
        remove(res);
        //加到头部
        addHead(res);
        res->m_time = nowtime;  
    }
}

template<typename T>
void timer_list<T>::delete_all() {
    while(1) {
        timer<T>* p = tail->prev;
        if(p == head) {
            break;
        }
        tail->prev = p->prev;
        p->prev->next = tail;
        delete p;
    }
    delete head;
    delete tail;
}

template<typename T>
void timer_list<T>::remove(timer<T>* p) {
    p->prev->next = p->next;
    p->next->prev = p->prev;
}

template<typename T>
void timer_list<T>::addHead(timer<T>* p) {
    p->next = head->next;
    head->next->prev = p;
    head->next = p;
    p->prev = head;
}

#endif
