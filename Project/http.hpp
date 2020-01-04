#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <sstream>
#include <boost/algorithm/string.hpp>
#include "tcpsocket.hpp"
using std::cout;
using std::endl;

class HttpRequest{
  public:
    std::string _method;    //请求方法
    std::string _path;      //请求路径
    std::unordered_map<std::string,std::string> _param;     //参数信息
    std::unordered_map<std::string,std::string> _headers;  //头部信息
    std::string _body;
  private:
    bool RecvHeader(TcpSocket& sock, std::string& header){      //接收头部
      //1.探测性接收大量数据
      while(1){
        std::string tmp;
        if(sock.RecvPeek(tmp) == false){
          return false;
        }
        cout << "tmp:[" << tmp << "]\n";
        //2.判断是否包含整个头部\r\n\r\n
        size_t pos;
        pos = tmp.find("\r\n\r\n", 0);  //第一次找到\r\n\r\n的下标
        cout << "pos:[" << pos << "]\n";
        //判断是否找到
        //3.判断当前接收的数据长度
        if(pos == std::string::npos && tmp.size() == 8192){
          return false;
        }
        else if(pos != std::string::npos){
          //4.若包含整个头部，则直接获取头部
          header.assign(&tmp[0], pos);
          size_t header_length = pos + 4;
          sock.Recv(header, header_length);
          return true;
        }
      }
    }


    //首行解析
    bool FirstLineParse(std::string& line){   //首行解析
      //GET / HTTP /1.1
      std::vector<std::string> line_list;
      boost::split(line_list, line, boost::is_any_of(" "), boost::token_compress_on);
      if(line_list.size() != 3){
        cout << "parse first line error" << endl;
        return false;
      }
      _method = line_list[0];
      size_t pos = line_list[1].find("?", 0);
      //判断是否有查询字符串
      if(pos  == std::string::npos){
        _path = line_list[1];
      }
      //有查询字符串，按照？分割
      else{
        _path = line_list[1].substr(0, pos);
        std::string query_string = line_list[1].substr(pos + 1);
        //query_string --- key=val&key=val
        std::vector<std::string> param_list;  //参数列表 -- 存放查询字符串键值对
        boost::split(param_list, query_string, boost::is_any_of("&"), boost::token_compress_on);
        for(auto i : param_list){
          size_t param_pos = -1;
          param_pos = i.find("=");
          if(param_pos == std::string::npos){
            cout << "查询字符串格式有误！！！" << endl;
            return false;
          }
          std::string key = i.substr(0, param_pos);
          std::string val = i.substr(param_pos + 1);
          _param[key] = val;
        }
      }
      return true;
    }
    bool PathIsLegal(){                       //路径解析
      return true;
    }
  public:

    //1.Http请求解析
    int RequestParse(TcpSocket& sock){
      //1.获取接收http头部
      std::string header;
      if(RecvHeader(sock, header) == false){
        return 400;   //表示客户端错误
      }
      //2.分割头部信息， 按照\r\n，得到一个list
      std::vector<std::string> header_list;
      boost::split(header_list, header, boost::is_any_of("\r\n"), boost::token_compress_on);
      //3.list[0] -- 首行，首行解析
      if(FirstLineParse(header_list[0]) == false){
        return 400;
      }
      //4.头部分割解析
      //key:val\r\n 
      size_t pos = 0;
      for(int i = 1;i < header_list.size();++i){
        pos = header_list[i].find(":");
        if(pos == std::string::npos){
          cout << "头部解析有误！！！" << endl;
          return 400;
        }
        std::string key = header_list[i].substr(0, pos);
        std::string val = header_list[i].substr(pos + 2);
        _headers[key] = val;
      }
      //5.请求信息校验
      
      //6.接收正文
      auto it = _headers.find("Content-Length");
      if(it == _headers.end()){
        std::stringstream tmp;
        tmp << it->second;
        int64_t file_len;
        tmp << file_len;
        sock.Recv(_body, file_len);
      }
      return 200;
    }
};



class HttpResponse{
  private:
    std::string GetDesc(){
      switch(_status){
        case 400:
          return "Bad Request";
        case 404:
          return "Not Found";
        case 200:
          return "OK";
      }
      return "Unknown";
    }
  public:
    int _status;
    std::unordered_map<std::string, std::string> _headers;
    std::string _body;    //正文
  public:
    bool SetHeader(const std::string key, const std::string val){
      _headers[key] = val;
      return true;
    }
    bool ErrorProcess(TcpSocket& sock){
      return true;
    }
    bool NormalProcess(TcpSocket& sock){
      //组织首行
      std::stringstream tmp;

      tmp << "HTTP/1.1" << " " << _status << " " << GetDesc();
      tmp << "\r\n";
      if(_headers.find("Content-Length") == _headers.end()){
        std::string len = std::to_string(_body.size());
        _headers["Content-Length"] = len;
      }
      for(auto i: _headers){
        tmp << i.first << ": " << i.second << "\r\n";
      }
      tmp << "\r\n";    //空行
      sock.Send(tmp.str());
      sock.Send(_body);
      return true;
    }
};


