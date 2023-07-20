#include <mymuduo/TcpServer.h>
#include <mymuduo/TcpClient.h>
#include <mymuduo/Logger.h>
#include <mymuduo/Timer.h>


#include <iostream>
enum status{
    Master,
    Slave
};
class HbServer
{
public:
    HbServer(status sStatus, EventLoop *loop, const InetAddress &pairAddr, 
            const InetAddress &localAddr, const std::string &name)
        : sStatus_(sStatus)
        , loop_(loop)
        , server_(loop, localAddr, "hbserver")
        , client_(loop, pairAddr, "hbclient")
        , timerId_()
        {   
            //only start linkmonitor in master server
            if (sStatus_ == Master)
            {
                server_.setConnectionCallback(
                    std::bind(&HbServer::mOnConnection,this,std::placeholders::_1));
                server_.setMessageCallback(
                    std::bind(&HbServer::mOnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
                timerId_ = loop_->runAfter(15.0, std::bind(&HbServer::mLinkOut, this));
            }

            else
            {
                client_.setConnectionCallback(
                    std::bind(&HbServer::sOnConnection,this,std::placeholders::_1));
                client_.setMessageCallback(
                    std::bind(&HbServer::sOnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
                timerId_ = loop_->runAfter(15.0, std::bind(&HbServer::sLinkOut, this));
            }
        }


    void mLinkOut()
    {
        LOG_ERROR("slave server out link!\n");
        //do nothing but alarm in master server
    }

    void sLinkOut()
    {
        LOG_ERROR("master server out link!\n");
        //do operation to stop slave server standby and make it work
    }

    void hbHandle(const TcpConnectionPtr &conn)
    {
        conn->send("tik");
        LOG_INFO("send hb request!\n");
        loop_->runAfter(3.0, 
                    [this, conn]{hbHandle(conn);});
    }   

    //when get tik, restart link
    void restartLink()
    {
        loop_->cancel(timerId_);
        timerId_ = loop_->runAfter(15.0, std::bind(&HbServer::mLinkOut, this));
    }

    void start()
    {
        if (sStatus_ == Master)
        {
            server_.start();
        }
        else 
        {
            client_.connect();
        }
    }

private:
    void mOnConnection(const TcpConnectionPtr &conn)
    {
        if (conn->connected())
        {
            LOG_INFO("Connection UP : %s", conn->peerAddress().toIpPort().c_str());
            restartLink();
        }
        else
        {
            LOG_INFO("Connection DOWN : %s", conn->peerAddress().toIpPort().c_str());
            conn->shutdown();
        }

    }

    // send back response and restart link in master server
    void mOnMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time)
    {   
        LOG_INFO("master server get request!\n");
        //unpack rmsg -- an easy example
        std::string rmsg = buf->retrieveAllAsString();
        std::string msg = "";
        for (int i=0; i<rmsg.size(); i++)
        {
            if (isalpha(rmsg[i]))
            {
                msg += rmsg[i];
            }
        }
        std::cout << msg.size() << std::endl; 
        if (msg == "tik")
        {  
            std::string str = "tok";
            conn->send(str);
            LOG_INFO("master server send response!\n");
            restartLink();
        }
        else 
        {
            LOG_ERROR("get wrong hb message!\n");
        }
    }
    //set a hb timer,send a hb request every timeout
    void sOnConnection(const TcpConnectionPtr &conn)
    {
        if (conn->connected())
        {
            LOG_INFO("Connected to %s\n",conn->peerAddress().toIpPort().c_str());
            conn->send("tik");
            LOG_INFO("slave server send first request!\n");
            loop_->runAfter(3.0, 
                            [this, conn]{hbHandle(conn);});//using lambda to avoid std::bind mistake
            
        }
        else
        {
            LOG_INFO("Disconnected from,%s\n",conn->peerAddress().toIpPort().c_str());
        }
    }
    //only restartlink,do not need to send response,cause we send it in hb callback
    void sOnMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time)
    {   
        LOG_INFO("client server get response!\n");
        std::string msg = buf->retrieveAllAsString();
        if (msg == "tok")
        {  
            restartLink();
        }
        else 
        {
            LOG_ERROR("get wrong hb message!\n");
        }
    }

    status sStatus_; //whether master or slave
    EventLoop* loop_; //main loop with server and client
    TcpServer server_;
    TcpClient client_;
    TimerId timerId_;
};

int main()
{
    EventLoop loop;
    InetAddress pair(8002); //tofill
    InetAddress local(8002);
    HbServer server(Master, &loop, pair, local, "HbServer");//or slave
    server.start();
    loop.loop();
}