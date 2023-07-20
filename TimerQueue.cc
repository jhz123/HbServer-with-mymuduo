#include "TimerQueue.h"

#include "Logger.h"
#include "EventLoop.h"
#include "Timer.h"
#include "TimerId.h"

#include <sys/timerfd.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

int createTimerfd()
{
    int timerfd = ::timerfd_create(CLOCK_MONOTONIC,
                                    TFD_NONBLOCK | TFD_CLOEXEC);
    if (timerfd < 0)
    {
        LOG_FATAL("Failed in timerfd_create\n");
    }
    return timerfd;
}

struct timespec howMuchTimeFromNow(Timestamp when)
{
    int64_t microseconds = when.microSecondsSinceEpoch()
                            - Timestamp::now().microSecondsSinceEpoch();
    if (microseconds < 100)
    {
        microseconds = 100;
    }
    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(microseconds / 1000000);
    ts.tv_nsec = static_cast<long>((microseconds % 1000000) * 1000);
    return ts;
}

void readTimerfd(int timerfd, Timestamp now)
{
    uint64_t howmany;
    ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
    LOG_INFO("TimerQueue::handleRead() %lu at %s\n",howmany,now.toString().c_str());
    if (n != sizeof howmany)
    {
        LOG_ERROR("TimerQueue::handleRead() reads %ld bytes instead of 8\n",n);
    }
}

void resetTimerfd(int timerfd, Timestamp expiration)
{
    struct itimerspec newValue;
    struct itimerspec oldValue;
    memset(&newValue, 0, sizeof newValue);
    memset(&oldValue, 0, sizeof oldValue);
    newValue.it_value = howMuchTimeFromNow(expiration);
    int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
    if (ret)
    {
        LOG_ERROR("timerfd_settime()\n");
    }
}

TimerQueue::TimerQueue(EventLoop* loop)
    : loop_(loop),
      timerfd_(createTimerfd()),
      timerfdChannel_(loop,timerfd_),
      timers_(),
      callingExpiredTimers_(false)
{
    timerfdChannel_.setReadCallback(std::bind(&TimerQueue::handleRead,this));
    timerfdChannel_.enableReading();
}

TimerQueue::~TimerQueue()
{
    timerfdChannel_.disableAll();
    timerfdChannel_.remove();
    ::close(timerfd_);
    for (const Entry& timer : timers_)
    {
        delete timer.second;
    }
}

TimerId TimerQueue::addTimer(TimerCallback cb,
                            Timestamp when,
                            double interval)
{
    Timer* timer = new Timer(std::move(cb), when, interval);
    loop_->runInLoop(std::bind(&TimerQueue::addTimerInLoop, this, timer));
    return TimerId(timer, timer->sequence());
}

void TimerQueue::cancel(TimerId timerId)
{
    loop_->runInLoop(std::bind(&TimerQueue::cancelInLoop, this, timerId));
}

void TimerQueue::addTimerInLoop(Timer* timer)
{
    if (loop_->isInLoopThread())
    {
        bool earliestChanged = insert(timer);

        if (earliestChanged)
        {
            resetTimerfd(timerfd_, timer->expiration());
        }
    }
    else 
    {
        LOG_FATAL("trying to add a timer in another thread\n");
    }
}

void TimerQueue::cancelInLoop(TimerId timerId)
{
    if (!loop_->isInLoopThread())
    {
        LOG_FATAL("trying to add a timer in another thread\n");
        return;
    }
    assert(timers_.size() == activeTimers_.size());
    ActiveTimer timer(timerId.timer_, timerId.sequence_);
    ActiveTimerSet::iterator it = activeTimers_.find(timer);
    if (it != activeTimers_.end())
    {
        size_t n = timers_.erase(Entry(it->first->expiration(), it->first));
        delete it->first; // FIXME: no delete please
        activeTimers_.erase(it);
    }
    else if (callingExpiredTimers_)
    {
        cancelingTimers_.insert(timer);
    }
    assert(timers_.size() == activeTimers_.size());
}

void TimerQueue::handleRead()
{
    if (!loop_->isInLoopThread())
    {
        LOG_FATAL("trying to add a timer in another thread\n");
        return;
    }
    Timestamp now(Timestamp::now());
    readTimerfd(timerfd_, now);

    std::vector<Entry> expired = getExpired(now);

    callingExpiredTimers_ = true;
    cancelingTimers_.clear();

    for (const Entry& it : expired)
    {
        it.second->run();
    }
    callingExpiredTimers_ = false;

    reset(expired, now);
}

std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
    std::vector<Entry> expired;
    Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
    TimerList::iterator end = timers_.lower_bound(sentry);
    std::copy(timers_.begin(), end, back_inserter(expired));
    timers_.erase(timers_.begin(), end);

    for (const Entry& it : expired)
    {
        ActiveTimer timer(it.second, it.second->sequence());
        size_t n = activeTimers_.erase(timer);
    }

    return expired;
}

void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
  Timestamp nextExpire;

  for (const Entry& it : expired)
  {
    ActiveTimer timer(it.second, it.second->sequence());
    if (it.second->repeat()
        && cancelingTimers_.find(timer) == cancelingTimers_.end())
    {
      it.second->restart(now);
      insert(it.second);
    }
    else
    {
      // FIXME move to a free list
      delete it.second; // FIXME: no delete please
    }
  }

  if (!timers_.empty())
  {
    nextExpire = timers_.begin()->second->expiration();
  }

  if (nextExpire.valid())
  {
    resetTimerfd(timerfd_, nextExpire);
  }
}

bool TimerQueue::insert(Timer* timer)
{
    if (!loop_->isInLoopThread())
    {
        LOG_FATAL("trying to add a timer in another thread\n");
        return false;
    }
    bool earliestChanged = false;
    Timestamp when = timer->expiration();
    TimerList::iterator it = timers_.begin();
    if (it == timers_.end() || when < it->first)
    {
        earliestChanged = true;
    }
    {
        std::pair<TimerList::iterator, bool> result
          = timers_.insert(Entry(when, timer));
        assert(result.second); (void)result;
    }
    {
        std::pair<ActiveTimerSet::iterator, bool> result
          = activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
        assert(result.second); (void)result;
    }

    return earliestChanged;
}

