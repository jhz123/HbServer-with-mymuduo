#include "Connector.h"

#include "Logger.h"
#include "Channel.h"
#include "EventLoop.h"

#include "sys/types.h"
#include "sys/socket.h"

#include <errno.h>

const int Connector::kMaxRetryDelayMs;

Connector::Connector(EventLoop* loop, const InetAddress& serverAddr)
                : loop_(loop),
                  serverAddr_(serverAddr),
                  connect_(false),
                  state_(kDisconnected),
                  retryDelayMs_(kInitRetryDelayMs)
{
    LOG_DEBUG("ctor[%p]\n",this);
}

Connector::~Connector()
{
  LOG_DEBUG("dtor[%p]\n",this);
}

void Connector::start()
{
  connect_ = true;
  loop_->runInLoop(std::bind(&Connector::startInLoop, this));
}

void Connector::startInLoop()
{
  if (connect_)
  {
    connect();
  }
  else
  {
    LOG_DEBUG("do not connect\n");
  }
}

void Connector::stop()
{
  connect_ = false;
  loop_->queueInLoop(std::bind(&Connector::stopInLoop, this));//consider cancel timer
}

void Connector::stopInLoop()
{
  if (state_ == kConnecting)
  {
    setState(kDisconnected);
    int sockfd = removeAndResetChannel();
    retry(sockfd);
  }
}

void Connector::connect()
{
  int sockfd = ::socket(serverAddr_.family(), SOCK_STREAM, IPPROTO_TCP);
  int ret = ::connect(sockfd, (sockaddr*)serverAddr_.getSockAddr(), 
                    static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
  int savedErrno = (ret == 0) ? 0 : errno;
  switch (savedErrno)
  {
    case 0:
    case EINPROGRESS:
    case EINTR:
    case EISCONN:
      connecting(sockfd);
      break;

    case EAGAIN:
    case EADDRINUSE:
    case EADDRNOTAVAIL:
    case ECONNREFUSED:
    case ENETUNREACH:
      retry(sockfd);
      break;

    case EACCES:
    case EPERM:
    case EAFNOSUPPORT:
    case EALREADY:
    case EBADF:
    case EFAULT:
    case ENOTSOCK:
      LOG_ERROR("connect error in Connector::startInLoop %d\n",savedErrno);
      if (::close(sockfd) < 0)
      {
        LOG_ERROR("sockets::close\n");
      }
      break;

    default:
      LOG_ERROR("unexpected error in Connector::startInLoop\n");
      if (::close(sockfd) < 0)
      {
        LOG_ERROR("sockets::close\n");
      }
      break;
  }
}

void Connector::restart()
{
  setState(kDisconnected);
  retryDelayMs_ = kInitRetryDelayMs;
  connect_ = true;
  startInLoop();
}

void Connector::connecting(int sockfd)
{
  setState(kConnecting);
  channel_.reset(new Channel(loop_,sockfd));
  channel_->setWriteCallback(std::bind(&Connector::handleWrite, this));
  channel_->setErrorCallback(std::bind(&Connector::handleError, this));

  channel_->enableWriting();
}

int Connector::removeAndResetChannel()
{
  channel_->disableAll();
  channel_->remove();
  int sockfd = channel_->fd();
  //cannot reset channel here
  loop_->queueInLoop(std::bind(&Connector::resetChannel, this));
  return sockfd;
}

void Connector::resetChannel()
{
  channel_.reset();
}

void Connector::handleWrite()
{
  LOG_INFO("Connector::handlewrite %d",state_);

  if (state_ == kConnecting)
  {
    int sockfd = removeAndResetChannel();
    int optval;
    socklen_t optlen = static_cast<socklen_t>(sizeof optval);
    int err;
    if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
      err = errno;
    }
    else
    {
      err = optval;
    }

    if (err)
    {
      LOG_DEBUG("Connector::handleWrite - so_error = %d\n" ,err);
      retry(sockfd);
    }
    else 
    {
      setState(kConnected);
      if (connect_)
      {
        newConnectionCallback_(sockfd);
      }
      else
      {
        if (::close(sockfd) < 0)
        {
          LOG_ERROR("sockets::close\n");
        }
      }
    }
  }
}

void Connector::handleError()
{
  LOG_ERROR("Connector::handleError state=%d\n",state_);
  if (state_ == kConnecting)
  {
    int sockfd = removeAndResetChannel();
    retry(sockfd);
  }
}

void Connector::retry(int sockfd)
{
  if (::close(sockfd) < 0)
  {
    LOG_ERROR("sockets::close\n");
  }
  setState(kDisconnected);
  if (connect_)
  {
    LOG_INFO("Connector::retry - Retry connecting to %s in %d milliseconds\n",
            serverAddr_.toIpPort().c_str(),retryDelayMs_ );
    loop_->runAfter(retryDelayMs_/1000.0,
                    std::bind(&Connector::startInLoop, shared_from_this()));
    retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);
  }
  else
  {
    LOG_DEBUG("do not connect\n");
  }
}
