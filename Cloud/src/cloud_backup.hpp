#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <zlib.h>
#include <pthread.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include "httplib.h"


#define NONHOT_TIME 10            //表示10s没访问就是非热点了
#define INTERVAL_TIME 30          //每隔30s检测一次
#define BACKUP_DIR "./backup/"    //文件的备份路径   
#define GZFILE_DIR "./gzfile/"    //压缩包的文件路径
#define DATA_FILE "./list.backup" //数据管理模块的数据备份文件名称

namespace _cloud_sys{
  //文件读写
  class FileUtil{
    public:
      //从文件中读取所有内容
      static bool Read(const std::string& name,std::string* body){
        //指定二进制读取方式,否则按照文本读取时会发生多个字节只有一个字符的情况,导致读取的字符比实际的要小
        std::ifstream fs(name, std::ios::binary);
        if(fs.is_open() == false){
          std::cout << "open file " << name << " failed\n";
          return false;
        }
        //获取文件大小 boost::filesystem::file_size();
        int64_t fsize = boost::filesystem::file_size(name);
        body->resize(fsize);  //给body申请空间接收文件数据
        fs.read(&(*body)[0], fsize);
        if(fs.good() == false){
          std::cout << "file " << name << " read data failed\n";
          return false;
        }
        fs.close();
        return true;
      }

      //向文件中写入数据
      static bool Write(const std::string& name, const std::string& body){
        //输出流 -- ofstream默认打开文件的时候清空原有内容
        //当前策略是覆盖写入
        std::ofstream ofs(name, std::ios::binary);
        if(ofs.is_open() == false){
          std::cout << "open file " << name << " failed\n";
          return false;
        }
        ofs.write(&body[0], body.size()); 
        if(ofs.good() == false){
          std::cout << "file " << name << " write data failed\n";
          return false;
        }
        ofs.close();
        return true;
      }
  };



  class CompressUtil{
    public:
      //文件压缩-源文件名称-压缩包名称
      static bool Compress(const std::string& src, const std::string& dst){
        std::string body;
        FileUtil::Read(src, &body);   

        //打开压缩包
        gzFile gf = gzopen(dst.c_str(), "wb");
        if(gf == NULL){
          std::cout << "open file " << dst << " failed\n";
          return false;
        }
        //由于gzwrite的返回值是真正压缩的数据的大小
        //所以只有当其值为要压缩的文件的大小时，才证明压缩完毕
        //故需要循环压缩
        int wlen = 0;
        while(wlen < (int)body.size()){
          int ret = gzwrite(gf, &body[wlen], body.size() - wlen);
          if(ret == 0){
            std::cout << "file " << dst << " write compress data failed!\n";
            return false;
          }
          wlen += ret;
        }
        gzclose(gf);
        return true;
      }

      //文件解压缩-压缩包名称-源文件名称
      static bool UnCompress(const std::string& src, const std::string& dst){
        //只能边读边写，不能直接解压缩后写入文件，否则内存不足
        std::ofstream ofs(dst, std::ios::binary);
        if(ofs.is_open() == false){
          std::cout << "open file " << dst << " failed!\n";
          return false;
        }
        gzFile gf = gzopen(src.c_str(), "rb");
        if(gf == NULL){
          std::cout << "open file " << src << " failed!\n";
          ofs.close();  //注意关闭文件
          return false;
        }
        char tmp[4096] = {0};
        int ret = 0;
        //gzread(句柄，缓冲区， 缓冲区大小)
        //返回实际读取到的解压后的数据大小
        while((ret = gzread(gf, tmp, 4096)) > 0) {
          ofs.write(tmp, ret);
        }
        ofs.close();
        gzclose(gf);
        return true;
      }
  };



  //数据管理模块
  class DataManager{
    public:
      DataManager(const std::string& path):_back_file(path){
        pthread_rwlock_init(&_rwlock, NULL);
      }

      ~DataManager(){
        //销毁读写锁
        pthread_rwlock_destroy(&_rwlock); 
      }
      //1.判断文件是否存在
      bool Exsists(const std::string& name){
        //是否能够从_file_list找到这个文件信息
        //通过key查找val的一个过程
        //由于_file_list是全局有效，故需加锁保证线程安全
        //加读锁
        pthread_rwlock_rdlock(&_rwlock);
        auto it = _file_list.find(name);
        if(it == _file_list.end()){
          std::cout << "the file " << name << " not Exsists\n";
          pthread_rwlock_unlock(&_rwlock);
          return false;
        }
        pthread_rwlock_unlock(&_rwlock);
        return true;
      } 

