#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include <unordered_map>
#include <boost/filesystem.hpp>		//目录头文件
#include <boost/algorithm/string.hpp> //split头文件
#include "httplib.h"


class FileUtil {
public:
	//从文件中读取所有内容
	static bool Read(const std::string& name, std::string* body) {
		//指定二进制读取方式,否则按照文本读取时会发生多个字节只有一个字符的情况,导致读取的字符比实际的要小
		std::ifstream fs(name, std::ios::binary);
		if (fs.is_open() == false) {
			std::cout << "open file " << name << " failed\n";
			return false;
		}
		//获取文件大小 boost::filesystem::file_size();
		int64_t fsize = boost::filesystem::file_size(name);
		body->resize(fsize);  //给body申请空间接收文件数据
		fs.read(&(*body)[0], fsize);
		if (fs.good() == false) {
			std::cout << "file " << name << " read data failed\n";
			return false;
		}
		fs.close();
		return true;
	}

	//向文件中写入数据
	static bool Write(const std::string& name, const std::string& body) {
		//输出流 -- ofstream默认打开文件的时候清空原有内容
		//当前策略是覆盖写入
		std::ofstream ofs(name, std::ios::binary);
		if (ofs.is_open() == false) {
			std::cout << "open file " << name << " failed\n";
			return false;
		}
		ofs.write(&body[0], body.size());
		if (ofs.good() == false) {
			std::cout << "file " << name << " write data failed\n";
			return false;
		}
		ofs.close();
		return true;
	}
};


class DataManager {
public:
	DataManager(const std::string& filename):_store_file(filename) {}
	//1.插入/更新数据
	bool Insert(const std::string& key, const std::string& val) {
		_backup_list[key] = val;
		//每次插入后都写入文件
		Storage();
		return true;
	}
	//2.获取文件etag
	bool GetEtag(const std::string& key, std::string* val) {
		auto it = _backup_list.find(key);
		if (it == _backup_list.end()) {
			return false;
		}
		*val = it->second;
		return true;
	}
	//3.持久化存储
	bool Storage() {
		//将_backup_list中的数据进行持久化存储
				//数据对象进行持久化存储--序列化
				//按下面的格式存储文件名与压缩包名
				//filename etag\r\n
		std::stringstream tmp;  //实例化一个string流对象
		auto it = _backup_list.begin();
		for (; it != _backup_list.end(); ++it) {
			tmp << it->first << " " << it->second << "\r\n";
		}
		//将读到的信息写入文件中
		//不用涉及线程安全，所以无需加锁
		FileUtil::Write(_store_file, tmp.str());
		return true;
	}
	//4.初始化加载原有数据
	bool InitLoad() {
		//从数据持久化存储文件中加载数据
				//1）将这个备份文件的数据读取出来
		std::string body;
		if (FileUtil::Read(_store_file, &body) == false) {
			return false;
		}

		//2）进行字符串处理，按照\r\n进行分割
		//boost::split(vector, src, sep, flag);
		std::vector<std::string> list;
		boost::split(list, body, boost::is_any_of("\r\n"), boost::token_compress_off);

		//3）每一行按照空格进行分割-前边是key，后边是val
		for (auto i : list) {
			size_t pos = i.find(" ");
			if (pos == std::string::npos) {
				continue;
			}
			std::string key = i.substr(0, pos);
			std::string val = i.substr(pos + 1);
			//4）将key/val添加到_file_list中
			Insert(key, val);
		}
	}
private:
	std::string _store_file;	//持久化存储文件名
	//备份的文件信息
	std::unordered_map<std::string, std::string> _backup_list;
};



class CloudClient {
public:
	CloudClient(const std::string& filename, const std::string& store_file,
		const std::string& srv_ip, uint16_t srv_port)
		:_listen_dir(filename), data_manage(store_file),
		_srv_ip(srv_ip), _srv_port(srv_port){}

	//1.完成整体的文件备份流程
	bool Start() {
		//加载以前备份的信息
		data_manage.InitLoad();
		while (1) {
			std::vector<std::string> list;
			//获取到所有需要备份的文件名
			GetBackUpFileList(&list);
			for (size_t i = 0; i < list.size(); ++i) {
				std::string name = list[i];
				//文件路劲和名 
				std::string pathname = _listen_dir + name;
				std::cout << pathname << " is need to backup\n";
				//读取文件数据作为请求正文
				std::string body;
				FileUtil::Read(pathname, &body);
				//实例化Client对象准备发起HTTP上传请求
				httplib::Client client(_srv_ip, _srv_port);
				std::string req_path = "/" + name;
				auto rsp = client.Put(req_path.c_str(), body,
					"application/octet-stream");
				if (rsp == NULL || (rsp != NULL && rsp->status != 200)) {
					//这个文件上传失败了
					std::cout << pathname << " backup failed\n";
					continue;
				}
				std::string etag;
				GetEtag(pathname, &etag);
				//备份成功，插入/更新信息
				data_manage.Insert(name, etag);
				std::cout << pathname << " backup successfully\n";
			}
			//休眠1s钟后重新检测
			sleep(1);
		}
		return true;

	}

	//2.获取需要备份的文件列表
	bool GetBackUpFileList(std::vector<std::string>* list) {
		//若目录不存在，则创建
		if (boost::filesystem::exists(_listen_dir) == false) {
			boost::filesystem::create_directory(_listen_dir);
		}
		//1）进行目录监控，获取指定目录下所有文件名称
		boost::filesystem::directory_iterator begin(_listen_dir);
		boost::filesystem::directory_iterator end;
		for (; begin != end; ++begin) {
			//目录不进行备份，当前只进行单层级目录的备份
			if (boost::filesystem::is_directory(begin->status())) {
				continue;
			}
			//获取文件路径名
			std::string pathname = begin->path().string();
			std::string name = begin->path().filename().string();
			std::string cur_etag, old_etag;
			//2）逐个文件计算etag
			GetEtag(pathname, &cur_etag);
			data_manage.GetEtag(name, &old_etag);
			//3）与data_manage中保存的etag继续进行比对
			//	a.没有找到 -- 新文件需要备份
			//	b.找到，但不等 -- 需要备份
			//	c.找到，且相等 -- 不需要备份
				//当前etag，原有etag不同，就进行备份
			if (cur_etag != old_etag) {
				list->push_back(name);
			}
		}
		return true;
	}

	//3.计算文件etag信息
	bool GetEtag(const std::string& pathname, std::string* etag) {
		//etag：文件大小-文件最后一次修改时间
		int64_t fsize = boost::filesystem::file_size(pathname);
		time_t mtime = boost::filesystem::last_write_time(pathname);
		*etag = std::to_string(fsize) + "-" + std::to_string(mtime);

		return true;
	}
private:
	DataManager data_manage;
	//监控目录
	std::string _listen_dir;
	std::string _srv_ip;
	uint16_t _srv_port;
};

