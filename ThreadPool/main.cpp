#include "threadpool.h"
#include<chrono>
#include<iostream>
using namespace std;
using uLong = unsigned long long;
class MyTask :public Task {
public:
    MyTask(int b,int e):begin(b),end(e){}
    Any run() {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        uLong sum = 0;
        for (int i = begin;i < end;i++) {
            sum += i;
        }
        return sum;
    }
private:
    int begin;
    int end;
};

int main(void)
{
    {
        ThreadPool pool;
        pool.setMode(PoolMode::CACHEED);
        pool.start(4);
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res3 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res4 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res5 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res6 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
    }
#if 0
    {
        ThreadPool pool;
        pool.setMode(PoolMode::CACHEED);
        pool.start(3);

        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(100000000, 200000000));
        Result res3 = pool.submitTask(std::make_shared<MyTask>(200000000, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000000, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000000, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000000, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000000, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000000, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000000, 300000000));

        uLong sum1 = res1.get().cast<uLong>();
        uLong sum2 = res2.get().cast<uLong>();
        uLong sum3 = res3.get().cast<uLong>();

        cout << (sum1 + sum2 + sum3) << endl;
        uLong sum = 0;
        for (int i = 0;i < 300000000;i++) {
            sum += i;
        }
        cout << sum << endl;
    }
    getchar();
#endif
    return 0;
}

