#ifndef THREADPOOL_H
#define THREADPOOL_H
#include<queue>
#include<vector>
#include<memory>
#include<mutex>
#include<condition_variable>
#include<atomic>
#include<functional>
#include<unordered_map>
#include<iostream>
#include<future>

class Thread
{
public:
	using threadHander = std::function<void(int)>;
	Thread(threadHander hander);
	~Thread();
	void start();
	int getId() const;
private:
	threadHander threadHander_;
	static int generateId_;
	int threadId_;
};
enum class PoolMode
{
	FIXED,
	CACHEED,
};


class ThreadPool {
public:
	ThreadPool();
	~ThreadPool();
	void start(size_t initThreadSize = std::thread::hardware_concurrency());
	void setMode(PoolMode mode);
	void setTaskQueMaxThreshHold(size_t max);

	template<typename Func,typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		using RType = decltype(func(args...));
		//task不能创建成局部的，不然出了作用域就没了
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
		);
		std::future<RType> result = task->get_future();

		std::unique_lock<std::mutex> lock(taskQueMtx_);

		if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]() {return taskQueSize_ < taskQueMaxThreshHold_;}))
		{
			std::cerr << "submit task failed!" << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType {return RType();}
			);
			(*task)();
			return task->get_future();
		}
		//都共享指针了，直接值捕获
		//若用引用捕获，task出作用域直接析构了
		taskQue_.emplace(
			[task]() {(*task)();}
		);
		++taskQueSize_;

		notEmpty_.notify_all();

		//cached模式下,任务处理比较紧急的情况 场景：小而快的任务
		//需要根据任务数量和空闲线程判断是否需要增加线程
		if (mode_ == PoolMode::CACHEED && taskQueSize_ > idleThreadSize_ && curThreadSize_ < threadSizeMaxThreshHold_)
		{
			std::cout << "create new thread:" << std::endl;
			auto t = std::make_unique<Thread>(std::bind(&ThreadPool::threadHandler, this, std::placeholders::_1));
			int threadId = t->getId();
			threads_.emplace(threadId, std::move(t));
			threads_[threadId]->start();
			curThreadSize_++;
			idleThreadSize_++;
		}

		return result;
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
private:
	void threadHandler(int threadId);
	bool checkRunningState() const;
private:
	//std::vector<std::unique_ptr<Thread>> threads_;
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;
	size_t initThreadSize_;
	std::atomic_uint idleThreadSize_;
	std::atomic_uint curThreadSize_;
	size_t threadSizeMaxThreshHold_;
	size_t taskQueMaxThreshHold_;//定值
	std::atomic_uint taskQueSize_;//在多线程中会变

	using Task = std::function<void()>;
	std::queue<Task> taskQue_;//shared_ptr为了能够接受右值，保持`用户传进来的即将消亡的Task`
	std::mutex taskQueMtx_;
	std::condition_variable notFull_;
	std::condition_variable notEmpty_;
	std::condition_variable noThread_;

	PoolMode mode_;
	std::atomic_bool isRunning_;
};

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
		auto t = std::make_unique<Thread>(std::bind(&ThreadPool::threadHandler, this, std::placeholders::_1));
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

void ThreadPool::threadHandler(int threadId)
{
	auto lastTime = std::chrono::high_resolution_clock::now();

	while(1){
		Task task;
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
			task();

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
	std::thread t(threadHander_, threadId_);
	t.detach();
}
int Thread::getId() const
{
	return threadId_;
}

#endif