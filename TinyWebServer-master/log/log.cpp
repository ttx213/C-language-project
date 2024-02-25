#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log()
{
    m_count = 0;
    m_is_async = false;
}

Log::~Log()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
    }
}
// 初始化，异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    // 如果设置了max_queue_size，则设置为异步
    if (max_queue_size >= 1)
    {
        // 设置写入方式flag
        m_is_async = true;
        // 创建并设置阻塞队列长度
        m_log_queue = new block_queue<string>(max_queue_size);
        // 线程tid
        pthread_t tid;
        // flush_log_thread为回调函数，这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }
    
    // 关闭日志标志
    m_close_log = close_log;
    
    // 输出内容的长度
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    
    // 设置日志最大行数
    m_split_lines = split_lines;
    
    // 获取当前时间
    time_t t = time(NULL);
    // 分解为tm结构，并用本地时区表示
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    
    // 搜索最后一次出现/的位置
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};
    
    // 相当于自定义日志名
    // 若输入的文件名没有/，则直接将时间+文件名作为日志名
    if (p == NULL)
    {
        // log_full_name存储处理后的字符串，复制大小为255，格式为第三个参数
        // 组织内容：年 月 日 文件名
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
        // 设置log名
        strcpy(log_name, p + 1);
        // 设置路径名
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }
    // 设置日期
    m_today = my_tm.tm_mday;
    // 添加的方式打开log文件，存在m_fp中
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL)
    {
        return false;
    }

    return true;
}

// 写日志
void Log::write_log(int level, const char *format, ...)
{
    // 获取时间
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    // 将记录精确到秒
    time_t t = now.tv_sec;
    // 转为tm
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    // 日志分级
    char s[16] = {0};    
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    // 写入一个log，对m_count++，m_split_lines最大行数
    m_mutex.lock();
    // 记录一行log
    m_count++;
    // 判断日期或行数是否合规
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) // everyday log
    {

        char new_log[256] = {0};
        // 将缓冲区内容写入m_fp
        fflush(m_fp);
        // 关闭m_fp
        fclose(m_fp);
        char tail[16] = {0};
        // 在tail中记录日期
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        // 日期不匹配
        if (m_today != my_tm.tm_mday)
        {
            // new_log中记录路径和时间
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            // 更新日期
            m_today = my_tm.tm_mday;
            // 重设行数
            m_count = 0;
        }
        // 行数达到最大
        else
        {
            // 超过了最大行，在之前的日志名基础上加后缀, m_count/m_split_lines
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        // 创建文件
        m_fp = fopen(new_log, "a");
    }
    // 解锁
    m_mutex.unlock();

    va_list valst;
    // 获取可变参数列表的第一个参数的地址
    va_start(valst, format);

    string log_str;
    m_mutex.lock();

    // 写入的具体时间内容格式，时间、log级别，返回写入的字数
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    // 1存储位置，2大小，3格式，4内容，返回写入的字数，该函数用于va_list
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);
    // 写完一行设置换行符
    m_buf[n + m] = '\n';
    // 换行符后面设置终止符
    m_buf[n + m + 1] = '\0';
    // 指向缓冲区当前位置
    log_str = m_buf;

    m_mutex.unlock();
    // 如果是异步模式且阻塞队列未满，则往队列中添加元素（唤醒线程处理）
    if (m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    // 否则把缓存区的字符串写入m_fp
    else
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}
