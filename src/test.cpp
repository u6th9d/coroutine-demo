#include "coroutine.h"

#include <iostream>
#include <stddef.h>

using namespace u6th9d;

class FunctionObject {
public:
    FunctionObject(int64_t begin, int64_t end): begin_(begin), end_(end) {
    }

    void* operator()(Schedule::SharedSchedule schedule, int id) {
        while (begin_ < end_) {
            begin_++;
            void* ptr = (void*)begin_;
            ptr = schedule->yieldCoroutine(ptr);
            if (ptr) {
                int64_t num = (int64_t)ptr;
                std::cout << "my id: " << id << " yieldCoroutine function return number: " << num << std::endl;
            }
        }
        return nullptr;
    }

private:
    int64_t begin_;
    int64_t end_;
};

int main() {
    Schedule::SharedSchedule schedule = std::make_shared<Schedule>();
    FunctionObject obj1(0, 4);
    FunctionObject obj2(10, 16);

    Schedule::CoroutineFunction c1 = std::bind(obj1, std::placeholders::_1, 1);
    Schedule::CoroutineFunction c2 = std::bind(obj2, std::placeholders::_1, 2);

    int id1 = schedule->createCoroutine(c1);
    int id2 = schedule->createCoroutine(c2);

    while (schedule->getCoroutineStatus(id1) != Schedule::CoroutineStatus::Invalid) {
        int64_t num1 = rand()%10;
        void* ptr1 = (void*)num1;
        ptr1 = schedule->runCoroutine(id1, ptr1);
        if (ptr1) {
            std::cout << "id: " << id1 << " runCoroutine function return number: " << (int64_t)ptr1 << std::endl;
        }
    }
    while (schedule->getCoroutineStatus(id2) != Schedule::CoroutineStatus::Invalid) {
        int64_t num2 = rand()%10;
        void* ptr2 = (void*)num2;
        ptr2 = schedule->runCoroutine(id2, ptr2);
        if (ptr2) {
            std::cout << "id: " << id2 << " runCoroutine function return number: " << (int64_t)ptr2 << std::endl;
        }
    }

    return 0;
}

/*
    id: 0 runCoroutine function return number: 1
    my id: 1 yieldCoroutine function return number: 6
    id: 0 runCoroutine function return number: 2
    my id: 1 yieldCoroutine function return number: 7
    id: 0 runCoroutine function return number: 3
    my id: 1 yieldCoroutine function return number: 5
    id: 0 runCoroutine function return number: 4
    my id: 1 yieldCoroutine function return number: 3
    id: 1 runCoroutine function return number: 11
    my id: 2 yieldCoroutine function return number: 6
    id: 1 runCoroutine function return number: 12
    my id: 2 yieldCoroutine function return number: 2
    id: 1 runCoroutine function return number: 13
    my id: 2 yieldCoroutine function return number: 9
    id: 1 runCoroutine function return number: 14
    my id: 2 yieldCoroutine function return number: 1
    id: 1 runCoroutine function return number: 15
    my id: 2 yieldCoroutine function return number: 2
    id: 1 runCoroutine function return number: 16
    my id: 2 yieldCoroutine function return number: 7
*/
