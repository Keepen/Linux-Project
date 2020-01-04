//封装一个线程池类
#pragma once
#include <iostream>
#include <sstream>
#include <thread>
#include <queue>
#include <vector>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#define MAX_THREAD 5
#define MAX_QUEUE 10

typedef void(*handler_t)(int);


class ThreadTask{
  private:
    int _data;
    handler_t _handler;
  public:
    //1.构造函数：初始化各成员
    ThreadTask(int data, handler_t handle):
      _data(data), _handler(handle){}

    //2.设置任务
    void SetTask(int data, handler_t handle){
      _data = data;
      _handler = handle;
      return;
    }

    //3.运行任务
    void TaskRun(){
      return _handler(_data);
    }
};




class ThreadPool{
  private:
    std::queue<ThreadTask> _queue;
    int _capacity;
    pthread_mutex_t _mutex;
    pthread_cond_t _cond_pro;
    pthread_cond_t _cond_con;
    int _thr_max;       //用于控制线程的最大数量
    /*public:
    ThreadPool();
    ThreadInit();         //创建指定数量的线程
    TaskPush(MyTask& t);  //线程安全的入队
    TaskPop(MyTask& t);   //线程安全的任务出队
    thr_start();
      //1.线程安全的任务出队
      //  //若没有任务， 且退出标志为真，则退出线程
      //2.处理任务
    */

  private:
    void thr_start(){
      while(1){
        pthread_mutex_lock(&_mutex);
        while(_queue.empty()){
          pthread_cond_wait(&_cond_con, &_mutex);
        }
        ThreadTask tt = _queue.front();
        _queue.pop();
        pthread_mutex_unlock(&_mutex);
        pthread_cond_signal(&_cond_pro);
        tt.TaskRun();
      }
      return;
    }


  public:
    ThreadPool(int maxq = MAX_QUEUE, int maxt = MAX_THREAD)
    :_capacity(maxq), _thr_max(maxt){
      pthread_mutex_init(&_mutex, NULL);
      pthread_cond_init(&_cond_con, NULL);
      pthread_cond_init(&_cond_pro, NULL);
    }
    ~ThreadPool(){
      pthread_mutex_destroy(&_mutex);
      pthread_cond_destroy(&_cond_con);
      pthread_cond_destroy(&_cond_pro);
    }

    //1.初始化线程池
    bool PoolInit(){
      for(int i = 0;i < _thr_max;++i){
        std::thread thr(&ThreadPool::thr_start, this);
        thr.detach();
      }
      return true;
    }

    //2.线程安全的任务入队
    bool TaskPush(ThreadTask& tt){
      pthread_mutex_lock(&_mutex);
      while(_queue.size() == _capacity){
        pthread_cond_wait(&_cond_pro, &_mutex);
      }
      _queue.push(tt);
      pthread_mutex_unlock(&_mutex);
      pthread_cond_signal(&_cond_con);
      return true;
    }   
};
