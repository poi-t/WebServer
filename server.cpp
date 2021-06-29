#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>
#include "locker.h"
#include "http_conn.h"
#include "thread_pool.h"
#include "timeout.h"
#include "redis.h"
static const int MAX_FD = 10000;           //最大的文件描述符个数
static const int MAX_EVENT_NUMBER = 5000; //监听的最大的事件数量
static const int PORT = 80;                //服务器监听端口
static const int TIMEOUT = 10;             //调用alarm函数的时间间隔

timer_list<http_conn> t_list;              //用于清理不活跃连接

/*添加信号捕捉*/
void addsig(int sig, void(handler)(int)) {
    struct sigaction sa;
    sa.sa_flags = 0;
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

//SIGALRM信号处理函数，既删除超时链接
void timeout_handle(int sig) {
    t_list.delete_timeout();
    alarm(TIMEOUT);
}

//错误处理
void err_sys(const char *err)
{
	perror(err);
	exit(0);
}

//添加文件描述符到epoll中
extern void addfd(int epolled, int fd, bool one_shot);

//从epoll中删除文件描述符
extern void removefd(int epollfd, int fd);

//从epoll中修改文件描述符
extern void modfd(int epollfd, int fd, int ev);

int main() {
    daemon(1,0); 

    //对SGIPIPE信号和SIGALRM信号进行处理
    addsig(SIGPIPE, SIG_IGN);
    addsig(SIGALRM, timeout_handle);

    //创建线程池， 初始化线程池
    threadpool<http_conn>* pool = NULL;
    try{
        pool = new threadpool<http_conn>;
    } catch(...) {
        exit(-1);
    }

    // 创建一个数组用于保存所有的客户端信息
    http_conn* users = NULL;
    try{
        users = new http_conn[MAX_FD];
    } catch(...) {
        exit(-1);
    }

    //创建监听套接字
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd == -1) {
        err_sys("socket");
    }

    //设置端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    if(bind(listenfd, (struct sockaddr*)&address, sizeof(address)) == -1) {
        err_sys("bind");
    }

    if(listen(listenfd, 8) == -1) {
        err_sys("listen");
    }
    
    //创建epoll对象和事件数组
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(1);

    //将监听的文件描述符添加到epoll对象中
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;
    
    Redis* accept_count = NULL;
    try{
        accept_count = new Redis();
    } catch(...) {
        exit(-1);
    }

    if(!accept_count->connect("127.0.0.1", 6379)) {
        err_sys("redis");
    }

    accept_count->setnx("count", "0");

    while(true) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if(num == -1 && errno != EINTR) {
            break;
        }

        for(int i = 0; i < num; ++i) {
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd) {
                //此时为客户端连接进来
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlen);
                if(connfd == -1) {
                    continue;
                }
                if(http_conn::m_user_count >= MAX_FD || connfd >= MAX_FD) {
                    close(connfd);
                    continue;
                }
                //将新的客户的数据初始化，放到数组中
                users[connfd].init(connfd, client_address);
                //创建对应的一个定时器加入链表
                t_list.set_time(connfd, users + connfd);
                accept_count->incr("count");
                
            } else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                //对方异常断开或者错误
                t_list.delete_timer(sockfd);
                users[sockfd].close_conn();
            } else if(events[i].events & EPOLLIN) {
                if(users[sockfd].readall()) {
                    //一次性读完
                    if(pool->append(users + sockfd)) {
                        t_list.set_time(sockfd, users + sockfd);
                    } else {
                        t_list.delete_timer(sockfd);
                        users[sockfd].close_conn();
                    }
                } else {
                    t_list.delete_timer(sockfd);
                    users[sockfd].close_conn();
                }
            } else if(events[i].events & EPOLLOUT) {
                if(users[sockfd].writeall()) {
                    t_list.set_time(sockfd, users + sockfd);
                } else {
                    t_list.delete_timer(sockfd);
                    users[sockfd].close_conn();
                }
            }
        }    
    }
    
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
    t_list.delete_all();
    return 0;
} 
