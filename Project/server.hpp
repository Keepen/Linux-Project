
//实现一个server类, 完成服务端的功能
#pragma once
#include <stdlib.h>
#include <fstream>
#include "tcpsocket.hpp"
#include "epollwait.hpp"
#include "http.hpp"
#include "threadpool.hpp"
#include <boost/filesystem.hpp>

#define WWW_ROOT "./www"



class Server{
  public:
    static void ThreadHandler(int sockfd){
      TcpSocket sock;
      sock.SetFd(sockfd);
      int status;
      HttpRequest req;        //头部解析对象
      HttpResponse rsp;       //相应对象 -- 解析完毕后进行填充对应信息
      //从clisock接收请求数据，进行解析
      status = req.RequestParse(sock); //解析头部,返回状态码
      if(status != 200){
        //解析失败，则直接响应错误
        rsp._status = status;
        rsp.ErrorProcess(sock);
        sock.Close();
        return;
      }
      cout << "------------------------------------" << endl;
      //处理
      HttpProcess(req, rsp);
      //将处理结果响应给客户端
      rsp.NormalProcess(sock);
      //当前采用短链接，处理完毕后直接关闭套接字
      sock.Close();
      return;
    }

    //断点续传
    static bool RangeDownload(std::string& path, std::string& range, std::string& body){
      //Range:bytes=start-end;
      std::string unit = "bytes=";
      size_t pos = range.find(unit);
      if(pos == std::string::npos){
        return false;
      }
      pos += unit.size();
      size_t pos2 = range.find("-", pos);
      if(pos2 == std::string::npos){
        return false;
      }
      std::string start = range.substr(pos, pos2 - pos);
      std::string end = range.substr(pos2 + 1);
      std::stringstream tmp;
      int64_t dig_start, dig_end;
      tmp << start;
      tmp >> dig_start;
      tmp.clear();
      if(end.size() == 0){
        dig_end = boost::filesystem::file_size(path) - 1;
      }
      else{
        tmp << end;
        tmp >> dig_end;
      }
      int64_t len = dig_end - dig_start + 1;
      //读文件
      body.resize(len);
      std::ifstream file(path);
      if(!file.is_open()){
        return false;
      }
      file.seekg(dig_start, std::ios::beg);
      file.read(&body[0], len);
      if(!file.good()){
        std::cerr << "断点续传中，读文件错误！！！" << endl;
          return false;
      }
      file.close();
      return true;
    }

    static bool HttpProcess(HttpRequest& req, HttpResponse& rsp){
      //若请求是POST -- CGI多进程处理
      //若请求是GET  -- 但是查询字符串不为空--CGI处理
      //请求时GET    -- 并且查询字符串为空 -
      //    若请求的是目录 -- 查看
      //    若请求的是文件 -- 下载
      std::string realpath = WWW_ROOT + req._path;
      if(!boost::filesystem::exists(req._path)){
        rsp._status = 404;
        cout << realpath << endl;
        return false;
      }
      if((req._method == "GET" && req._param.size() != 0)
          || req._method == "POST"){
        //此时是一个文件上传请求
        CGIProcess(req, rsp);
      }
      else{
        //此时是一个基本的文件下载/目录列表请求
        if(boost::filesystem::is_directory(realpath)){
          //查看目录请求
          ListShow(realpath, rsp._body);
          rsp.SetHeader("Content-Type", "text/html");
        }
        else{
          //普通文件下载请求
          auto it = req._headers.find("Range");
          if(it == req._headers.end()){
            Download(realpath, rsp._body);    //将文件中的数据放到body中
            rsp.SetHeader("Content-Type", "application/octet-stream");  //流式文件供用户下载
            rsp.SetHeader("Accept-Ranges","Bytes");
            rsp.SetHeader("Etag","abcdefg");
          }
          else{
            std::string range = it->second;
            RangeDownload(realpath, range, rsp._body);
            rsp.SetHeader("Content-Type", "application/octet-stream");
            std::string unit = "bytes=";
            size_t pos = range.find(unit);
            if(pos == std::string::npos){
              return false;
            }
            std::stringstream tmp;
            tmp << range.substr(pos + unit.size());
            tmp << "/";
            int64_t len = boost::filesystem::file_size(realpath);
            tmp << len;
            rsp.SetHeader("Content-Range", tmp.str());
            rsp._status = 206;  //断点续传
            return true;
          }
        }
      }
      rsp._status = 200;
      return true;
    }


    //CGI处理函数 -- 多进程处理
    static bool CGIProcess(HttpRequest& req, HttpResponse& rsp){
      int pipe_in[2], pipe_out[2];
      if(pipe(pipe_in) < 0 || pipe(pipe_out) < 0){
        cout << "创建管道失败！！！" << endl;
        return false;
      }
      int pid = fork();
      if(pid < 0){
        return false;
      }
      //子进程 -- 设置环境变量
      else if(pid == 0){
        close(pipe_in[0]);    //父进程读， 子进程写， 关闭读端
        close(pipe_out[1]);   //父进程写， 子进程读， 关闭写端
        dup2(pipe_out[0], 0); //将标准输入重定向到管道
        dup2(pipe_in[1], 1);
        setenv("METHOD", req._method.c_str(), 1); //设置环境变量
        setenv("PATH", req._path.c_str(), 1);
        for(auto& i: req._headers){
          setenv(i.first.c_str(), i.second.c_str(), 1);
        }
        std::string realpath = WWW_ROOT + req._path;
        execl(realpath.c_str(), realpath.c_str(), NULL);
        exit(0);
      }
      //父进程 -- 将正文写入
      else{
        close(pipe_in[1]);
        close(pipe_out[0]);
        write(pipe_out[1], &req._body[0], req._body.size());
        while(1){
          char buf[1024] = {0};
          int ret = read(pipe_in[0], buf, 1024);  //将管道中的数据读入buf中
          //管道所有写端关闭
          if(ret == 0){
            break;
          }
          buf[ret] = '\0';
          rsp._body += buf;
        }
        rsp._status = 200;
        close(pipe_in[0]);
        close(pipe_out[1]);
        return true;
      }
    }

