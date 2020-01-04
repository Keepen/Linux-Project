//套接字类：实现通讯的基本功能
#pragma once
#include <iostream>
#include <string>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using std::cout;
using std::cin;
using std::endl;


class TcpSocket{
  public:
    TcpSocket():_sockfd(-1){}
    int GetFd(){
      return _sockfd;
    }
    void SetFd(int fd){
      _sockfd = fd;
    }
    void SetNonBlock(){
      int flag = fcntl(_sockfd, F_GETFL, 0);
      fcntl(_sockfd, F_SETFL, flag | O_NONBLOCK);
    }
    bool SocketInit(int port){
      _sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if(_sockfd < 0){
        cout << "socket error!!!" << endl;
        return false;
      }
      //设置地址复用,允许建立端口相同但是IP不同的多个socket描述符
      int opt = 1;
      setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(int));
      struct sockaddr_in addr;
      addr.sin_addr.s_addr = INADDR_ANY; //表示“0.0.0.0”该IP地址
      addr.sin_port = htons(port);
      addr.sin_family = AF_INET;
      socklen_t len = sizeof(addr);
      int ret = bind(_sockfd, (sockaddr*)&addr, len);
      if(ret < 0){
        cout << "bind error" << endl;
        close(_sockfd);
        return false;
      }
      //默认并发连接数是10
      ret = listen(_sockfd, 10);
      if(ret < 0){
        cout << "listen error" << endl;
        close(_sockfd);
        return false;
      }
      return true;
    }

    bool Accept(TcpSocket& sock){
      struct sockaddr_in addr;
      socklen_t len = sizeof(addr);
      int fd = accept(_sockfd, (sockaddr*)&addr, &len);
      if(fd < 0){
        cout << "accept error" << endl;
        return false;
      }
      sock._sockfd = fd;
      sock.SetNonBlock();
      return true;
    }


    //试探性接收数据
    bool RecvPeek(std::string& buf){
      buf.clear();
      char tmp[8192] = {0};
      int ret = recv(_sockfd, tmp, 8192, MSG_PEEK);
      if(ret <= 0){
        //EAGAIN:非阻塞情况下，缓冲区内没有数据
        if(errno == EAGAIN){
          return true;
        }
        cout << "recv error" << endl;
        return false;
      }
      buf.assign(tmp, ret);
      return true;
    } 

    
    //真正接收数据了，接收指定长度的数据
    bool Recv(std::string& buf, int len){
      buf.resize(len);
      int rlen = 0;  //表示已经接收到的数据长度
      while(rlen < len){
        int ret = recv(_sockfd, &buf[0] + rlen, len - rlen, 0);
        if(ret <= 0){
          if(errno == EAGAIN){
            usleep(1000);
            continue;
          }
          return false;
        }
        rlen += len;
      }
      return true;
    }

    bool Send(const std::string& buf){
      int64_t len = 0;
      while(len < buf.size()){
        int ret = send(_sockfd, &buf[len], buf.size() - len, 0);
        if(ret < 0){
          if(errno == EAGAIN){
            usleep(1000);
            continue;
          }
          cout << "send error" << endl;
          return false;
        }
        len += ret;
      }
      return true;
    }
    bool Close(){
      close(_sockfd);
      return true;
    }
  private:
    int _sockfd;
};

