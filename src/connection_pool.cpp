#include "../header/connection_pool.hh"
#include "../header/show_error.hh"
connection_pool& connection_pool::get_instance (string User, string PassWord, 
				string DataBaseName, int Port, string Url)
{
	static connection_pool pool;
	for(int i = 0; i < pool.MAX_CONN; ++i)
	{
		MYSQL *con = NULL;
		con = mysql_init(con);

		if (con == NULL)
		{
			SHOW_ERROR
			exit(1);
		}
		con = mysql_real_connect(con, Url.c_str(), User.c_str(), PassWord.c_str(), 
				DataBaseName.c_str(), Port, NULL, 0);

		if (con == NULL)
		{
			SHOW_ERROR
			exit(1);
		}
		pool.conn_list.push_back(con);
	}
	return pool;
}

connection_pool::connection_pool():MAX_CONN(8), conn_list(0),
			sem_full(MAX_CONN), sem_empty(0), lock()
{}

connection_pool::~connection_pool() 
{
	lock.lock();
	if (conn_list.size() > 0)
	{
		for(auto &con : conn_list)
			mysql_close(con);
		conn_list.clear();
	}
	lock.unlock();
}

MYSQL* connection_pool::get_conn()
{
	MYSQL *ret = NULL;
	sem_full.wait();
	lock.lock();
	
	ret = conn_list.front();
	conn_list.pop_front();

	sem_empty.post();
	lock.unlock();

	return ret;
}

bool connection_pool::return_conn(MYSQL*con)
{
	if(NULL == con)
		return false;
	sem_empty.wait();
	lock.lock();

	conn_list.push_front(con);

	sem_full.post();
	lock.unlock();

	return true;
}

int connection_pool::get_remain_conn()
{
	int ret = 0;
	lock.lock();
	ret = conn_list.size();
	lock.unlock();

	return ret;
}
