
#include <iostream>
#include <future>
#include <thread>
#include "threadpool.h"
using namespace std;

int sum(int a, int b) {
    return a+b;
}

int main()
{
    ThreadPool pool;
    pool.start(2);
    std::future<int> res2 = pool.submitTask(sum, 1, 2);
    std::future<int> res1 = pool.submitTask(sum, 1, 2);
    std::future<int> res3 = pool.submitTask(sum, 1, 2);
    std::future<int> res4 = pool.submitTask(sum, 1, 2);
    std::future<int> res = pool.submitTask(sum, 1, 2);
    cout << res.get() << "\n";
    
}

