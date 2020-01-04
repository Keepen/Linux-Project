#include <iostream>
#include <unistd.h>
#include <fstream>
#include <string>
#include <vector>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <sstream>
using std::endl;

#define WWW_ROOT "./www/"
class Boundary{
  public:
    int64_t _start_addr;
    int64_t _data_len;
    std::string _name;
    std::string _filename;
};

bool GetHeader(const std::string& key, std::string& val){
  std::string body;
  char* ptr = getenv(key.c_str());
  if(ptr == NULL){
    return false;
  }
  val = ptr;
  return true;
}

bool headerParse(std::string& header, Boundary& file){
  std::vector<std::string> list;
  boost::split(list, header, boost::is_any_of("\r\n"), boost::token_compress_on);
  for(int i = 0;i < list.size();++i){
    std::string sep = ": ";
    size_t pos = list[i].find(sep);
    if(pos == std::string::npos){
      return false;
    }
    std::string key = list[i].substr(0, pos);
    std::string val = list[i].substr(pos + sep.size());
    if(key != "Content-Disposition"){
      continue;
    }
    std::string name_field = "name=\"";
    std::string filename_sep = "filename=\"";
    pos = val.find(name_field);
    if(pos == std::string::npos){
      continue;
    }
    pos = val.find(filename_sep);
    if(pos == std::string::npos){
      return false;
    }
    pos += filename_sep.size();
    size_t next_pos = val.find("\"", pos);
    if(next_pos == std::string::npos){
      return false;
    }
    file._filename = val.substr(pos, next_pos - pos);
    file._name = "fileupload";
  }
  return true;
}



bool BoundaryParse(std::string& body, std::vector<Boundary>& list){
  std::string cont_b = "boundary=";
  std::string tmp;
  if(GetHeader("Content-Type", tmp) == false){
    return false;
  }
  size_t pos = tmp.find(cont_b);
  if(pos == std::string::npos){
    return false;
  }
  std::string boundary = tmp.substr(pos + cont_b.size());
  std::string dash = "--";
  std::string craf = "\r\n";
  std::string tail = "\r\n\r\n";
  std::string f_boundary = dash + boundary + craf;
  std::string m_boundary = craf + dash + boundary;
  
  size_t next_pos;
  pos = body.find(f_boundary);
  if(pos != 0){
    std::cerr << "first boundary error" << endl;
    return false;
  }
  pos += f_boundary.size();

  while(pos < body.size()){
    next_pos = body.find(tail, pos);   //找寻头部结尾
    if(next_pos == std::string::npos){
      return false;
    }
    std::string header = body.substr(pos, next_pos - pos);
    pos = next_pos + tail.size();           //数据的起始位置
    next_pos = body.find(m_boundary, pos);  //找\r\n--boundary，数据的结束位置
    if(next_pos == std::string::npos){
      return false;
    }
    int64_t offset = pos;
    //下一个boundary的起始地址 -- 数据的起始地址
    int64_t length = next_pos - pos;  //数据的长度
    next_pos += m_boundary.size();    //指向换行的位置
    pos = body.find(craf, next_pos);
    if(pos == std::string::npos){
      return false;
    }
    pos += craf.size(); //pos指向了下一个m_boundary的地址/最后一个boundary，数据的结尾
    //那么pos == body.size(), 跳出循环
    Boundary file;
    file._data_len = length;
    file._start_addr = offset;
    //解析头部
    if(headerParse(header, file) == false){
      std::cout << "header parse error" << endl;
      return false;
    }
    list.push_back(file);
  }

  return true;
}

bool StorageFile(std::string& body, std::vector<Boundary>& list){
  for(int i = 0;i < list.size();++i){
    if(list[i]._name != "fileupload"){
      continue;
    }
    std::string realpath = WWW_ROOT + list[i]._filename;
    std::ofstream file(realpath);
    if(!file.is_open()){
      std::cerr << "open file " << realpath << "failed" << endl;
      return false;
    }
    file.write(&body[list[i]._start_addr], list[i]._data_len);
    if(!file.good()){
      std::cerr << "write file error" << endl;
      return false;
    }
    file.close();
  }
  return true;
}



int main(int argc, char* argv[], char* env[]){
  std::string body;
  char* cont_len = getenv("Contene-Length");
  std::string err = "<html>Failed!!!</html>";
  std::string suc = "<html>Success!!!</html>";
  if(cont_len != NULL){
    std::stringstream tmp;
    tmp << cont_len;
    int64_t fsize;
    tmp >> fsize;
    body.resize(fsize);
    int rlen = 0;
    int ret = 0;
    while(rlen < fsize){
      ret = read(0, &body[0] + rlen, fsize - rlen);
      if(ret <= 0){
        exit(-1);
      }
      rlen += ret;
    }
    std::vector<Boundary> list;
    bool p_ret;
    p_ret = BoundaryParse(body, list);
    if(p_ret == false){
      std::cout << "boundary parse error" << endl;
      std::cout << err;
      return -1;
    }
    p_ret = StorageFile(body, list);
    if(p_ret == false){
      std::cout << "storage error" << endl;
      std::cout << err << endl;
      return -1;
    }
    std::cout << suc;
    return 0;
  }
  std::cout << err;
  return 0;
}
