#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include <unordered_map>
#include <boost/filesystem.hpp>		//Ŀ¼ͷ�ļ�
#include <boost/algorithm/string.hpp> //splitͷ�ļ�
#include "httplib.h"


class FileUtil {
public:
	//���ļ��ж�ȡ��������
	static bool Read(const std::string& name, std::string* body) {
		//ָ�������ƶ�ȡ��ʽ,�������ı���ȡʱ�ᷢ������ֽ�ֻ��һ���ַ������,���¶�ȡ���ַ���ʵ�ʵ�ҪС
		std::ifstream fs(name, std::ios::binary);
		if (fs.is_open() == false) {
			std::cout << "open file " << name << " failed\n";
			return false;
		}
		//��ȡ�ļ���С boost::filesystem::file_size();
		int64_t fsize = boost::filesystem::file_size(name);
		body->resize(fsize);  //��body����ռ�����ļ�����
		fs.read(&(*body)[0], fsize);
		if (fs.good() == false) {
			std::cout << "file " << name << " read data failed\n";
			return false;
		}
		fs.close();
		return true;
	}

	//���ļ���д������
	static bool Write(const std::string& name, const std::string& body) {
		//����� -- ofstreamĬ�ϴ��ļ���ʱ�����ԭ������
		//��ǰ�����Ǹ���д��
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
	//1.����/��������
	bool Insert(const std::string& key, const std::string& val) {
		_backup_list[key] = val;
		//ÿ�β����д���ļ�
		Storage();
		return true;
	}
	//2.��ȡ�ļ�etag
	bool GetEtag(const std::string& key, std::string* val) {
		auto it = _backup_list.find(key);
		if (it == _backup_list.end()) {
			return false;
		}
		*val = it->second;
		return true;
	}
	//3.�־û��洢
	bool Storage() {
		//��_backup_list�е����ݽ��г־û��洢
				//���ݶ�����г־û��洢--���л�
				//������ĸ�ʽ�洢�ļ�����ѹ������
				//filename etag\r\n
		std::stringstream tmp;  //ʵ����һ��string������
		auto it = _backup_list.begin();
		for (; it != _backup_list.end(); ++it) {
			tmp << it->first << " " << it->second << "\r\n";
		}
		//����������Ϣд���ļ���
		//�����漰�̰߳�ȫ�������������
		FileUtil::Write(_store_file, tmp.str());
		return true;
	}
	//4.��ʼ������ԭ������
	bool InitLoad() {
		//�����ݳ־û��洢�ļ��м�������
				//1������������ļ������ݶ�ȡ����
		std::string body;
		if (FileUtil::Read(_store_file, &body) == false) {
			return false;
		}

		//2�������ַ�����������\r\n���зָ�
		//boost::split(vector, src, sep, flag);
		std::vector<std::string> list;
		boost::split(list, body, boost::is_any_of("\r\n"), boost::token_compress_off);

		//3��ÿһ�а��տո���зָ�-ǰ����key�������val
		for (auto i : list) {
			size_t pos = i.find(" ");
			if (pos == std::string::npos) {
				continue;
			}
			std::string key = i.substr(0, pos);
			std::string val = i.substr(pos + 1);
			//4����key/val��ӵ�_file_list��
			Insert(key, val);
		}
	}
private:
	std::string _store_file;	//�־û��洢�ļ���
	//���ݵ��ļ���Ϣ
	std::unordered_map<std::string, std::string> _backup_list;
};



class CloudClient {
public:
	CloudClient(const std::string& filename, const std::string& store_file,
		const std::string& srv_ip, uint16_t srv_port)
		:_listen_dir(filename), data_manage(store_file),
		_srv_ip(srv_ip), _srv_port(srv_port){}

	//1.���������ļ���������
	bool Start() {
		//������ǰ���ݵ���Ϣ
		data_manage.InitLoad();
		while (1) {
			std::vector<std::string> list;
			//��ȡ��������Ҫ���ݵ��ļ���
			GetBackUpFileList(&list);
			for (size_t i = 0; i < list.size(); ++i) {
				std::string name = list[i];
				//�ļ�·������ 
				std::string pathname = _listen_dir + name;
				std::cout << pathname << " is need to backup\n";
				//��ȡ�ļ�������Ϊ��������
				std::string body;
				FileUtil::Read(pathname, &body);
				//ʵ����Client����׼������HTTP�ϴ�����
				httplib::Client client(_srv_ip, _srv_port);
				std::string req_path = "/" + name;
				auto rsp = client.Put(req_path.c_str(), body,
					"application/octet-stream");
				if (rsp == NULL || (rsp != NULL && rsp->status != 200)) {
					//����ļ��ϴ�ʧ����
					std::cout << pathname << " backup failed\n";
					continue;
				}
				std::string etag;
				GetEtag(pathname, &etag);
				//���ݳɹ�������/������Ϣ
				data_manage.Insert(name, etag);
				std::cout << pathname << " backup successfully\n";
			}
			//����1s�Ӻ����¼��
			sleep(1);
		}
		return true;

	}

	//2.��ȡ��Ҫ���ݵ��ļ��б�
	bool GetBackUpFileList(std::vector<std::string>* list) {
		//��Ŀ¼�����ڣ��򴴽�
		if (boost::filesystem::exists(_listen_dir) == false) {
			boost::filesystem::create_directory(_listen_dir);
		}
		//1������Ŀ¼��أ���ȡָ��Ŀ¼�������ļ�����
		boost::filesystem::directory_iterator begin(_listen_dir);
		boost::filesystem::directory_iterator end;
		for (; begin != end; ++begin) {
			//Ŀ¼�����б��ݣ���ǰֻ���е��㼶Ŀ¼�ı���
			if (boost::filesystem::is_directory(begin->status())) {
				continue;
			}
			//��ȡ�ļ�·����
			std::string pathname = begin->path().string();
			std::string name = begin->path().filename().string();
			std::string cur_etag, old_etag;
			//2������ļ�����etag
			GetEtag(pathname, &cur_etag);
			data_manage.GetEtag(name, &old_etag);
			//3����data_manage�б����etag�������бȶ�
			//	a.û���ҵ� -- ���ļ���Ҫ����
			//	b.�ҵ��������� -- ��Ҫ����
			//	c.�ҵ�������� -- ����Ҫ����
				//��ǰetag��ԭ��etag��ͬ���ͽ��б���
			if (cur_etag != old_etag) {
				list->push_back(name);
			}
		}
		return true;
	}

	//3.�����ļ�etag��Ϣ
	bool GetEtag(const std::string& pathname, std::string* etag) {
		//etag���ļ���С-�ļ����һ���޸�ʱ��
		int64_t fsize = boost::filesystem::file_size(pathname);
		time_t mtime = boost::filesystem::last_write_time(pathname);
		*etag = std::to_string(fsize) + "-" + std::to_string(mtime);

		return true;
	}
private:
	DataManager data_manage;
	//���Ŀ¼
	std::string _listen_dir;
	std::string _srv_ip;
	uint16_t _srv_port;
};

