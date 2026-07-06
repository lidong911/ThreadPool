#include <iostream>
#include<functional>
#include <thread>
#include <future>
using namespace std;

#include "threadpool.h"

/*
如何让线程池提交任务更方便
1.  pool.submitTask(sum1,1,10)
     pool.submitTask(sum2,1,10,30)
     submitTask:可变参模板编程

2. 我们自己造了一个Result以及相关类型，代码挺多
    c++线程库  thread   packaged_task(Function函数对象)   async
    使用future来代替result节省线程池代码

*/
int sum1(int a, int b)
{
    //比较耗时
    return a + b;
}
//io线程
void io_thread(int listenfd)
{

}

//worker线程
void worker_thread(int clientfd)
{

}
int main()
{
    ThreadPool pool;
    pool.start(4);
    future<int> r1 = pool.submitTask(sum1, 10, 20);
    cout << r1.get() << endl;
  /*  packaged_task<int(int, int) > task(sum1);
    future<int> res = task.get_future();
    task(10, 20);

    thread t(std::move(task), 10, 20);
    t.detach();
    cout << res.get() << endl;*/
}

