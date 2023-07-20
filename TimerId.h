#pragma once

class Timer;

class TimerId 
{
public:
    TimerId()
        : timer_(nullptr),
          sequence_(0)
    {    
    }

    TimerId(Timer* timer, int seq)
        : timer_(timer),
          sequence_(seq)
    {
    }

    Timer* getTimer()
    {
        return timer_;
    }
    friend class TimerQueue;

private:
    Timer* timer_;
    int sequence_;
};