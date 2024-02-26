#ifndef TIMER_HH
#define TIMER_HH

#include <arpa/inet.h>
#include <ctime>
#include <vector>
using std::vector;

const time_t TIMESLOT = 5;

class timer
{
friend class timer_heap;

private:
	int cfd = -1;
	struct sockaddr_in addr;
public:
	// 过期时间
	time_t expire = 0;
	static int epoll_fd;
	void init(int cfd, struct sockaddr_in* addr);
	void expire_call_back();
	timer();
	~timer();
};

class timer_heap
{
private:
	int end_indx = 1; //方便堆父子节点的计算

	vector<timer*> heap;
public:
	// 每过一个TIMESLOT就运行一次，检查有没有client过期
	void tick();
	bool in(timer*);
	bool out(int pos = 1);
	timer* top();
	void up(int);
	void down(int);
	bool adjust(timer*);
	void del(timer*);

	timer_heap();
	~timer_heap();
};


#endif