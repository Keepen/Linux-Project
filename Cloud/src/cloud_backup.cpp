#include "cloud_backup.hpp"
#include <thread>
#include <iostream>
using namespace std;


void compress_test(char* argv[]){

  //argv[1] = 源文件名称
  //argv[2] = 压缩包名称
  _cloud_sys::CompressUtil::Compress(argv[1], argv[2]);
  std::string tmp(argv[2]);
  std::string file = tmp + ".txt";  //当前路径
  _cloud_sys::CompressUtil::UnCompress(argv[2], file.c_str());
}

void data_test(){
  _cloud_sys::DataManager data_manage("./test.txt");
  data_manage.InitLoad();
  std::vector<std::string> list;
  data_manage.GetAllName(&list);
  for(auto it:list){
    std::cout << it << endl;
  }
  list.clear();
  cout << "----------------------------" << endl;
  data_manage.NonCompressList(&list);
  for(auto it : list){
    std::cout << it << endl;
  }
  /*data_manage.Insert("a.txt", "a.txt");
  data_manage.Insert("b.txt", "b.txt.gz");
  data_manage.Insert("c.txt", "c.txt");
  data_manage.Insert("d.txt", "d.txt.gz");
  data_manage.Storage();
  */

}


void m_non_compress(){
  _cloud_sys::NonHotCompress ncom(GZFILE_DIR, BACKUP_DIR);
  ncom.Start();
  return;
}


void thr_httpserver(){
  _cloud_sys::Server srv;
  srv.Start();
  return;
}




int main(int argc, char* argv[]){
  
  bool ret = boost::filesystem::exists(GZFILE_DIR);
  if(ret == false){
    boost::filesystem::create_directory(GZFILE_DIR);
  }
  ret = boost::filesystem::exists(BACKUP_DIR);
  if(ret == false){
    boost::filesystem::create_directory(BACKUP_DIR);
  }
  //_cloud_sys::data_manage.Insert("vimdoc.gz.txt","vimdoc.gz.txt");

  //启动非热点文件压缩
  std::thread thr_compress(m_non_compress);
  //网络通信服务模块
  std::thread thr_server(thr_httpserver);


  thr_compress.join(); //等待线程退出
  thr_server.join();  
  _cloud_sys::Server server;
  server.Start();
  return 0;
}
