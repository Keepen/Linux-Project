all:main upload
.PHONY:main upload
main:main.cpp
	g++ -g -std=c++11 $^ -o $@ -lpthread -lboost_system -lboost_filesystem
upload:upload.cpp
	g++ -g -std=c++11 $^ -o $@ -lboost_system -lboost_filesystem
	
