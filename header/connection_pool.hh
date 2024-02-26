#ifndef CONNECTION_POOL_HH
#define CONNECTION_POOL_HH
#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include <list>

#include "./locker.hh"

using std::list;
using std::string;

// 单例模式，一个sql的连接池
class connection_pool
{
public:
	// 单例模式，返回一个连接池
	static connection_pool& get_instance(string User, string PassWord, 
				string DataBaseName, int Port, string url = "localhost");

	MYSQL* get_conn();

	bool return_conn(MYSQL*);

	int get_remain_conn();
private:

	list<MYSQL*> conn_list;
	const int MAX_CONN;

	locker lock;
	sem sem_full, sem_empty;

	connection_pool();
	~connection_pool();
};


#endif