#ifndef __COROUTINE_H__
#define __COROUTINE_H__

// 使用context族函数需要包含的头文件
#include <ucontext.h>

// include memcpy function
#include <cstring>
// include assert function
#include <cassert>

#include <vector>
#include <map>
#include <functional>
#include <memory>

namespace u6th9d {

// 调度器类型
class Schedule :public std::enable_shared_from_this<Schedule> {
public:
    // 调度器的智能指针类型
    using SharedSchedule = std::shared_ptr<Schedule>;
    // 能够用于创建协程的callable类型
    using CoroutineFunction = std::function<void* (SharedSchedule)>;
    // 协程的状态
    enum CoroutineStatus {
        // 创建完成 等待执行
        Ready,
        // 正在执行
        Running,
        // 挂起 等待执行
        Suspend,
        // 无效的
        Invalid
    };

private:
    // 协程类型
    class Coroutine;
    // 调度器的智能指针类型
    using WeakSchedule = std::weak_ptr<Schedule>;
    // 协程的智能指针类型
    using SharedCoroutine = std::shared_ptr<Coroutine>;

public:
    // 调度器构造函数 调度器默认可调度8个协程 可动态扩容
    Schedule(int size = 8);
    // 调度器析构函数
    ~Schedule();
    // 调度器创建协程接口 接受特定类型的callable
    int createCoroutine(CoroutineFunction& function);
    // 调度器根据协程id调用协程
    void* runCoroutine(int id, void* msg);
    // 协程主动挂起自己
    void* yieldCoroutine(void* msg);
    // 获取id对应协程的运行状态
    int getCoroutineStatus(int id) const ;

private:
    // 调用协程对象的callable
    void runCoroutineFunction();
    // 从调度器移除已经执行完毕的协程
    void removeCurrentCoroutine();
    // 获取调度器当前正在执行的协程对象
    SharedCoroutine getCurrentCoroutine();
    // c++函数runCoroutineFunction到c函数void(*)()的adapter
    static void runHelper(void* ptr);
    // 共享栈的大小 默认值为2m 运行时不可更改
    static const int MAX_STACK_SIZE = 2 * 1024 * 1024;

private:
    // 调度器当前容纳的协程
    int capacity_;
    // 当前正在执行的协程id
    int current_id_;
    // 协程id的使用情况 id未使用时为false
    std::vector<bool> coroutine_status_;
    // 协程id到协程对象的映射
    std::map<int, SharedCoroutine> coroutines_;
    // 协程运行栈的指针
    char* stack_;
    // main函数的上下文
    ucontext_t context_;
    // 消息
    void* msg_;
};

} // namespace u6th9d

#endif // !__COROUTINE_H__