      //2.判断文件是否已经压缩
      bool IsCompress(const std::string& name){
        //管理的数据：源文件名-压缩包名称
        //文件上传后，源文件名称和压缩包名称一致
        //文件压缩后，将压缩包名称更新为具体的包名 
        pthread_rwlock_rdlock(&_rwlock);
        auto it = _file_list.find(name);
        if(it == _file_list.end()){
          std::cout << "the iscompress file " << name << "not found\n";
          pthread_rwlock_unlock(&_rwlock);
          return false;
        }
        //源文件名 == 压缩包名称 -- 表示没有被压缩
        if(it->first == it->second){
          pthread_rwlock_unlock(&_rwlock);
          return false;
        }
        pthread_rwlock_unlock(&_rwlock);
        return true;
      }

      //3.获取未压缩文件列表
      bool NonCompressList(std::vector<std::string>* list){
        //遍历_file_list；将没有压缩的文件名称添加到list中
        pthread_rwlock_rdlock(&_rwlock);
        for(auto it = _file_list.begin();it != _file_list.end();++it){
          //源文件名称 == 压缩包名称  即没有被压缩的文件，就要添加到list中
          if(it->first == it->second){
            list->push_back(it->first);
          }
        }
        pthread_rwlock_unlock(&_rwlock);
        return true;
      } 

      //4.插入/更新数据
      bool Insert(const std::string& src, const std::string& dst){
        //添加新的云文件和压缩文件，初始化两者名字一样
        //或者直接修改也是一样的
        //加写锁
        pthread_rwlock_wrlock(&_rwlock);
        _file_list[src] = dst;
        pthread_rwlock_unlock(&_rwlock);
        //每次插入数据后，就更新保存的文件
        Storage();
        return true;
      }

      //5.获取所有文件名
      bool GetAllName(std::vector<std::string>* list){
        //直接遍历_file_licst 即可
        //加读锁
        pthread_rwlock_rdlock(&_rwlock);
        auto it = _file_list.begin();
        for(;it != _file_list.end();++it){
          //获取的是源文件名称，不是压缩包名称
          //展示文件列表时使用
          list->push_back(it->first);
        }
        pthread_rwlock_unlock(&_rwlock);
        return true;
      }

      //6.数据改变后持久化存储
      bool Storage(){
        //将_file_list中的数据进行持久化存储
        //数据对象进行持久化存储--序列化
        //按下面的格式存储文件名与压缩包名
        //src dst\r\n
        std::stringstream tmp;  //实例化一个string流对象
        pthread_rwlock_rdlock(&_rwlock);
        auto it = _file_list.begin();
        for(;it != _file_list.end();++it){
          tmp << it->first << " " << it->second << "\r\n";
        }
        pthread_rwlock_unlock(&_rwlock);
        //将读到的信息写入文件中
        //不用涉及线程安全，所以无需加锁
        FileUtil::Write(_back_file, tmp.str());
        return true;
      }

      //7.启动时，初始化加载原有数据
      bool InitLoad(){
        //从数据持久化存储文件中加载数据
        //1）将这个备份文件的数据读取出来
        std::string body;
        if(FileUtil::Read(_back_file, &body) == false){
          return false;
        }

        //2）进行字符串处理，按照\r\n进行分割
        //boost::split(vector, src, sep, flag);
        std::vector<std::string> list;
        boost::split(list, body, boost::is_any_of("\r\n"), boost::token_compress_off);
        
        //3）每一行按照空格进行分割-前边是key，后边是val
        for(auto i: list){
          size_t pos = i.find(" ");
          if(pos == std::string::npos){
            continue;
          }
          std::string key = i.substr(0, pos);
          std::string val = i.substr(pos+1);
          //4）将key/val添加到_file_list中
          Insert(key, val);
        }
        return true;
      }

    //8.根据源文件获取压缩包名称
      bool GetGzName(const std::string& src, std::string* dst){
        auto it = _file_list.find(src);
        if(it == _file_list.end()){
          return false;
        }
        *dst = it->second;
        return true;
      }

    private:
      std::string _back_file; //持久化数据存储文件名称
      std::unordered_map<std::string,std::string> _file_list;   //数据管理容器
      pthread_rwlock_t _rwlock;
  };
  //定义静态数据管理模块对象
  static _cloud_sys::DataManager data_manage(DATA_FILE);



