#include "../header/timer.hh"
#include "../header/show_error.hh"
#include "../header/web_server.hh"
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <sys/epoll.h>
#include <unistd.h>

using std::swap;
using std::min;

void timer::init(int cfd, struct sockaddr_in* addr_d)
{
	this->cfd = cfd;
	memcpy(&addr, addr_d, sizeof(addr));
	expire = time(0) + TIMESLOT;
}

void timer::expire_call_back()
{
	if(-1 == epoll_ctl(epoll_fd, EPOLL_CTL_DEL, this->cfd, NULL))
	{
		SHOW_ERROR
		perror("epoll_ctl");
	}
	// printf("close???????????\n");
	close(this->cfd);
	cfd = -1;
	memset(&addr, 0, sizeof(addr));
}

int timer::epoll_fd = -1;
timer::timer() {};
timer::~timer(){}


void timer_heap::tick()
{
	time_t now;
	timer* root;
	while(1)
	{
		now = time(NULL);
		root = top();
		if(root == NULL || root->expire > now)
			break;
		
		out();
	}
}

bool timer_heap::in(timer* t)
{
	if(end_indx >= heap.size())
	{
		SHOW_ERROR
		return false;
	}

	heap[end_indx] = t;
	up(end_indx++);
}

void timer_heap::up(int indx)
{
	while(indx > 1)
	{
		if(heap[indx]->expire < heap[indx / 2]->expire)
		{
			swap(heap[indx], heap[indx / 2]);
			indx /= 2;
		}
		else
			break;
	}
}

void timer_heap::down(int indx)
{
	while(indx < end_indx)
	{
		//右孩子出界,且左孩子较小
		if(indx * 2 + 1 == end_indx && heap[indx]->expire > heap[indx * 2]->expire) 
		{
			swap(heap[indx], heap[indx * 2]);
			indx *= 2;
		}
		//都在界内，且有一个比较小
		else if ( indx * 2 + 1 < end_indx && 
				  min(heap[indx * 2]->expire, heap[indx * 2 + 1]->expire) < heap[indx]->expire)
		{
			if(heap[indx * 2]->expire < heap[indx * 2 + 1]->expire)
			{
				swap(heap[indx], heap[indx * 2]);
				indx *= 2;
			}
			else 
			{
				swap(heap[indx], heap[indx * 2 + 1]);
				indx *= 2; ++indx;
			}
		}
		else
			break;
	}
}

timer* timer_heap::top()
{
	return heap[1];
}

bool timer_heap::out(int pos)
{
	if(pos >= end_indx)
	{
		SHOW_ERROR
		return false;
	}

	swap(heap[pos], heap[--end_indx]);
	heap[end_indx]->expire_call_back();
	heap[end_indx] = NULL;
	down(pos);
}
bool timer_heap::adjust(timer* t)
{
	t->expire = time(NULL) + TIMESLOT;
	for(int i = 1; i < end_indx; ++i)
		if(t == heap[i])
		{
			up(i);
			down(i);
			return true;
		}
	
	return false;
}

void timer_heap::del(timer* t)
{
	for(int i = 1; i < end_indx; ++i)
		if(t == heap[i])
		{
			out(i);
			return;
		}
}

timer_heap::timer_heap() : heap(MAX_CLIENT + 1, NULL) {}

timer_heap::~timer_heap()
{
	heap.clear();
}