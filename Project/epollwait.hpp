//封装一个Epoll类来实现并发
#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <sys/epoll.h>
#include "tcpsocket.hpp"
using std::cout;
using std::endl;
#define MAX_EPOLL 1024

class Epoll{
  public:
    Epoll():_epfd(-1){}

    bool Init(){
      _epfd = epoll_create(MAX_EPOLL);
      if(_epfd < 0){
        cout << "create epoll error" << endl;
        return false;
      }
      return true;
    }

    bool Add(TcpSocket& sock){
      struct epoll_event ev;
      int fd = sock.GetFd();
      ev.events = EPOLLIN | EPOLLET;
      ev.data.fd = fd;
      int ret = epoll_ctl(_epfd, EPOLL_CTL_ADD, fd, &ev);
      if(ret < 0){
        cout << "append monitor error" << endl;
        return false;
      }
      return true;
    }

    bool Del(TcpSocket& sock){
      int fd = sock.GetFd();
      int ret = epoll_ctl(_epfd, EPOLL_CTL_DEL, fd, NULL);
      if(ret < 0){
        cout << "remove monitor error" << endl;
        return false;
      }
      return true;
    }

    bool Wait(std::vector<TcpSocket>& list, int timeout = 3000){
      struct epoll_event evs[MAX_EPOLL];
      int nfds = epoll_wait(_epfd, evs, MAX_EPOLL, timeout);
      if(nfds < 0){
        cout << "epoll monitor error" << endl;
        return false;
      }
      else if(nfds == 0){
        //cout << "epoll wait timeout" << endl;
        return false;
      }
      for(int i = 0;i < nfds;++i){
        int fd = evs[i].data.fd;
        TcpSocket sock;
        sock.SetFd(fd);
        list.push_back(sock);
      }
      return true;
    }

  private:
    int _epfd;
};
