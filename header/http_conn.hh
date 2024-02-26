#ifndef HTTP_CONN_HH
#define HTTP_CONN_HH
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include "connection_pool.hh"

const char *get_mime_type(const char *name);
void init_sql(connection_pool& pool);

class http_conn
{
	static constexpr int FILENAME_LEN = 128;
	static constexpr int READ_BUFFER_SIZE = 2048;
	static constexpr int WRITE_BUFFER_SIZE = 2048;
public:
	void show_read_buffer();
	void show_write_buffer();
	http_conn();
    void init(int sockfd, const sockaddr_in &addr, const char *root);
	void init(int fd, int ep_fd);					//新客户的init
	// ET模式，把client_fd的数据全部读到read_buffer
	bool read_once();
	void process();
	void unmap();
    bool write();
	~http_conn() {}
private:
	enum STATE {
		REQUESTLINE = 0, //处理请求行
		HEADER,			 //处理头部
		CONTENT,		 //处理正文
	};
	enum LINE_STATE
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };
	enum HTTP_CODE {
		NO_REQUEST = 0,		//请求不完整
        GET_REQUEST,	//得到请求
        BAD_REQUEST,	//请求有错误
        NO_RESOURCE,	//没有这个资源
        FORBIDDEN_REQUEST, //客户端没有权限
        FILE_REQUEST,	//
        INTERNAL_ERROR,
        CLOSED_CONNECTION	
	};
	enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

	
	//保活的init
	void init();			
	void addfd();									
	void modfd(unsigned mod);									
	
	//解析请求报文
	HTTP_CODE parse();
	//解析请求行
	HTTP_CODE parse_request_line(char *);
	//解析头部
	HTTP_CODE parse_header(char* );
	//解析正文
	HTTP_CODE parse_content(char* );
	//遇到/r/n将其变成/0/0即可,从checked_indx开始就是当前line
	LINE_STATE prepare_line();
	char *get_line();
	//向客户回应
	HTTP_CODE respond();
	//向write_buffer写
	bool process_write(HTTP_CODE ret);
	bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

	void close_conn();


public:
	static int epoll_fd;
	bool ready_for_send = false;
	static MYSQL* mysql;

	//线程才会设置这两个
	bool timer_flag = false;
	int improv = 0;
private:
	STATE m_state = REQUESTLINE;  				//状态机当前状态

	int client_fd = -1;
    sockaddr_in m_address;

	char read_buffer[READ_BUFFER_SIZE] = ""; 	//读缓存
	int read_indx = 0, 							//标记read_buffer的end
		checked_indx = 0, 						//标记已经check的end
		start_line_indx = 0;					//标记读当前行的begin

	char write_buffer[WRITE_BUFFER_SIZE] = ""; 	//写缓存
	int write_indx = 0;
	int bytes_to_send = 0;
    int bytes_have_send = 0;
	//请求相关
	METHOD method;
    char real_file[FILENAME_LEN] = "";
	bool keep_alive = false;
	char *url = NULL;				//请求头里的
	const char *root = NULL;					//getcwd
	char *memory_of_file = NULL;				//内存映射
	char *version = NULL;
    char *host = NULL;
	int content_length = 0;
    struct stat file_stat;
    struct iovec io_vector[2];					//分散写，一个内存块是write_buffer一个是file
    int iv_count = 0;
    int cgi = 0;        						//是否启用的POST

    char *content_string = NULL; 				//存储请求头数据
};

#endif