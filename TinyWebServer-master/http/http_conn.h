#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;       // 文件名最大长度
    static const int READ_BUFFER_SIZE = 2048;  // 读缓存区大小
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓存区大小
    // http连接的方法
    enum METHOD
    {
        GET = 0, // 申请获得资源
        POST,    // 向服务器提交数据并修改
        HEAD,    // 仅获取头部信息
        PUT,     // 上传某个资源
        DELETE,  // 删除某个资源
        TRACE,   // 要求服务器返回原始HTTP请求的内容，可用来查看服务器对HTTP请求的影响
        OPTIONS, // 查看服务器对某个特定URL都支持哪些请求方法。也可把URL设置为* ，从而获得服务器支持的所有请求方法
        CONNECT, // 用于某些代理服务器，能把请求的连接转化为一个安全隧道
        PATH     // 对某个资源做部分修改
    };
    // 主状态机状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0, // 检查请求行
        CHECK_STATE_HEADER,          // 检查头部状态
        CHECK_STATE_CONTENT          // 检查内容
    };
    // HTTP状态码
    enum HTTP_CODE
    {
        NO_REQUEST,        // 请求不完整，需要继续读取请求报文数据
        GET_REQUEST,       // 获取了完整请求
        BAD_REQUEST,       // HTTP请求报文有语法错误
        NO_RESOURCE,       // 无资源
        FORBIDDEN_REQUEST, // 禁止请求
        FILE_REQUEST,      // 文件请求
        INTERNAL_ERROR,    // 服务器内部错误
        CLOSED_CONNECTION  // 关闭连接
    };
    // 从状态机状态
    enum LINE_STATUS
    {
        LINE_OK = 0, // 读取完成
        LINE_BAD,    // 读取有错误
        LINE_OPEN    // 未读完
    };

public:
    // 构造函数
    http_conn() {}
    // 析构函数
    ~http_conn() {}

public:
    // 初始化套接字地址，函数内部会调用私有方法init
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    // 关闭http连接
    void close_conn(bool real_close = true);
    // 处理HTTP请求的入口函数
    void process();
    // 读取浏览器端发来的全部数据
    bool read_once();
    // 响应报文写入函数
    bool write();
    // 地址
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    // 同步线程初始化数据库读取表
    void initmysql_result(connection_pool *connPool);
    // 计时器标志
    int timer_flag;
    int improv;

private:
    void init();                                            // 初始化
    HTTP_CODE process_read();                               // 从m_read_buf读取，并处理请求报文
    bool process_write(HTTP_CODE ret);                      // 向m_write_buf写入响应报文数据
    HTTP_CODE parse_request_line(char *text);               // 主状态机，解析http请求行
    HTTP_CODE parse_headers(char *text);                    // 主状态机，解析http请求头
    HTTP_CODE parse_content(char *text);                    // 主状态机，判断http请求内容
    HTTP_CODE do_request();                                 // 生成响应报文
    char *get_line() { return m_read_buf + m_start_line; }; // 用于将指针向后偏移，指向未处理的字符
    LINE_STATUS parse_line();                               // 从状态机，分析一行的内容，返回状态
    void unmap();                                           // 关闭内存映射

    // 根据响应报文格式，生成对应8个部分，以下函数均由do_request调用
    bool add_response(const char *format, ...);          // response
    bool add_content(const char *content);               // content
    bool add_status_line(int status, const char *title); // status_line
    bool add_headers(int content_length);                // headers
    bool add_content_type();                             // content_type
    bool add_content_length(int content_length);         // content_length
    bool add_linger();                                   // linger
    bool add_blank_line();                               // blank_line

public:
    static int m_epollfd;    // 最大文件描述符个数
    static int m_user_count; // 当前用户连接数
    MYSQL *mysql;            // 数据库指针
    int m_state;             // 读为0, 写为1

private:
    int m_sockfd;                        // 当前fd
    sockaddr_in m_address;               // 当前地址
    char m_read_buf[READ_BUFFER_SIZE];   // 存储读取的请求报文数据
    long m_read_idx;                     // 缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    long m_checked_idx;                  // m_read_buf读取的位置m_checked_idx
    int m_start_line;                    // m_read_buf中已经解析的字符个数
    char m_write_buf[WRITE_BUFFER_SIZE]; // 存储发出的响应报文数据
    int m_write_idx;                     // 指示buffer中的长度
    CHECK_STATE m_check_state;           // 主状态机状态
    METHOD m_method;                     // 请求方法

    // 以下为解析请求报文中对应的6个变量
    char m_real_file[FILENAME_LEN]; // 存储读取文件的名称
    char *m_url;                    // url
    char *m_version;                // version
    char *m_host;                   // host
    long m_content_length;          // content_length
    bool m_linger;                  // linger

    char *m_file_address;           // 读取服务器上的文件地址
    struct stat m_file_stat;        // 文件状态
    struct iovec m_iv[2];           // io向量机制iovec，标识两个缓存区
    int m_iv_count;                 // 表示缓存区个数
    int cgi;                        // 是否启用的POST
    char *m_string;                 // 存储请求头数据
    int bytes_to_send;              // 待发送字节个数
    int bytes_have_send;            // 已发送字节个数
    char *doc_root;                 // 文件根目录

    map<string, string> m_users; // 用户名密码对
    int m_TRIGMode;              // 触发模式
    int m_close_log;             // 是否关闭log

    char sql_user[100];   // 用户名
    char sql_passwd[100]; // 用户密码
    char sql_name[100];   // 数据库名
};

#endif
