#include "http_conn.h"

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;
const char* resources_dir = "/root/HttpServer/resources";  //网站资源目录；


//设置文件描述符非阻塞
int setnoblocking(int fd) {
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
    return old_flag;
}

//向epoll中添加需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if(one_shot) {
        // 防止同一个通信被不同的线程处理
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

    //设置文件描述符非阻塞
    setnoblocking(fd);
}

//从epoll中移除监听的文件描述符
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//修改文件描述符，重置socket上的EPOLLONESHOT事件，确保下一次可读时EPOLLIN事件可触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in& addr) {
    m_sockfd = sockfd;
    m_address = addr;

    //添加到epoll对象中
    addfd(m_epollfd, sockfd, true);
    m_user_count++;
    init();//初始化其他部分
}

//关闭连接，由于主线程也可能调用此函数，需要对用户数加锁
void http_conn::close_conn() {
    if(m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        user_count_lock.lock();
        m_user_count--;
        user_count_lock.unlock();
    }
}

//一次性读出所有数据,循环读取客户数据，直到无数据可读或对方关闭连接
bool http_conn::readall() {
    if(m_read_idx >= READ_BUF_SIZE) {
        return false;
    }

    //读取到的字节
    int bytes_read = 0;
    while(true) {
        // 从m_read_buf + m_read_idx索引处开始保存数据，大小是READ_BUFFER_SIZE - m_read_idx
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUF_SIZE - m_read_idx, 0);
        if(bytes_read == -1) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                //没有数据
                break;
            } else {
                return false;
            }
        } else if(bytes_read == 0) {
            //对方关闭连接
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}

//一次性写完数据
bool http_conn::writeall(){
    int ret = 0;
    if(bytes_to_send == 0) {
        // 将要发送的字节为0，这一次响应结束
        return write_end();
    }

    while(1) {
        //发送写缓冲区中数据
        ret = write(m_sockfd, m_write_buf + bytes_have_send, m_write_idx - bytes_have_send);
        if(ret == -1) {
            if(m_file_idx != 0) {
                unmap();
            }
            return false;
        }
        bytes_have_send += ret;
        bytes_to_send -= ret;
        if(bytes_have_send == m_write_idx) {
            break;
        }
    }

    if(bytes_to_send == 0) {
        return write_end();
    }

    int m_file_send = 0;
    while(1) {
        //发送文件
        ret = write(m_sockfd, m_file_address + m_file_send, m_file_idx - m_file_send);
        if(ret == -1) {
            unmap();
            return false;
        }
        bytes_have_send += ret;
        bytes_to_send -= ret;
        m_file_send += ret;
        if(bytes_to_send == 0) {
            unmap();
            return write_end();
        }
    }

}

//由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process() {
    //解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if(read_ret == PROCESSING) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    // 生成响应
    bool write_ret = process_write( read_ret );
    if (!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}


//内部相关变量初始化
void http_conn::init() {
    m_check_state = CHECK_REQUEST_LINE;//初始化状态为解析请求首行
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_linger = false;       
    m_method = GET;         
    m_content_length = 0;
    m_host = NULL;
    m_start_line = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    m_file_idx = 0;
    m_checked_idx = 0;
    m_write_idx = 0;

    bzero(m_read_buf, READ_BUF_SIZE);
    bzero(m_write_buf, READ_BUF_SIZE);
    bzero(m_real_file, FILENAME_LEN);
    bzero(m_url, URL_LEN);
}

//主状态机，解析请求
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = PROCESSING;
    char* text = 0;
    while((m_check_state == CHECK_ENTITY && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)) {
        //解析到了一行完整的数据，或者解析到来请求体，也为一行完整数据
        //获取一行数据
        text = get_line();
        m_start_line = m_checked_idx;

        switch (m_check_state) {
            case CHECK_REQUEST_LINE: {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if(ret == UNIMPLEMENTED) {
                    return UNIMPLEMENTED;
                }
                break;
            }

            case CHECK_HEADER: {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if(ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            
            case CHECK_ENTITY: {
                ret = parse_entity(text);
                if(ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_INCOMPLETE;
                break;
            }

            default: {
                return INTERNAL_ERROR;
                break;
            }
        }
    }
    return PROCESSING;
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR: {
            const char* error_500 =  "Internal Server Error";
            add_status_line(500, error_500);
            add_headers(err_information_len + 2 * strlen(error_500));
            if (!add_entity(error_500)) {
                return false;
            }
            break;
        }

        case BAD_REQUEST: {
            const char* error_400 = "Bad Request";
            add_status_line(400, error_400);
            add_headers(err_information_len + 2 * strlen(error_400));
            if (!add_entity(error_400)) {
                return false;
            }
            break;
        }

        case NO_RESOURCE: {
            const char* error_404 = "NOT FOUND";
            add_status_line(404, error_404);
            add_headers(err_information_len + 2 * strlen(error_404));
            if (!add_entity(error_404)) {
                return false;
            }
            break;
        }

        case FORBIDDEN_REQUEST: {
            const char* error_403 = "Forbidden";
            add_status_line(403, error_403);
            add_headers(err_information_len + 2 * strlen(error_403));
            if (!add_entity(error_403)) {
                return false;
            }
            break;
        }

        case FILE_REQUEST: {
            const char* ok_200 = "OK";
            add_status_line(200, ok_200);
            add_headers(m_file_stat.st_size);
            m_file_idx = m_file_stat.st_size;
            bytes_to_send = m_write_idx + m_file_idx;
            return true;
        }

        case UNIMPLEMENTED : {
            const char* error_501 = "Method Not Implemented";
            add_status_line(403, error_501);
            add_headers(err_information_len + 2 * strlen(error_501));
            if (!add_entity(error_501)) {
                return false;
            }
            break;
        }
        default: {
            return false;
        }
    }
    bytes_to_send = m_write_idx;
    return true;
}

//获取一行数据
char* http_conn::get_line() {
    return m_read_buf + m_start_line;
}


//解析一行，依据\r\n为结尾
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for( ; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r') {
            if(m_checked_idx + 1 == m_read_idx) {
                //以\r结尾
                return LINE_INCOMPLETE;
            } else if(m_read_buf[m_checked_idx + 1] == '\n') {
                //以\r\n结尾，改为以\0结尾
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            } else {
                //中间出现\r
                return LINE_ERROR;
            }
        } else if(temp == '\n') {
            return LINE_ERROR;
        }
    }

    return LINE_INCOMPLETE;
}

