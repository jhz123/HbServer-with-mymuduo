#pragma once

#include "noncopyable.h"
#include "InetAddress.h"

#include <functional>
#include <memory>

class Channel;
class EventLoop;

class Connector : noncopyable,
                  public std::enable_shared_from_this<Connector>
// connector类可以从自身获取一个shared_ptr对象
// 避免手动管理对象的内存分配和释放
{
public:
    using NewConnectionCallback = std::function<void (int sockfd)>;

    Connector(EventLoop* loop, const InetAddress& serverAddr);
    ~Connector();

    void setNewConnectionCallback(const NewConnectionCallback& cb)
    { newConnectionCallback_ = cb;}

    void start();
    void restart();
    void stop();

    const InetAddress& serverAddress() const { return serverAddr_; }

private:
    enum States { kDisconnected, kConnecting, kConnected };
    static const int kMaxRetryDelayMs = 30 * 1000;
    static const int kInitRetryDelayMs = 500;

    void setState(States s) { state_ = s; }
    void startInLoop();
    void stopInLoop();
    void connect();
    void connecting(int sockfd);
    void handleWrite();
    void handleError();
    void retry(int sockfd);
    int removeAndResetChannel();
    void resetChannel();

    EventLoop* loop_;
    InetAddress serverAddr_;
    bool connect_; // atomic
    States state_;  // FIXME: use atomic variable
    std::unique_ptr<Channel> channel_;
    NewConnectionCallback newConnectionCallback_;
    int retryDelayMs_;
};