#include "../header/http_conn.hh"
#include "../header/locker.hh"
#include "../header/connection_pool.hh"
#include "../header/timer.hh"
#include "../header/web_server.hh"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>

using std::cout;
using std::endl;

int main()
{
	web_server server("192.168.68.152", 8888);
	server.sql_pool_init();
	server.event_listen();
	server.event_loop();
	return 0;
}