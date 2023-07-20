#include "TcpClient.h"

#include "Logger.h"
#include "Connector.h"
#include "EventLoop.h"

#include "stdio.h"
#include "string.h"

void RemoveConnection(EventLoop* loop, const TcpConnectionPtr& conn)
{
    loop->queueInLoop(std::bind(&TcpConnection::connectDestroyed,conn));
}

void removeConnector(const ConnectorPtr& connector)
{
  //connector->
}

void defaultMessageCallback(const TcpConnectionPtr&,
                                        Buffer* buf,
                                        Timestamp)
{
  buf->retrieveAll();
}

void defaultConnectionCallback(const TcpConnectionPtr& conn)
{
  LOG_INFO("%s -> %s is up\n",
           conn->localAddress().toIpPort().c_str(),
           conn->peerAddress().toIpPort().c_str()); 
}

TcpClient::TcpClient(EventLoop* loop,
                     const InetAddress& serverAddr,
                     const std::string& nameArg)
  : loop_(loop),
    connector_(new Connector(loop, serverAddr)),
    name_(nameArg),
    connectionCallback_(defaultConnectionCallback),
    messageCallback_(defaultMessageCallback),
    retry_(false),
    connect_(true),
    nextConnId_(1)
{
    connector_->setNewConnectionCallback(
        std::bind(&TcpClient::newConnection, this, std::placeholders::_1));
    
    LOG_INFO("tcpclient::tcpclient[%s]-conncetor%p\n",name_.c_str(),connector_.get());
}

TcpClient::~TcpClient()
{
    LOG_INFO("tcpclient::~tcpclient[%s]-conncetor%p\n",name_.c_str(),connector_.get());
    TcpConnectionPtr conn;
    bool unique = false;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        unique = connection_.unique();//todo check unique
        conn = connection_;
    }
    if (conn)
    {
        // FIXME: not 100% safe, if we are in different thread
        CloseCallback cb = std::bind(&RemoveConnection, loop_, std::placeholders::_1);
        loop_->runInLoop(
            std::bind(&TcpConnection::setCloseCallback, conn, cb));  
    }
    else
    { 
        connector_->stop();
        loop_->runAfter(1, std::bind(&removeConnector, connector_));
    }
}

void TcpClient::connect()
{
    connect_ = true;
    connector_->start();
}

void TcpClient::disconnect()
{
    connect_ = false;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (connection_)
        {
            connection_->shutdown();
        }
    }
}

void TcpClient::stop()
{
    connect_ = false;
    connector_->stop();
}

void TcpClient::newConnection(int sockfd)
{
  //getpeeraddr
  struct sockaddr_in peeraddr;
  memset(&peeraddr, 0, sizeof peeraddr);
  socklen_t addrlen = static_cast<socklen_t>(sizeof peeraddr);
  if (::getpeername(sockfd, 
              reinterpret_cast<struct sockaddr*>(&peeraddr), 
              &addrlen) < 0)
  {
    LOG_ERROR("getpeeraddr\n");
  }

  InetAddress peerAddr(peeraddr);
  char buf[32];
  snprintf(buf, sizeof buf, ":%s#%d", peerAddr.toIpPort().c_str(), nextConnId_);
  ++nextConnId_;
  std::string connName = name_ + buf;

  //geetlocaladdr
  struct sockaddr_in localaddr;
  memset(&localaddr, 0, sizeof localaddr);
  addrlen = static_cast<socklen_t>(sizeof localaddr);
  if (::getsockname(sockfd, 
              reinterpret_cast<struct sockaddr*>(&localaddr), 
              &addrlen) < 0)
  {
    LOG_ERROR("getpeeraddr\n");
  }

  InetAddress localAddr(localaddr);
  // FIXME poll with zero timeout to double confirm the new connection
  // FIXME use make_shared if necessary
  TcpConnectionPtr conn(new TcpConnection(loop_,
                                          connName,
                                          sockfd,
                                          localAddr,
                                          peerAddr));

  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  conn->setCloseCallback(
      std::bind(&TcpClient::removeConnection, this, std::placeholders::_1)); // FIXME: unsafe
  {
    std::unique_lock<std::mutex> lock(mutex_);
    connection_ = conn;
  }
  conn->connectEstablished();
}

void TcpClient::removeConnection(const TcpConnectionPtr& conn)
{
  {
    std::unique_lock<std::mutex> lock(mutex_);
    connection_.reset();
  }

  loop_->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
  if (retry_ && connect_)
  {
    LOG_INFO("reconnecting\n");
    connector_->restart();
  }
}