//解析HTTP请求行，获得请求方法，目标URL
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    char method[12];
    int i = 0, len = strlen(text);

    while(!isspace(*text) && (i < sizeof(method) - 1) && i < len) {
        method[i++] = *text++;
    }
    if(i == len) {
        return BAD_REQUEST;
    }

    method[i] = '\0';
    if(strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else {
        return UNIMPLEMENTED;//不支持
    }

    while(isspace(*text) && i < len) {
		++text;
        ++i;
	}
    if(i == len) {
        return BAD_REQUEST;
    }

    int j = 0;
    while (!isspace(*text) && (i < URL_LEN - 1) && (i < len)) {
		m_url[j++] = *text++;
        ++i;
	}
 	m_url[i] = '\0';
    m_check_state = CHECK_HEADER;
    return PROCESSING;
} 

//解析HTTP请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
    // 遇到空行，表示头部字段解析完毕
    if(text[0] =='\0') {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if(m_content_length != 0) {
            m_check_state = CHECK_ENTITY;
            return PROCESSING;
        } else {
            // 否则说明已经得到了一个完整的HTTP请求
            return GET_REQUEST;
        }
    } else if(strncasecmp(text, "Connection:", 11) == 0) {
        // 处理Connection字段
        text += 11;
        while(isspace(*text)) {
		    ++text;
	    }
        if(strncasecmp(text, "keep-alive", 10) == 0) {
            m_linger = true;
        }
    } else if(strncasecmp(text, "Content-Length:", 15) == 0) {
        // 处理Content-Length字段
        text += 15;
        while(isspace(*text)) {
		    ++text;
	    }
        m_content_length = atoi(text);
    } else if(strncasecmp(text, "Host:", 5) == 0) {
        //处理Host字段
        text += 5;
        while(isspace(*text)) {
		    ++text;
	    }
        m_host = text;
    }
    return PROCESSING;
} 

// 不真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
http_conn::HTTP_CODE http_conn::parse_entity(char* text) {
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return PROCESSING;
}

// 当得到一个完整、正确的HTTP请求时就分析目标文件的属性，如果目标文件存在、对所有用户可读，
// 且不是目录，则使用mmap将其映射到内存地址m_file_address处，并告知调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request() {
    //拼接基地址和url得到文件绝对路径
    strcpy(m_real_file, resources_dir);
    int len = strlen(resources_dir);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    
    if(m_real_file[strlen(m_real_file) - 1] == '/') {
        //默认访问index.html
        strcat(m_real_file, "index.html");
    }
    
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if(stat(m_real_file, &m_file_stat) == -1) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        strcat(m_real_file, "/index.html");
    }

    // 以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    // 创建内存映射
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// 对内存映射区执行munmap操作
void http_conn::unmap() {
    if(m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response(const char* format, ...) {
    if(m_write_idx >= WRITE_BUF_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUF_SIZE - 1 - m_write_idx, format, arg_list);
    if( len >= (WRITE_BUF_SIZE - 1 - m_write_idx)) {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len) {
    add_response("Server: HttpServer\r\n");
    add_response("Content-Length: %d\r\n", content_len);
    add_response("Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close");
    add_response("Content-Type:%s\r\n", "text/html");
    add_response("%s", "\r\n" );
    return true;
}

bool http_conn::add_entity( const char* content ) {
    add_response("<html>\r\n");
    add_response("<head><title>%s</title></head>\r\n", content);
	add_response("<body>\r\n");
	add_response("<div style=\"text-align:center;\">%s</div>\r\n", content);
    add_response("</body>\r\n");
    add_response("<html>\r\n");
    return true;
}

bool http_conn::write_end() {
    modfd(m_epollfd, m_sockfd, EPOLLIN);
    if(m_linger) {
        init();
        return true;
    } else {
        return false;
    }
}
