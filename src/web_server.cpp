#include "../header/web_server.hh"
#include "../header/connection_pool.hh"
#include "../header/show_error.hh"
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
int ep_fd = -1;
int pipefd[2] = {-1, -1};
void web_server::sql_pool_init()
{
	init_sql(connection_pool::get_instance(sql_name, sql_psword, sql_dbname, sql_port, sql_url));
}

void web_server::event_listen()
{
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, IP, &addr.sin_addr.s_addr);
	socklen_t len = sizeof(addr);

	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(-1 == listen_fd)
	{
		SHOW_ERROR
		perror("socket");
	}
	set_no_blocking(listen_fd);
	int reuse = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	if(-1 == bind(listen_fd, (struct sockaddr*)&addr, len))
	{
		SHOW_ERROR
		perror("bind");
	}
	listen(listen_fd, 4);
	
	if(-1 == (epoll_fd = epoll_create(1)))
	{
		SHOW_ERROR
		perror("epoll_create");
	}
	ep_fd = timer::epoll_fd = http_conn::epoll_fd = this->epoll_fd;
	add_fd(epoll_fd, listen_fd, false);

	if(-1 == socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd))
		SHOW_ERROR
    set_no_blocking(pipefd[1]);
    add_fd(epoll_fd, pipefd[0], false);

	//注册信号处理函数
    reg_sig(SIGPIPE, SIG_IGN);
    reg_sig(SIGALRM, sig_handler);
    reg_sig(SIGTERM, sig_handler);

	alarm(TIMESLOT);
}

void web_server::event_loop()
{
	bool time_out = false, shut_down = false;
	while (!shut_down)
	{
		int n = epoll_wait(epoll_fd, events, MAX_EVENT_NUMBER, -1);
		if(-1 == n && errno != EINTR)
		{
			SHOW_ERROR
			perror("epoll_wait");
			exit(1);
		}

		for(int i = 0; i < n; ++i)
		{
			int cfd = events[i].data.fd;
			if(cfd == listen_fd)
			{
				deal_listenfd();
			}
			else if(cfd == pipefd[0] && (events[i].events & EPOLLIN))
			{
				deal_signal(time_out, shut_down);
			}
			else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                t_heap.del(&timers[cfd]);
            }
			else if (events[i].events & EPOLLIN)
            {
                deal_client_read(cfd);
            }
			else if (events[i].events & EPOLLOUT)
            {
                deal_client_write(cfd);
            }
		}	

		if(time_out)
		{
			t_heap.tick();
			time_out = false;
			alarm(TIMESLOT);
		}	
	}
	
}

void web_server::deal_client_read(int cfd)
{
	t_heap.adjust(timers + cfd);
	//append
	pool.append(users + cfd, false);
	while (true)
	{
		if (1 == users[cfd].improv)
		{
			if (1 == users[cfd].timer_flag)
			{
				t_heap.del(timers + cfd);
				users[cfd].timer_flag = 0;
			}
			users[cfd].improv = 0;
			break;
		}
	}
}
void web_server::deal_client_write(int cfd)
{
	t_heap.adjust(timers + cfd);
	pool.append(users + cfd, true);
	while (true)
	{
		if (1 == users[cfd].improv)
		{
			if (1 == users[cfd].timer_flag)
			{
				t_heap.del(&timers[cfd]);
				users[cfd].timer_flag = 0;
			}
			users[cfd].improv = 0;
			break;
		}
	}
}

void web_server::deal_signal(bool& time_out, bool& shut_down)
{
	while(1)
	{
		bzero(signals, 1024);
		int n = recv(pipefd[0], signals, 1024, 0);
		if(n == -1)  
		{
			if(errno != EAGAIN && errno != EWOULDBLOCK)
				SHOW_ERROR
			return;
		}	

		for(int i = 0; i < n; ++i)
		{
			switch (signals[i])
			{
			case SIGALRM:
				time_out = true;
				break;
			case SIGTERM:
				shut_down = true;
				break;
			}
		}
	}
}

void web_server::deal_listenfd()
{
	while(1)
	{
		struct sockaddr_in addr;
		socklen_t len = sizeof(addr);
		int cfd = accept(listen_fd, (struct sockaddr*)&addr, &len);

		//Too busy
		if(cfd >= MAX_CLIENT)
		{
			SHOW_ERROR
			break;
		}
		//accept出错
		if(cfd == -1)  
		{
			if(errno != EAGAIN && errno != EWOULDBLOCK)
			{
				SHOW_ERROR
				perror("accept");
				exit(1);
			}
			break;
		}
		
		users[cfd].init(cfd, addr, root); 
		timers[cfd].init(cfd, &addr);
		t_heap.in(timers + cfd);
	}
}

void reg_sig(int sig, void (*handler)(int))
{
	struct sigaction act;
    memset(&act, '\0', sizeof(act));
    act.sa_handler = handler;
	act.sa_flags |= SA_RESTART;
    sigfillset(&act.sa_mask);
    if(-1 == sigaction(sig, &act, NULL))
		SHOW_ERROR
}

void sig_handler(int sig)
{
	send(pipefd[1], (char *)&sig, 1, 0);
}

bool add_fd(int ep_fd, int fd, bool oneshot)
{
	struct epoll_event e;
	e.data.fd = fd;
	e.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
	if(oneshot)
		e.events |= EPOLLONESHOT;
	set_no_blocking(fd);
	epoll_ctl(ep_fd, EPOLL_CTL_ADD, fd, &e);
}

void set_no_blocking(int fd)
{
	int flag = fcntl(fd, F_GETFL);
	flag |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flag);
}

http_conn web_server::users[MAX_CLIENT];
char web_server::root[128] = "";
web_server::web_server(const char* ip, int port) : pool(pthread_pool::get_pool())
{
	strcpy(IP, ip);
	this->port = port;
	if(getcwd(root, 128) == NULL)
	{
		SHOW_ERROR
		exit(1);
	}
	strcat(root, "/root");
	memset(IP, '\0', strlen(IP));
}

web_server::~web_server(){}
