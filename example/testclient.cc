#include <mymuduo/TcpClient.h>
#include <mymuduo/Logger.h>
#include <mymuduo/EventLoop.h>


#include <iostream>
using namespace std;

void onConnection(const TcpConnectionPtr& conn)
{
    if (conn->connected())
    {
        LOG_INFO("Connected to %s\n",conn->peerAddress().toIpPort().c_str());
        conn->send("hello world!");
    }
    else
    {
        LOG_INFO("Disconnected from,%s\n",conn->peerAddress().toIpPort().c_str());
    }
}

void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp receiveTime)
{
    cout << "received" << buf->readableBytes() << " bytes from"
    << conn->peerAddress().toIpPort() << " at" << receiveTime.toString() << endl;
    cout << buf->retrieveAllAsString() << endl;
}

int main()
{
    EventLoop loop;

    InetAddress serverAddr(8002,"127.0.0.1");

    TcpClient client(&loop, serverAddr, "EchoClient");

    client.setConnectionCallback(onConnection);
    client.setMessageCallback(onMessage);

    client.connect();

    loop.loop();

    return 0;
}
