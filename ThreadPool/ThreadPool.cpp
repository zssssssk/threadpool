// ThreadPool.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "threadpool.h"
#include<functional>
#include <iostream>
#include<thread>
#include<mutex>
#include<chrono>
#include<memory>
const size_t taskQueMaxThreshHold = INT32_MAX;
const size_t threadSizeMaxThreshHold = 10;
const size_t threadIdleMaxTime = 6;//secs
ThreadPool::ThreadPool() :
	initThreadSize_(0),
	curThreadSize_(0),
	taskQueSize_(0),
	idleThreadSize_(0),
	taskQueMaxThreshHold_(taskQueMaxThreshHold),
	threadSizeMaxThreshHold_(threadSizeMaxThreshHold),
	mode_(PoolMode::FIXED),
	isRunning_(false)
{

}

ThreadPool::~ThreadPool()
{
	isRunning_ = false;

	//等待线程池里的所有线程返回 有两种状态：阻塞 | 正在执行
	//还有一种，在获取锁之前
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	noThread_.wait(lock, [&]() {return curThreadSize_ == 0;});
}


void ThreadPool::start(size_t initThreadSize)
{
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;
	isRunning_ = true;
	//std::unordered_map threads_;
	for (size_t i = 0; i < initThreadSize_; i++)
	{
		auto t = std::make_unique<Thread>(std::bind(&ThreadPool::threadHandler, this,std::placeholders::_1));
		int threadId = t->getId();
		threads_.emplace(threadId, std::move(t));
		//threads_.emplace_back(std::move(t));
	}
	for (size_t i = 0; i < initThreadSize_; i++)
	{

		threads_[i]->start();
		idleThreadSize_++;
	}
}

void ThreadPool::setMode(PoolMode mode)
{
	if (!checkRunningState()) {
		mode_ = mode;
	}
}

void ThreadPool::setTaskQueMaxThreshHold(size_t max)
{
	if (!checkRunningState()) {
		taskQueMaxThreshHold_ = max;
	}
}

Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]() {return taskQueSize_ < taskQueMaxThreshHold_;})) 
	{
		std::cerr << "submit task failed!" << std::endl;
		return Result(sp,false);
	}

	taskQue_.emplace(sp);
	taskQueSize_++;

	notEmpty_.notify_all();

	//cached模式下,任务处理比较紧急的情况 场景：小而快的任务
	//需要根据任务数量和空闲线程判断是否需要增加线程
	if (mode_ == PoolMode::CACHEED && taskQueSize_ > idleThreadSize_ && curThreadSize_ < threadSizeMaxThreshHold_)
	{
		std::cout << "create new thread:" << std::endl;
		auto t = std::make_unique<Thread>(std::bind(&ThreadPool::threadHandler, this,std::placeholders::_1));
		int threadId = t->getId();
		threads_.emplace(threadId,std::move(t));
		threads_[threadId]->start();
		curThreadSize_++;
		idleThreadSize_++;
	}

	return Result(sp);
}

void ThreadPool::threadHandler(int threadId)
{
	auto lastTime = std::chrono::high_resolution_clock::now();

	while(1){
		std::shared_ptr<Task> task;
		{
			//cached模式下，有可能创建了许多线程
			//要把超时的线程回收掉，直至数量为initThreadSize_

			std::unique_lock<std::mutex> lock(taskQueMtx_);
			while (taskQueSize_ == 0) {
				if (!isRunning_) {
					curThreadSize_--;
					threads_.erase(threadId);
					noThread_.notify_all();
					return;
				}
				if (mode_ == PoolMode::CACHEED) {

					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						auto now = std::chrono::high_resolution_clock::now();
						auto dur = now - lastTime;
						std::cout << "thread:" << std::this_thread::get_id() << " curThreadSize_:" <<
							curThreadSize_<<" initThreadSize_:"<< initThreadSize_<<std::endl;
						if (dur.count() > threadIdleMaxTime && curThreadSize_ > initThreadSize_) {
							threads_.erase(threadId);
							curThreadSize_--;
							idleThreadSize_--;
							std::cout << "thread " << std::this_thread::get_id() << " exit!" << std::endl;
							return;
						}
					}
					
				}
				else {
					notEmpty_.wait(lock);
				}
			}

			task = taskQue_.front();
			taskQue_.pop();
			taskQueSize_--;

			idleThreadSize_--;

			//通知自己人吃螃蟹
			if (taskQueSize_ > 0) {
				notEmpty_.notify_all();
			}

			notFull_.notify_all();
		}
		if (task!=nullptr)
		{
			std::cout << "i am doing task in thread:" << std::this_thread::get_id() << std::endl;
			task->exec();

		}
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock::now();
	}


}

bool ThreadPool::checkRunningState() const
{
	return isRunning_;
}



/////////// Thread Class
int Thread::generateId_ = 0;
Thread::Thread(threadHander hander)
	: threadHander_(hander)
	, threadId_(generateId_++)
{

}

Thread::~Thread()
{
}

void Thread::start()
{
	std::thread t(threadHander_,threadId_);
	t.detach();
}
int Thread::getId() const
{
	return threadId_;
}
//////////// Result Class
Result::Result(std::shared_ptr<Task> task, bool isValid)
	:task_(task)
	,isValid_(isValid)
{
	task_->setResult(this);
}

void Result::setValue(Any any)
{
	any_ = std::move(any);
	sem_.post();
}

Any Result::get()
{
	if (!isValid_) return "";
	sem_.wait();
	return std::move(any_);
}



//////////// Task Class

void Task::exec()
{
	if (result_) 
	{
		result_->setValue(run());
	}
}

void Task::setResult(Result* result)
{
	result_ = result;
}