    //文件下载
    static bool Download(std::string& path, std::string& body){
      //获取文件大小
      int64_t fsize = boost::filesystem::file_size(path);
      body.resize(fsize);

      std::ifstream file(path);
      if(!file.is_open()){
        std::cerr << "文件打开失败！！！" << endl;
        return false;
      }

      //文件打开成功，读文件
      file.read(&body[0], fsize);
      if(!file.good()){
        std::cerr << "读取文件错误！！！" << endl;
        return false;
      }
      file.close();
      return true;
    }

    //文件列表显示函数
    static bool ListShow(std::string& path, std::string& body){
      std::string www = WWW_ROOT;
      size_t pos = path.find(www);
      if(pos == std::string::npos){
        cout << "路径非法！！！" << endl;
        return false;
      }
      std::string req_path = path.substr(www.size()); 
      std::stringstream tmp;
      tmp << "<html><head><style>";
      tmp << "*{margin:0;}";
      tmp << ".main-window{height:100%;width:80%;margin:0 auto;}";
      tmp << ".upload{position:relative;height:20%;width:100%;background-color:#33c0b9;text-align:center;}";
      tmp << ".listshow{position:relative;height:80%;width:100%;background:#6fcad6;}";
      tmp << "</style></head>";
      tmp << "<body><div class='main-window'>";
      tmp << "<div class='upload'>";
      //上传的html语句
      tmp << "<form action='/upload' method='POST'>";
      tmp << "enctype='multupart/form-data'>";
      tmp << "<div class='upload-btn'>";
      tmp << "<input type='file' name='fileupload'>";
      tmp << "<input type='submit' name='submit' >";
      tmp << "</div></form>";

      tmp << "</div><hr />";
      tmp << "<div class='listshow'><ol>";
      //组织每个文件信息结点
      boost::filesystem::directory_iterator begin(path);
      boost::filesystem::directory_iterator end(path);
      for(;begin != end;++begin){
        int64_t mtime, ssize;
        std::string pathname = begin->path().string();
        std::string name = begin->path().filename().string(); 
        std::string uri = req_path + name;

        //如果是目录：
        if(boost::filesystem::is_directory(pathname)){
          tmp << "<li><strong><a href='";
          tmp << uri << "'>";
          tmp << name << "/";
          tmp << "</a><br /></strong>";
          tmp << "<small>filetype: directory ";
          tmp << "</small></li>";
        }
        //是文件，就要描述其大小
        else{
          ssize = boost::filesystem::file_size(begin->path());
          mtime = boost::filesystem::last_write_time(begin->path());
          tmp << "<li><strong><a href='";
          tmp << uri << "'>";
          tmp << name; 
          tmp << "</a><br /></strong>";
          tmp << "<small>modified:";
          tmp << mtime;
          tmp << "<br />filetype:application-octstream ";
          tmp << ssize / 1024 << "KB";
          tmp << "</small></li>";
        }
      }
      tmp << "</ol></div><hr /></div></body></html>";
      body = tmp.str();
      return true;
    }

  public:
    bool Start(int port){
      bool ret;
      ret = _lst_sock.SocketInit(port); //创建一个套接字
      if(ret == false){
        return false;
      }
      /*
         ret = _epoll.Init();
         if(ret == false){
         return false;
         }
         */

      ret = _pool.PoolInit();
      if(ret == false){
        return false;
      }
      /*
         _epoll.Add(_lst_sock);
         */
      //获取新连接
      while(1){
        TcpSocket cli_sock;
        ret =_lst_sock.Accept(cli_sock);
        if(ret == false){
          continue;   
        }
        cli_sock.SetNonBlock();
        ThreadTask tt(cli_sock.GetFd(), ThreadHandler);
        _pool.TaskPush(tt);
        /*
           std::vector<TcpSocket> list;
           ret = _epoll.Wait(list);
           if(ret == false){
           continue;
           }
           for(int i = 0;i < list.size();++i){
           if(list[i].GetFd() == _lst_sock.GetFd()){
           TcpSocket cli_sock;
           ret =_lst_sock.Accept(cli_sock);
           if(ret == false){
           continue;   
           }
           cli_sock.SetNonBlock();
           _epoll.Add(cli_sock);
           }
        //如果不是监听socket，就将其抛到线程池里面去处理
        else{
        ThreadTask tt(list[i].GetFd(), ThreadHandler);
        _pool.TaskPush(tt);
        _epoll.Del(list[i]);  //将其移除
        }
        }
        */
      }
      _lst_sock.Close();
      return true;
    }

  private:
    TcpSocket _lst_sock;
    ThreadPool _pool;
    Epoll _epoll;
};
