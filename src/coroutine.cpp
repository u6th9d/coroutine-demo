#include "coroutine.h"

namespace u6th9d {

class Schedule::Coroutine {
public:
    // 协程构造函数 初始化调度器、协程应该调用的callable等成员
    Coroutine(SharedSchedule& schedule, CoroutineFunction& function, int id)
        : schedule_(schedule), function_(function), id_(id)
        , status_(CoroutineStatus::Ready), stack_(nullptr)
        , size_(0), capacity_(0) {
    }
    // 协程析构函数 释放协程挂起后备份的运行栈
    ~Coroutine() {
        if (stack_) {
            delete[] stack_;
        }
    }
    // 调用callable
    void* start() {
        void* msg = nullptr;
        // 如果callable存在
        if (function_) {
            // 如果能通过weak提升到shared
            SharedSchedule schedule = schedule_.lock();
            if (schedule) {
                // 执行callable 参数是调度器对象的shared引用
                msg = function_(schedule);
            }
        }
        return msg;
    }

    void saveStack(const char* top) {
        // 在栈上创建一个临时变量 用于获取当前栈已经增长到哪里了
        char sp;
        // sp的地址一定要大于栈空间启始地址 否则就是栈溢出
        assert(&sp > top - MAX_STACK_SIZE);
        // 如果用于备份栈上内容的buffer的容量不够
        if (top - &sp > capacity_) {
            if (capacity_ > 0) {
                // 删除旧的buffer
                delete[] stack_;
            }
            // 创建新的buffer
            stack_ = new char[top - &sp];
            // 更新容量
            capacity_ = top - &sp;
        }
        // 拷贝栈上的内容到buffer
        memcpy(stack_, &sp, top - &sp);
        // 更新备份的内容的长度
        size_ = top - &sp;
    }
    // 将备份的栈内容加载到调度器的提供的栈空间
    void loadStack(char* top) const {
        // 注意栈是向下增长的 所以要拷贝到高地址的那一边
        memcpy(top - size_, stack_, size_);
    }
    // 设置协程运行状态
    void setStatus(CoroutineStatus status) {
        status_ = status;
    }
    // 获取协程运行状态
    CoroutineStatus getStatus() const {
        return status_;
    }
    // 获取协程保存的上下文
    ucontext_t* getContext() {
        return &context_;
    }

private:
    // 调度器指针
    WeakSchedule schedule_;
    // 可调用对象的引用
    CoroutineFunction& function_;
    // 协程id
    int id_;
    // 协程的运行状态
    CoroutineStatus status_;
    // 协程挂起时备份的栈内容
    char* stack_;
    // 栈备份的长度
    ptrdiff_t size_;
    // 用于备份栈的buffer的长度
    ptrdiff_t capacity_;
    // 协程运行的上下文
    ucontext_t context_;
};

// 调度器构造函数 创建协程运行时的栈空间
Schedule::Schedule(int size)  :capacity_(size), current_id_(-1), coroutine_status_(size, false), msg_(nullptr) {
    stack_ = new char[MAX_STACK_SIZE];
}
// 调度器析构函数 释放为协程运行分配的栈空间
Schedule::~Schedule() {
    delete [] stack_;
}
// 使用callable创建一个协程对象 返回协程id
int Schedule::createCoroutine(CoroutineFunction& function) {
    // 如果容量不够 就扩大一倍
    if (coroutines_.size() >= capacity_) {
        capacity_ *= 2;
        // 将扩大后未使用的id标志位设为false
        coroutine_status_.resize(capacity_, false);
    }
    for (int i = 0; i < capacity_; i++) {
        int id = i;
        // 找到最小未使用的id 参考open(2)
        if (!coroutine_status_[id]) {
            // 通过this指针创建shared指针
            SharedSchedule self = this->shared_from_this();
            // 创建一个协程对象
            SharedCoroutine coroutine = std::make_shared<Coroutine>(self, function, id);
            // 将id标志位设置为true
            coroutine_status_[id] = true;
            // 将协程对象插入到map中
            coroutines_[id] = coroutine;
            return id;
        }
    }
    // 代码不可能走到这里
    assert(0);
    return -1;
}
// 运行id对应的协程的callable
void* Schedule::runCoroutine(int id, void* msg) {
    // 找到指定的协程对象
    std::map<int, SharedCoroutine>::const_iterator it = coroutines_.find(id);
    // 找不到就返回
    if (it == coroutines_.cend()) {
        assert(0);
        return nullptr;
    }
    SharedCoroutine coroutine = it->second;
    // 获取协程对象的允许状态
    CoroutineStatus status = coroutine->getStatus();
    // 如果是刚创建 从未执行过
    if (status == CoroutineStatus::Ready) {
        // 获取协程的上下文
        ucontext_t* pcontext = coroutine->getContext();
        // 初始化协程的上下文
        getcontext(pcontext);
        // 把栈空间指针填在context结构体
        pcontext->uc_stack.ss_sp = stack_;
        pcontext->uc_stack.ss_size = MAX_STACK_SIZE;
        // 这个上下文结束后回到哪里
        pcontext->uc_link = &context_;
        // 设置协程运行状态为Running
        coroutine->setStatus(CoroutineStatus::Running);
        // 更新正在运行的协程id
        current_id_ = id;
        msg_ = msg;
        // 修改刚才get到的上下文 当这个上下文被激活时 填入的函数就会被执行
        makecontext(pcontext, reinterpret_cast<void(*)()>(runHelper), 1, this);
        // 获取当前上下文到左边的参数 并切换到右边这个上下文
        // 由于右边的上下文uc_link到当前上下文 所以右边的执行结束后会回到左边的上下文
        swapcontext(&context_, pcontext);
        return msg_;
    // 如果执行过 现在是挂起状态
    } else if(status == CoroutineStatus::Suspend) {
        // 获取协程挂起时的上下文
        ucontext_t* pcontext = coroutine->getContext();
        // 加载备份的栈内容
        coroutine->loadStack(stack_ + MAX_STACK_SIZE);
        // 设置协程运行状态为Running
        coroutine->setStatus(CoroutineStatus::Running);
        // 更新正在运行的协程id
        current_id_ = id;
        msg_ = msg;
        // 获取当前上下文到左边的参数 并切换到右边这个上下文 挂起的协程又运行起来了
        swapcontext(&context_, pcontext);
        return msg_;
    } else {
        assert(0);
        return nullptr;
    }
}
// 协程调用这个函数主动要求挂起
void* Schedule::yieldCoroutine(void* msg) {
    // 找到指定的协程对象
    std::map<int, SharedCoroutine>::const_iterator it = coroutines_.find(current_id_);
    if (it == coroutines_.cend()) {
        // 找不到？不可能的
        assert(0);
        return nullptr;
    }
    SharedCoroutine coroutine = it->second;
    // 获取协程的上下文
    ucontext_t* pcontext = coroutine->getContext();
    // context的地址一定要大于栈空间启始地址 否则就是栈溢出
    assert(reinterpret_cast<char*>(&pcontext) > stack_);
    // 保存运行时的栈的内容
    coroutine->saveStack(stack_ + MAX_STACK_SIZE);
    // 设置协程运行状态为Suspend
    coroutine->setStatus(CoroutineStatus::Suspend);
    // 将正在运行的协程id置空
    current_id_ = -1;
    msg_ = msg;
    // 备份协程上下文到左边的参数 并切换到右边这个上下文 此时回到runCoroutine函数调用swapcontext那里
    swapcontext(pcontext, &context_);
    return msg_;
}
// 获取id对应的协程的运行状态
int Schedule::getCoroutineStatus(int id) const {
    // 找到指定的协程对象
    std::map<int, SharedCoroutine>::const_iterator it = coroutines_.find(id);
    if (it == coroutines_.cend()) {
        // 找不到就返回无效
        return CoroutineStatus::Invalid;
    }
    SharedCoroutine coroutine = it->second;
    return coroutine->getStatus();
}
// 调用协程对象的callable
void Schedule::runCoroutineFunction() {
    // 获取调度器当前正在执行的协程对象
    SharedCoroutine coroutine = getCurrentCoroutine();
    void* msg = nullptr;
    if (coroutine) {
        // 调用
        msg = coroutine->start();
    }
    // 调用结束了就删除这个协程对象
    removeCurrentCoroutine();
    // 将正在运行的协程id置空
    current_id_ = -1;
    msg_ = msg;
}

// 从调度器移除已经执行完毕的协程
void Schedule::removeCurrentCoroutine() {
    int id = current_id_;
    // 找到指定的协程对象
    std::map<int, SharedCoroutine>::iterator it = coroutines_.find(id);
    if (it != coroutines_.end()) {
        // 从map中删掉它
        coroutines_.erase(it);
        // 将id标志位设置为false 表示id未使用
        coroutine_status_[id] = false;
    } else {
        // 找不到？不存在的
        assert(0);
    }
}

// 获取调度器当前正在执行的协程对象
Schedule::SharedCoroutine Schedule::getCurrentCoroutine() {
    int id = current_id_;
    // 找到指定的协程对象
    std::map<int, SharedCoroutine>::iterator it = coroutines_.find(id);
    if (it != coroutines_.end()) {
        return it->second;
    } else {
        // 找不到？不存在的
        assert(0);
        return SharedCoroutine();
    }
}

// c++函数runCoroutineFunction到c函数void(*)()的adapter
// makecontext函数的参数无法使用c++的函数对象 只能用这个将就一下先
void Schedule::runHelper(void* ptr) {
    // 强制类型转换
    Schedule* schedule = static_cast<Schedule*>(ptr);
    // 调用协程对象的callable
    schedule->runCoroutineFunction();
}

} // namespace u6th9d