  //非热点压缩模块
  class NonHotCompress{
    public:
      NonHotCompress(const std::string gz_dir, const std::string bu_dir):_gz_dir(gz_dir), _bu_dir(bu_dir){

      }
      //向外部提供的接口
      bool Start(){
        //1.是一个循环的，持续的过程 -- 每隔一段时间，判断有没有非热点文件，然后进行压缩
        //问题：什么是非热点文件 -》 当前时间 - 最后一次访问时间 > N秒 
        while(1){
          //1）获取所有的未压缩的文件列表
          std::vector<std::string> list;
          data_manage.NonCompressList(&list);

          //2）逐个判断这个文件是否是热点文件
          for(size_t i = 0;i < list.size();++i){
            bool ret = FileIsHot(list[i]);  
            if(ret == false){
              //3）如果是非热点文件，则进行压缩，然后删除源文件
              std::string s_filename = list[i];
              std::string d_filename = list[i] + ".gz";
              std::string src_name = _bu_dir + s_filename;   //源文件名称(带路径)
              //压缩包名称（带路径）
              std::string dst_name = _gz_dir + d_filename;
              if(CompressUtil::Compress(src_name, dst_name) == true){
                data_manage.Insert(s_filename, d_filename);    //更新数据信息  
                //删除源文件
                unlink(src_name.c_str());   //删除文件目录项
                std::cout << "已经删除源文件\n";
              }
            }
          }
          //4）休眠
          sleep(INTERVAL_TIME);   //每隔30s检测一次
        }
        return true;
      }
    private:
      //判断文件是否是热点文件
      bool FileIsHot(const std::string& name){
        //当前时间 - 最后一次访问时间 > Ns 就是非热点文件
        time_t cur_t = time(NULL);  //获取当前时间
        struct stat st;
        if(stat(name.c_str(), &st) < 0){
          std::cout << "get file stat " << name << " failed\n";
          return false;
        }
        if((cur_t -st.st_atim.tv_sec) > NONHOT_TIME){
          return false;
        }
        return true;
      }
    private:
      std::string _gz_dir;  //压缩后的文件存储路径
      std::string _bu_dir;  //压缩前文件路径
  };


  //http服务器的搭建
  class Server{
    public:
      //启动网络通信模块接口
      bool Start(){
        _server.Put("/(.*)", Upload);
        
        _server.Get("/list", List);
        //使用正则表达式进行捕捉字符串
        //  .* -- 表示匹配任意字符串， ()表示捕捉这个字符串，捕捉的到的字符串就可以在回调函数中使用
        //为了避免有文件名叫list，与list请求混淆，所以在前面加上/download
        _server.Get("/download/(.*)", Download);

        //使用任意ip，端口使用9000
        //搭建tcp服务器，进行http数据接收处理
        _server.listen("0.0.0.0", 9000);

        return true;
      }
    private:
      //文件上传处理回调函数
      static void Upload(const httplib::Request& req, httplib::Response& rsp){
        //req.method -- 请求方法
        //req.path -- 请求资源路径
        //req.headers -- 头部信息键值对
        //req.body -- 存放请求数据的正文
        //获取到请求的文件名
        std::string filename = req.matches[1];
        //组织文件路径名，文件备份在指定路径
        std::string pathname = BACKUP_DIR + filename;
        //写入正文数据， 文件不存在则会创建
        FileUtil::Write(pathname, req.body);
        rsp.status = 200;
        //上传后将其插入到数据管理模块
        data_manage.Insert(filename, filename);

        //set_content(正文数据， 正文数据长度， 正文类型：Content-Type)
        //rsp.set_content("upload", 6, "text/html");
        return;
      }
      //文件列表处理回调函数
      static void List(const httplib::Request& req, httplib::Response& rsp){
        std::vector<std::string> list;
        data_manage.GetAllName(&list);
        std::stringstream tmp;
        tmp << "<html><body><hr />";
        for(size_t i = 0;i < list.size();++i){
            tmp << "<a href='/download/" << list[i] << "'>" << list[i] << "</a></hr />";
        } 
        tmp << "</hr /></body></html>";
        rsp.set_content(tmp.str().c_str(), tmp.str().size(), "text/html");
        rsp.status = 200;
      }
      //文件下载处理回调函数
      static void Download(const httplib::Request& req,httplib::Response& rsp){

        //将要下载的文件名直接放到path里
        std::string filename = req.matches[1];
        if(data_manage.Exsists(filename) == false){
          rsp.status = 404;   //文件不存在就返回404
          return;
        }
        //1.文件存在， 源文件的备份路径名
        std::string pathname = BACKUP_DIR + filename;
        //  1）文件被压缩了，先解压缩
        if(data_manage.IsCompress(filename) == true){
          std::string gzfile;
          //获取压缩包名称
          data_manage.GetGzName(filename, &gzfile);
          //组织一个压缩包的路径名
          std::string gzpathname = GZFILE_DIR + gzfile;
          //将压缩包解压
          CompressUtil::UnCompress(gzpathname, pathname);
          //解压后，修改两者名字一样
          data_manage.Insert(filename, filename);
          //解压后删除压缩包
          unlink(gzpathname.c_str());
        }
        //2）直接将数据读取到rsp的body中
        FileUtil::Read(pathname, &rsp.body);
        rsp.set_header("Content-Type", "application/oct-stream");
        rsp.status = 200;
        return;
      }
    private:
      std::string _file_dir;  //文件上传备份的路径
      httplib::Server _server;
  };
}

