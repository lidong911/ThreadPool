#ifndef  THREADPOOL_H
#define THREADPOOL_H
#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <thread>
#include <future>
const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60;  //单位是s


enum  class PoolMode
{
	Mode_FIXED,  //固定数量的线程
	Mode_CACHED,  //线程数量可动态增长
};

//线程类型
class Thread
{
public:
	//线程函数对象类型
	using ThreadFunc = std::function<void(int)>;

	//线程构造
	Thread(ThreadFunc func)
		:func_(func)
		, threadId_(generateId_++)
	{}
	// 线程析构
	~Thread() = default;
	//启动线程池
	void start()
	{
		//创建一个线程来执行一个线程函数
		std::thread t(func_, threadId_); //c++11来说 线程对象t 和线程函数func_
		t.detach();  //设置分离线程  pthread_detch pthread_t设置成分离线程
	}
	//获取线程id
	int getId() const
	{
		return threadId_;
	}
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_; //保存线程id
};

int Thread::generateId_ = 0;


//线程池类型
class ThreadPool
{
public:
	//线程池构造
	ThreadPool()
		: initThreadSize_(4)
		, taskSize_(0)
		, idleThreadSize_(0)
		, curthreadSize_(0)
		, taskQueMaxThresHold_(TASK_MAX_THRESHHOLD)
		, threadsizeThresHold_(200)
		, poolMode_(PoolMode::Mode_FIXED)
		, isPoolRunning_(false)

	{}
	//线程池析构
	~ThreadPool()
	{

		isPoolRunning_ = false;

		//等待线程池里面所有的线程返回 有两种状态：阻塞 & 正在执行任务中
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		noEmpty_.notify_all();
		exitCond_.wait(lock, [&]() -> bool { return threads_.size() == 0; });
	}


	void start(int InitThreadSize = std::thread::hardware_concurrency()) //开启线程池
	{
		//设置线程池的启动状态
		isPoolRunning_ = true;
		//记录初始线程个数
		initThreadSize_ = InitThreadSize;
		curthreadSize_ = InitThreadSize;
		//创建线程对象
		for (int i = 0; i < initThreadSize_; i++)
		{
			//创建thread线程对象的时候，把线程函数给到thread线程对象  unique智能指针不能复制只能转移move
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			//threads_.emplace_back(std::move(ptr));
		}

		//启动所有线程
		for (int i = 0; i < initThreadSize_; i++)
		{
			threads_[i]->start(); //需要去执行一个线程函数
			idleThreadSize_++;   //记录初始空闲的线程
		}
	}
	void setMode(PoolMode mode)  //设置线程池工作模式
	{
		if (checkRunningState())
			return;
		poolMode_ = mode;
	}

	// 设置任务队列上限的阈值
	void setTaskQueThreshHold(int threshhold)
	{
		if (checkRunningState())
			return;
		taskQueMaxThresHold_ = threshhold;
	}

	//设置线程池cached模式下线程阈值
	void setThreadSizeThreshHold(int threshhold)
	{
		if (checkRunningState())
			return;
		if (poolMode_ == PoolMode::Mode_CACHED)
		{
			threadsizeThresHold_ = threshhold;
		}
	}
	//给线程池提交任务
	//使用可变参模板编程，让submitTask可以接收任意任务函数和任意数量的参数
	//函数返回值需要future<>
	template<typename Func,typename...Args>
	//通过decltype推导出函数的返回值类型实例化future 然后就能auto自动推导出submittask返回值
	auto submitTask(Func&& func, Args&&...args) -> std::future<decltype(func(args...))>
	{
		//打包任务，放入任务队列里面
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();
		//获取锁  
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		//用户提交任务最长不能阻塞超过1s否则判断提交任务失败，返回
		if (!noFull_.wait_for(lock, std::chrono::seconds(1),
			[&]() ->bool {return   taskQue_.size() < (size_t)taskQueMaxThresHold_; }))
		{
			//表示nofull_等待1s，条件依然没有满足
			std::cerr << "task queue is full, submit task full." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]() ->RType {return RType(); });
			(*task)();
			return task ->get_future();
		}
		//如果有空余把任务放入任务队列中
		//taskQue_.emplace(sp);
		//using Task = std::function<void()>;
		taskQue_.emplace([task]() {(*task)();});
		taskSize_++;
		//在notEmpty_上进行 通知,赶快分配线程执行任务
		noEmpty_.notify_all();

		//cached模式  任务处理比较紧急，场景：小而快的任务 需要根据任务数量和空闲线程的数量， 判断是否需要创建新的线程
		if (poolMode_ == PoolMode::Mode_CACHED
			&& taskSize_ > idleThreadSize_
			&& curthreadSize_ < threadsizeThresHold_)
		{
			std::cout << ">>> create new thread..." << std::this_thread::get_id() << "exit" << std::endl;
			//创建新线程对象
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			threads_[threadId]->start();
			//修改线程个数相关的变量
			idleThreadSize_++;
			curthreadSize_++;
		}

		//返回任务的Result对象
		return result;
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator = (const ThreadPool&) = delete;

