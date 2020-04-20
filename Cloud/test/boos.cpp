#include <iostream>
#include <string>
#include <boost/filesystem.hpp>

int main(){
  std::string dir = "./";
  boost::filesystem::directory_iterator begin(dir);
  boost::filesystem::directory_iterator end;
  //不能进入目录来遍历,只能单层级目录来进行遍历
  for(;begin != end; ++begin){
    //过滤目录，只考虑普通文件
    //begin->status() 文件的属性信息
    std::string pathname = begin->path().string();  //带路径的文件名
    std::string name = begin->path().filename().string();//纯文件名
    if(boost::filesystem::is_directory(begin->status())){
      std::cout << pathname << "is dir\n";
    }
    //begin()->path() 
    std::cout << "pathname:  " << pathname << std::endl;
    std::cout << "name:\t" << name << std::endl;
  }
  return 0;
}
