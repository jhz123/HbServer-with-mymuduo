#include <mymuduo/EventLoop.h>
#include <mymuduo/TimerQueue.h>

#include <iostream>

void printTime()
{
    std::cout << "Current time:" << Timestamp::now().toString() << std::endl;
}

int main()
{
    EventLoop loop;
    TimerQueue timerQueue(&loop);

    TimerId timerId = timerQueue.addTimer(printTime, Timestamp::now(), 2.0);

    loop.loop();

    return 0;
}
