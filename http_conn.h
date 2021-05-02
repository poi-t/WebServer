#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/uio.h>
#include <ctype.h>
#include "locker.h"

class http_conn {
public:
    http_conn() {}
    ~http_conn() {}
    void init(int sockfd, const sockaddr_in& addr);      //初始化新接收的连接
    void close_conn();                                   //关闭连接
    bool readall();                                      //一次性读入数据
    bool writeall();                                     //一次性写出数据
    void process();                                      //处理客户端请求

    static int m_epollfd;                                //所有的socke上的事件都被注册到此epoll上
    static int m_user_count;                             //统计连接的用户的数量
    
private:
    //HTTP请求方法
    enum METHOD {GET, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

    //解析客户端请求时，主状态机的状态
    enum CHECK_STATE {CHECK_REQUEST_LINE, CHECK_HEADER, CHECK_ENTITY};

    //从状态机的三种可能状态，既行的读取状态，
    enum LINE_STATUS {LINE_OK, LINE_ERROR, LINE_INCOMPLETE};

    /*
        服务器处理 HTTP 请求的可能结果，报文解析的结果
        PROCESSING           : 请求仍需处理
        GET_REQUEST          : 获得了一个完整的客户请求
        BAD_REQUEST          : 客户端请求语法错误 (400)
        FORBIDDEN_REQUEST    : 表示客户对资源没有足够的访问权限 (403)
        NO_RESOURCE          : 服务器没有该资源 (404)
        FILE_REQUEST         : 文件请求，获取文件成功 (200)
        INTERNAL_ERROR       : 服务器内部错误 (500)
        UNIMPLEMENTED        : 客户端使用了未实现的请求方法 (501)
    */
    enum HTTP_CODE {PROCESSING, GET_REQUEST, BAD_REQUEST, FORBIDDEN_REQUEST, NO_RESOURCE, FILE_REQUEST, INTERNAL_ERROR, UNIMPLEMENTED};

    void init();                                         //初始化内部信息
    HTTP_CODE process_read();                            //解析HTTP请求
    bool process_write(HTTP_CODE ret);                   //填充HTTP应答

    // 下面这一组函数被process_read调用以分析HTTP请求
    HTTP_CODE parse_request_line(char* text);            //解析请求行
    HTTP_CODE parse_headers(char* text);                 //解析请求头
    HTTP_CODE parse_entity(char* text);                  //解析请求实体
    HTTP_CODE do_request();                              //解析文件
    LINE_STATUS parse_line();                            //解析一行数据，依据\r\n为结尾
    char* get_line();                                    //获取一行数据
    
    // 这一组函数被process_write调用以填充HTTP应答
    void unmap();                                        //对内存映射区执行munmap操作
    bool add_response(const char* format, ...);          //往写缓冲中写入待发送的数据
    bool add_status_line(int status, const char* title); //写入状态行
    bool add_headers(int content_length);                //写入响应头
    bool add_entity(const char* content);                //写入响应实体
    bool write_end();                                    //一次写结束的最终操作

    static const int FILENAME_LEN = 512;                 //文件名最大长度
    static const int URL_LEN = 500;                      //url允许最大长度
    static const int READ_BUF_SIZE = 2048;               //读缓冲区大小
    static const int WRITE_BUF_SIZE = 2048;              //写缓冲区大小
    static const int err_information_len = 97;           //错误处理报文基长度
    
    int m_sockfd;                                        //该HTTP连接的socket
    sockaddr_in m_address;                               //通信的socket地址

    char m_read_buf[READ_BUF_SIZE];                      //读缓冲区
    int m_read_idx;                                      //标识读缓冲区已读入数据最后一个字节的下一个位置
    int m_checked_idx;                                   //当前正在分析的字符在读缓冲区中的位置
    int m_start_line;                                    //当前正在解析的行的起始位置
    locker user_count_lock;                              //保护用户数量操作的互斥锁
    CHECK_STATE m_check_state;                           //主状态机当前所处状态
    METHOD m_method;                                     //请求方法
    char* m_host;                                        //主机名
    int m_content_length;                                //HTTP请求报文主体总长度
    bool m_linger;                                       //HTTP请求是否要求保持连接
    char m_url[URL_LEN];                                 //客户请求的目标文件的文件名
    char m_real_file[FILENAME_LEN];                      //客户请求的目标文件的完整路径

    char m_write_buf[WRITE_BUF_SIZE];                    //写缓冲区
    int m_write_idx;                                     //写缓冲区中待发送的字节数
    char* m_file_address;                                //客户请求的目标文件被mmap到内存中的起始位置
    int m_file_idx;                                      //目标文件大小
    struct stat m_file_stat;                             //目标文件的状态
    int bytes_to_send;                                   //将要发送的数据的字节数
    int bytes_have_send;                                 //已经发送的字节数
};

#endif
