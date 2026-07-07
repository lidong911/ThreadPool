# ThreadPool 项目

一个基于 C++11 实现的高效线程池，支持固定模式（FIXED）和缓存模式（CACHED），提供灵活的任务提交接口和异步获取结果的能力。

---

## 📖 简介

本项目实现了一个轻量级、易用的线程池，主要用于多任务并发处理。它通过**可变参模板**和**类型擦除**技术，允许用户提交任意可调用对象（函数、Lambda、函数对象）并携带任意参数，同时通过 `std::future` 获取任务执行结果。线程池支持两种工作模式：

- **FIXED 模式**：线程数量固定，任务队列有界，适用于任务量可控、线程开销稳定的场景。
- **CACHED 模式**：线程数量可动态增长（上限可配置），空闲线程超过一定时间（60秒）后自动回收，适用于任务突发且执行快速的场景。

---

## ✨ 特性

- **灵活的任务提交**：使用可变参模板 + 完美转发，支持任意函数和任意数量参数。
- **异步结果获取**：通过 `std::packaged_task` 和 `std::future` 获取任务返回值。
- **两种工作模式**：FIXED / CACHED，满足不同业务需求。
- **动态线程管理**（CACHED 模式）：根据任务负载自动创建线程，空闲超时自动回收。
- **有界任务队列**：可配置队列最大容量，提交任务时支持超时等待（1秒），避免无限阻塞。
- **线程安全**：使用互斥锁和条件变量保证任务队列的并发安全。
- **RAII 资源管理**：析构时自动等待所有任务完成并回收线程资源。

---

## 🛠️ 编译与运行

### 环境要求

- C++11 或更高版本（支持 `std::thread`、`std::future`、`std::atomic` 等）
- 支持 CMake 或直接使用 g++/clang++

### 编译

```bash
# 使用 g++
g++ -std=c++11 -pthread ThreadPool_final.cpp -o threadpool_demo

# 运行示例
./threadpool_demo
```

---

## 📝 使用示例

```cpp
#include "threadpool.h"
#include <iostream>
#include <future>

int sum1(int a, int b) {
    return a + b;
}

int main() {
    ThreadPool pool;
    // 启动线程池，默认线程数为 CPU 核心数
    pool.start(4);
    
    // 提交任务，获取 future
    std::future<int> r1 = pool.submitTask(sum1, 10, 20);
    
    // 阻塞等待结果
    std::cout << r1.get() << std::endl;  // 输出 30

    // 提交 Lambda 任务
    auto r2 = pool.submitTask([](int x) { return x * x; }, 5);
    std::cout << r2.get() << std::endl;  // 输出 25

    return 0;
}
```

### 更多用法

- **设置工作模式**（默认 FIXED）：

  ```cpp
  pool.setMode(PoolMode::Mode_CACHED);
  ```

- **设置任务队列容量**：

  ```cpp
  pool.setTaskQueThreshHold(100);  // 最多容纳 100 个任务
  ```

- **设置 CACHED 模式下的最大线程数**：

  ```cpp
  pool.setThreadSizeThreshHold(50);
  ```

---

## 🧠 核心设计

### 1. 可变参模板 + 完美转发

`submitTask` 使用模板参数包接收任意可调用对象及其参数，通过 `std::forward` 保持参数的原始类型（左值/右值），利用 `decltype` 自动推导返回类型，最终返回 `std::future<RType>`。

### 2. 类型擦除与统一存储

任务队列元素类型为 `std::function<void()>`，所有任务（无论返回类型和参数）最终通过 Lambda 包装成 `void()` 形式，实现统一存储和调度。

### 3. 线程函数（`threadFunc`）

- 每个线程循环从任务队列取任务执行。
- 在 FIXED 模式下，若无任务则阻塞等待 `notEmpty_` 条件变量。
- 在 CACHED 模式下，等待任务时采用超时等待（1秒），若超时且空闲超过 60 秒且当前线程数大于初始线程数，则自动回收当前线程。
- 析构时，设置 `isPoolRunning_ = false`，唤醒所有线程，等待线程安全退出。

### 4. 任务提交流程

1. 使用 `std::bind` 绑定参数，生成无参可调用对象。
2. 用 `std::packaged_task<RType()>` 包装，获取 `std::future`。
3. 检查任务队列是否已满（支持超时等待 1 秒）。
4. 将任务（Lambda）放入队列，增加 `taskSize_`。
5. 唤醒一个等待线程。
6. CACHED 模式下，若任务数大于空闲线程数且未达线程上限，则创建新线程。

---

## 🔧 配置参数

| 常量                    | 默认值      | 说明                                                         |
| ----------------------- | ----------- | ------------------------------------------------------------ |
| `TASK_MAX_THRESHHOLD`   | `INT32_MAX` | 任务队列最大容量（可修改）                                   |
| `THREAD_MAX_THRESHHOLD` | `100`       | CACHED 模式线程上限（可通过 `setThreadSizeThreshHold` 修改） |
| `THREAD_MAX_IDLE_TIME`  | `60` 秒     | CACHED 模式下空闲线程回收时间                                |



---

## 📂 文件说明

- `threadpool.h`：线程池核心头文件，包含 `Thread` 和 `ThreadPool` 类定义及实现。
- `ThreadPool_final.cpp`：示例程序，演示基本使用。

---

## 🤝 贡献与反馈

欢迎提交 Issue 和 Pull Request，一起完善这个线程池实现。

---

## 📄 许可

本项目仅供学习交流使用。
