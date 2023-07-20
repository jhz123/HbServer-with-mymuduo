#pragma once

#include <atomic>

#include "Timestamp.h"
#include "Callbacks.h"
#include "noncopyable.h"

class Timer : noncopyable
{
public:
    Timer(TimerCallback cb, Timestamp when, double interval)
        : callback_(std::move(cb)),
          expiration_(when),
          interval_(interval),
          repeat_(interval > 0.0),
          sequence_(s_numCreated_.fetch_add(1,std::memory_order_relaxed)+1)
    { }

    void run() const
    {
        callback_();
    }

    Timestamp expiration() const { return expiration_; }
    bool repeat() const { return repeat_; }
    int sequence() const { return sequence_; }

    void restart(Timestamp now);

    static int numCreated() { return s_numCreated_.load(std::memory_order_relaxed); }
    
private:
    const TimerCallback callback_;
    Timestamp expiration_;
    const double interval_;
    const bool repeat_;
    const int sequence_;
    static std::atomic_int s_numCreated_;
};
