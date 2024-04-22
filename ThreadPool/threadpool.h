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
class Semaphore {
public:
	Semaphore(int res = 0):resLimit_(res){}
	~Semaphore() = default;

	void wait() {
		std::unique_lock<std::mutex> lock(mtx_);
		cond_.wait(lock, [&]() {return resLimit_ > 0;});
		resLimit_--;
	}
	void post() {
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}
private:
	size_t resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;

};

class Any {
public:
	Any() = default;
	~Any() = default;
	Any (const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;


	template<typename T>
	Any(T data):base_(std::make_unique<Derive<T>>(data)){}

	template<typename T>
	T cast() {
		Derive<T>* derive = dynamic_cast<Derive<T>*>(base_.get());
		if (derive == nullptr) {
			std::cout << "derive == nullptr" << std::endl;
			throw "type is unmatch!";
		}
		return derive->data_;
	}
private:
	class Base {
	public:
		virtual ~Base() = default;
	};

	template<typename T>
	class Derive:public Base {
	public:
		Derive(T data):data_(data){}
		T data_;
	};

private:
	std::unique_ptr<Base> base_;

};

class Result;

class Task
{
public:
	Task() :result_(nullptr){}
	~Task() = default;
	virtual Any run() = 0;
	void exec();
	void setResult(Result* result);
private:
	Result* result_;
};

class Result {
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	void setValue(Any any);
	Any get();
private:
	Any any_;
	Semaphore sem_;
	std::atomic_bool isValid_;
	std::shared_ptr<Task> task_;
};

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
	Result submitTask(std::shared_ptr<Task> sp);

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


	std::queue<std::shared_ptr<Task>> taskQue_;//shared_ptr为了能够接受右值，保持`用户传进来的即将消亡的Task`
	std::mutex taskQueMtx_;
	std::condition_variable notFull_;
	std::condition_variable notEmpty_;
	std::condition_variable noThread_;

	PoolMode mode_;
	std::atomic_bool isRunning_;
};

#endif