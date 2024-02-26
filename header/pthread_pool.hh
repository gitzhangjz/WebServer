#ifndef PTHREAD_POOL_HH
#define PTHREAD_POOL_HH

#include <queue>
#include <pthread.h>
#include "http_conn.hh"
#include "locker.hh"
using std::queue;

class pthread_pool
{
static constexpr int MAX_THREAD = 8;
static constexpr int MAX_REQUEST = 10000;
public:
	//返回一个线程池对象（单例）
	static pthread_pool &get_pool();
	//添加任务
	bool append(http_conn* h, bool ready_for_write);
private:
	// 初始化线程池，传入线程数量
	pthread_pool(int thread_num);
	~pthread_pool();
	//线程工作函数
	static void *work(void *args);
	void run();
// --------------------------------------
	//线程池（单例）
	static pthread_pool pool; 
	//线程数组
	pthread_t *threads = NULL;
	//任务队列
	queue<http_conn*> task_que;
	//mutex.get()返回mutex指针，传给条件变量
	locker mutex;
	cond cond_not_full;
	cond cond_not_empty;
};

#endif