private:
	void threadFunc(int threadid)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();

		//所有任务必须执行完成，线程池才可以回收所有线程资源
		for (;;)
		{
			Task task;
			{
				//同样先获取锁
				std::unique_lock<std::mutex> lock(taskQueMtx_);
				std::cout << "tid: " << std::this_thread::get_id()
					<< "尝试获取任务..." << std::endl;

				//cached模式下，有可能已经创建了很多的线程，但是空闲时间超过60s，应该把多余的线程回收掉
				// (超过initThreadSize_数量的线程要回收)
				//当前时间   - 上一次线程执行的时间  > 60s

					//每一秒中返回一次 怎么区分：超时返回？还是有任务待执行返回
				//锁加双重判断
				while (taskQue_.size() == 0)
				{

					//线程池要结束，回收线程资源
					if (!isPoolRunning_)
					{
						threads_.erase(threadid); //不要 std::this_thread::getid()
						std::cout << "threadid:" << std::this_thread::get_id() << "exit!" << std::endl;
						exitCond_.notify_all();
						return;  //线程函数结束，线程结束
					}

					if (poolMode_ == PoolMode::Mode_CACHED)
					{
						//条件变量超时返回
						if (std::cv_status::timeout ==
							noEmpty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() >= THREAD_MAX_IDLE_TIME
								&& curthreadSize_ > initThreadSize_)
							{
								//开始回收线程
								//记录线程数量的相关变量的值修改
								//把线程对象从线程列表容器中删除  没有办法 threadFunc 《=》thread 对象
								//threadid =》thread对象
								threads_.erase(threadid); //不要 std::this_thread::getid()
								curthreadSize_--;
								idleThreadSize_--;

								std::cout << "threadid:" << std::this_thread::get_id() << "exit!" << std::endl;
								return;


							}
						}
					}
					else
					{
						//等待noEmpty条件
						noEmpty_.wait(lock, [&]()->bool {return taskQue_.size() > 0 || !isPoolRunning_; });

					}
					//线程池要结束，回收线程资源
				
				}
				idleThreadSize_--;
				std::cout << "tid: " << std::this_thread::get_id()
					<< "获取任务成功...." << std::endl;
				//不空，从任务队列中去一个任务出来，
				task = taskQue_.front();
				taskQue_.pop(); //移除
				taskSize_--;

				//如果依然有剩余任务，继续通知其他线程执行任务
				if (taskQue_.size() > 0)
				{
					noEmpty_.notify_all();//通知其他线程继续消费
				}
				//执行完一个任务，得进行通知  通知可以继续提交生产任务
				noFull_.notify_all();
			}	//就应该把锁释放掉
			//当前线程负责执行这个任务
			if (task != nullptr)
			{
				task(); //执行function<void()>
			}
			idleThreadSize_++;
			lastTime = std::chrono::high_resolution_clock().now();  //更新线程执行完任务的时间

		}
	}
		//检查pool的运行状态
	bool checkRunningState() const
		{
			return isPoolRunning_;
		}

private:
	std::unordered_map<int, std::unique_ptr <Thread>> threads_;
	size_t initThreadSize_;  //初始线程数量
	//记录当前线程池里面的线程总数量
	std::atomic_int curthreadSize_;
	//记录空闲线程的数量
	std::atomic_int idleThreadSize_;
	//线程数量上限阈值
	int threadsizeThresHold_;

	//Task任务对应的就是函数对象
	using Task = std::function<void()>;
	std::queue<Task> taskQue_; //任务队列
	std::atomic_int taskSize_; //任务数量
	int taskQueMaxThresHold_; // 任务队列数量上限的阈值
	//线程通信
	std::mutex taskQueMtx_; //保证任务队列的线程安全
	std::condition_variable noFull_; //表示任务队列不满
	std::condition_variable noEmpty_;  //表示任务队列不空

	std::condition_variable exitCond_; //等待线程资源全部回收
	PoolMode poolMode_;  //当前线程池工作模式

	//表示当前线程池的启动状态
	std::atomic_bool isPoolRunning_;


};


#endif


