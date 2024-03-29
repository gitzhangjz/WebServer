#ifndef WEB_SERVER_HH
#define WEB_SERVER_HH

#include <vector>
#include <string>
#include "http_conn.hh"
#include "timer.hh"
#include "pthread_pool.hh"

using std::vector;
using std::string;
const int MAX_CLIENT = 10000;    
const int MAX_EVENT_NUMBER = 10000; //最大事件数

void set_no_blocking(int fd);

// 监听client_fd时开启EPOLLONESHOT
bool add_fd(int ep_fd, int fd, bool oneshot);

//注册信号处理函数
void reg_sig(int, void (*handler)(int));
// 信号处理函数：信号写入pipe，交给epoll处理
extern int ep_fd; 
void sig_handler(int sig);

class web_server
{
public:
	//timer的tick如果监测到cfd超时，需要设置http_conn的timer_flag
	static char root[128]; // 网站根目录
	static http_conn users[MAX_CLIENT]; // 客户端连接
private:
	int cur_client = 0;
	timer timers[MAX_CLIENT];	// 定时器，每个客户端连接一个定时器，用于超时处理
	timer_heap t_heap;		// 定时器堆，用于超时处理
	
	//线程池	
	pthread_pool &pool;

	//数据库相关参数
	string sql_name = "root";
	string sql_psword = "root";
	string sql_dbname = "webdb";
	string sql_url = "localhost";
	int sql_port = 4321;

	//epool相关参数
	char IP[16]; // 服务器IP地址
	uint16_t port = 0; // 服务器端口
	int listen_fd = -1;
	int epoll_fd = -1;
	struct epoll_event events[MAX_EVENT_NUMBER]; // 事件数组，epoll_wait()将填充这个数组
	char signals[1024] = "";

	void deal_listenfd();
	void deal_signal(bool &time_out, bool &shut_down);
	void deal_client_read(int fd);
	void deal_client_write(int fd);
public:
	// sql连接池初始化
	void sql_pool_init();
	void event_listen();
	void event_loop();
	
	web_server(const char *ip, int port);
	~web_server();
};

#endif