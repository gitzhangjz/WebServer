#include "../header/pthread_pool.hh"
#include "../header/show_error.hh"

pthread_pool pthread_pool::pool(MAX_THREAD);

pthread_pool &pthread_pool::get_pool()
{
	return pool;
}

pthread_pool::pthread_pool(int thread_num)
{
	threads = new pthread_t[thread_num];
	for(int i = 0; i < thread_num; ++i)
	{
		if (pthread_create(threads + i, NULL, work, this) != 0)
        {
            delete[] threads;
            throw std::exception();
        }
        if (pthread_detach(threads[i]))
        {
            delete[] threads;
            throw std::exception();
        }
	}
}

bool pthread_pool::append(http_conn* task, bool ready_for_send)
{
	if(NULL == task)
	{
		SHOW_ERROR
		return false;
	}
	task->ready_for_send = ready_for_send;
	mutex.lock();
	while(task_que.size() >= MAX_REQUEST)
		cond_not_full.wait(mutex.get());

	task_que.push(task);

	cond_not_empty.signal();
	mutex.unlock();
	return true;
}

void pthread_pool::run()
{
	http_conn *task = NULL;
	while (true)
	{
		mutex.lock();
		while(task_que.empty())
			cond_not_empty.wait(mutex.get());
		
		task = task_que.front();
		task_que.pop();

		cond_not_full.signal();
		mutex.unlock();
		//拿到任务
		if(task->ready_for_send == false)
		{
			if (task->read_once())
			{
				// task->show_read_buffer();
				task->improv = 1;
				task->process();
			}
			else
			{
				task->timer_flag = 1;
				task->improv = 1;
			}
		}
		else
		{
			if (task->write())
			{
				task->improv = 1;
			}
			else
			{
				task->timer_flag = 1;
				task->improv = 1;
			}
		}

	}
	
}

void *pthread_pool::work(void *arg)
{
	pthread_pool* p = (pthread_pool*)arg;
	p->run();
}

pthread_pool::~pthread_pool()
{
	while(task_que.size() > 0)
		task_que.pop();
	
	mutex.~locker();
	cond_not_empty.~cond();
	cond_not_full.~cond();
	delete threads;
}