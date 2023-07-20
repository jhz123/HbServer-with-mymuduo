#pragma once

#include "TcpConnection.h"

#include <mutex>

class Connector;
using ConnectorPtr = std::shared_ptr<Connector>;

class TcpClient : noncopyable
{
public:
    TcpClient(EventLoop* loop,
              const InetAddress& serverAddr,
              const std::string& nameArg);
    ~TcpClient();

    void connect();
    void disconnect();
    void stop();

  TcpConnectionPtr connection()
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return connection_;
  }

  EventLoop* getLoop() const { return loop_; }
  bool retry() const { return retry_; }
  void enableRetry() { retry_ = true; }

  const std::string& name() const
  { return name_; }

  void setConnectionCallback(ConnectionCallback cb)
  { connectionCallback_ = std::move(cb); }

  /// Set message callback.
  /// Not thread safe.
  void setMessageCallback(MessageCallback cb)
  { messageCallback_ = std::move(cb); }

  /// Set write complete callback.
  /// Not thread safe.
  void setWriteCompleteCallback(WriteCompleteCallback cb)
  { writeCompleteCallback_ = std::move(cb); }

private:
    //not thread safe
    void newConnection(int sockfd);
    void removeConnection(const TcpConnectionPtr& conn);
    
    EventLoop* loop_;
    ConnectorPtr connector_;
    const std::string name_;
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    std::atomic_bool retry_;
    std::atomic_bool connect_;
    int nextConnId_;
    std::mutex mutex_;
    TcpConnectionPtr connection_ ; //need to be guard by mutex_
};