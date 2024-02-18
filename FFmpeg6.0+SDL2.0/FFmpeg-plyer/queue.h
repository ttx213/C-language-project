#ifndef QUEUE_H
#define QUEUE_H
#include <mutex>
#include <condition_variable>
#include <queue>

template <typename T>
class Queue
{
public:
    Queue() {}
    ~ Queue() {}
    void Abort()
    {
        abort_ = 1;
        cond_.notify_all();
    }

    int Push(T val)
    {   //用于在其作用域内锁定mutex_这个互斥量。这个锁的作用是确保在访问队列时是线程安全的，即同一时间只有一个线程可以访问队列。
        std::lock_guard<std::mutex> lock(mutex_);
        if(1 == abort_) {
            return -1;
        }
        queue_.push(val);
        //通知一个正在等待条件变量cond_的线程，表示队列中有新的元素可用。这样可以唤醒一个等待中的线程，让其继续执行。
        cond_.notify_one();
        return 0;
    }

    int Pop(T &val, const int timeout = 0)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if(queue_.empty()) {
            // 等待push或者超时唤醒
            cond_.wait_for(lock, std::chrono::milliseconds(timeout), [this] {
                return !queue_.empty() | (abort_ == 1);
            });
        }
        if(1 == abort_) {
            return -1;
        }
        if(queue_.empty()) {
            return -2;
        }
        val = queue_.front();
        queue_.pop();
        return 0;
    }

    int Front(T &val)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(1 == abort_) {
            return -1;
        }
        if(queue_.empty()) {
            return -2;
        }
        val = queue_.front();
        return 0;
    }

    int Size()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }


private:
    //abort_的作用是控制队列的终止状态，用于在队列终止时避免继续对队列进行操作
    int abort_ = 0;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::queue<T> queue_;
};

#endif // QUEUE_H
