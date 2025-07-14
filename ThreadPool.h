#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include<vector>
using std::vector;

#include<queue>
using std::queue;

#include<thread>//线程相关
using std::thread;
//没有 std::this_thread
using std::this_thread::sleep_for;

#include<mutex>//线程锁有关
using std::mutex;
using std::lock_guard;
using std::unique_lock;

#include<condition_variable>//条件变量
using std::condition_variable;

#include<atomic>//原子操作
using std::atomic;

#include<future>//异步
using std::future;
using std::promise;

#include<functional>//绑定器(bind之类),函数式编程,lambda表达式
using std::function;
using std::bind;

#include<stdexcept>
using std::exception;
using std::runtime_error;

#include<utility>
using std::move;

//线程池类
class ThreadPool{

    private:
        //该vector就是线程池,线程池就是该vector
        vector<thread> pool;
        //任务队列,通过绑定器,将任务放进去
        queue<function<void()>> que;
        //互斥锁,防止线程之间竞争过度
        mutex queue_mutex;
        //条件竞争,用于线程之间合理竞争
        condition_variable cd;//条件变量
        //是否停止的标志,并初始化为不停止
        atomic<bool> stopFlag = false;//

    
    public:
    
        // 默认构造函数,初始化线程
        ThreadPool(size_t numOFthreads){
            for(size_t i = 0;i<numOFthreads;i++){
                pool.emplace_back([this]{//this是指向当前对象的指针,无论哪个线程访问,都指向同一个对象实例
                    for(;;){
                        function<void()> task;
                        {
                            //获取互斥锁
                            unique_lock<mutex> lock(queue_mutex);
                            //等待直到1.线程池停止或者有2.任务队列中有任务可执行
                            this/*当前这条线程*/->cd.wait(lock,[this]{return this->stopFlag.load() || !this->que.empty();});
                            //如果线程池停止且没有任务,则任务退出
                            if(this->stopFlag.load() && this->que.empty()){
                                return;
                            }
                            //获取任务并从队列中移除
                            task = move(this->que.front());
                            this->que.pop();
                        }
                        //执行任务
                        task();
                    }
                });
            }
        }
        
    
        // 禁止拷贝构造和赋值构造
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        //向线程池中添加任务,返回 future作为结果
        template<class F, class... Args>
        auto enqueue(F& f, Args&&... args)
            ->future<typename std::result_of<F(Args...)>::type> {
            using return_type = typename std::result_of<F(Args...)>::type;
            
            auto task = std::make_shared<std::packaged_task<return_type()>>(
                std::bind(std::forward<F>(f),std::forward<Args>(args)...)
            );
            //获取任务的 future
            std::future<return_type> res = task->get_future();//异步获取结果
            {
                lock_guard<mutex> lock(queue_mutex);
                //如果停止标志为true,则抛出异常,防止线程池停止后继续添加任务
                if(stopFlag.load() && que.empty())
                    throw runtime_error("ThreadPool is stopped");
                //转而向队列中添加任务
                que.emplace([task](){(*task)();});
            }
            cd.notify_one();//通知一个线程
            return res;//向栈返回异步任务的结果
        }

        //析构函数,等待所有线程结束,但不释放资源
        inline ~ThreadPool(){
            {
                //
                unique_lock<mutex> lock(queue_mutex);
                stopFlag = true;
            }
            cd.notify_all();//通知所有线程而不是一两个,告诉他们要关闭了     
            for(auto &thread : pool){//线程池里的所有线程
                thread.join();//等待所有线程结束//join反而是等待线程结束,也对
            }
        }

};


#endif
