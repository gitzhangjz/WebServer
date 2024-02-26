#include "../header/http_conn.hh"
#include "../header/show_error.hh"
#include "../header/connection_pool.hh"
#include <sys/uio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <map>
#include <string>
using std::map;
using std::string;
using std::pair;

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker sql_lock;
map<string, string> mp;
MYSQL *http_conn::mysql = NULL;
void set_no_blocking(int fd);
int http_conn::epoll_fd = -1;

void http_conn::process()
{
	HTTP_CODE parse_ret = parse();
    // 没有请求，再次设置epoll
	if(NO_REQUEST == parse_ret)
	{
		modfd(EPOLLIN);
		return;
	}
	//将网页或者错误页面写入write_buffer
	bool write_ret = process_write(parse_ret);

	//向write_buffer写失败
	if (!write_ret)
	{
		SHOW_ERROR
		close_conn();
	}else // 解析成功，epoll监听写
		modfd(EPOLLOUT);
}

bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() &&
           add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}

bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", 
				(keep_alive == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

bool http_conn::add_response(const char *format, ...)
{
    if (write_indx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(write_buffer + write_indx, 
				WRITE_BUFFER_SIZE - 1 - write_indx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - write_indx))
    {
        va_end(arg_list);
        return false;
    }
    write_indx += len;
    va_end(arg_list);

    return true;
}

// 根据指令将网页写入写缓存
bool http_conn::process_write(HTTP_CODE ret)
{
	switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
		{
			SHOW_ERROR
            return false;
		}
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
        {
			SHOW_ERROR
            return false;
		}
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (file_stat.st_size != 0)
        {
            add_headers(file_stat.st_size);
            io_vector[0].iov_base = write_buffer;
            io_vector[0].iov_len = write_indx;
            io_vector[1].iov_base = memory_of_file;
            io_vector[1].iov_len = file_stat.st_size;
            iv_count = 2;
            bytes_to_send = write_indx + file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
			break;
        }
    }
    default:
        return false;
    }
    io_vector[0].iov_base = write_buffer;
    io_vector[0].iov_len = write_indx;
    iv_count = 1;
    bytes_to_send = write_indx;
    return true;
}

// 将数据读到read_buffer
bool http_conn::read_once()
{
	if(read_indx + 1 >= READ_BUFFER_SIZE)
	{
		SHOW_ERROR
		return false;
	}
	while (1)
	{
		int n = recv(client_fd, read_buffer + read_indx, 
						READ_BUFFER_SIZE - read_indx, 0);
		//非阻塞情况下
		if(n > 0)
		{
			read_indx += n;
		}
		// 非阻塞情况下，socket里没数据
		else if(errno == EAGAIN || errno == EWOULDBLOCK)
		{
			return true;
		}
		//发生错误
		else
		{
			perror("read_once");
			return false;
		}
	}
}

http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
	//第一个空格位置归零
	char *sp = strpbrk(text, " \t");
	if(NULL == sp)
	{
		SHOW_ERROR
		return BAD_REQUEST;
	}
	*sp++ = '\0';

	// 得到method
	if(0 == strcasecmp(text, "GET"))
		method = GET;
	else if(0 == strcasecmp(text, "POST"))
	{
		method = POST;
		cgi = 1;
	}
	else
	{
		SHOW_ERROR
		return BAD_REQUEST;
	}

	// 得到URI地址
	sp += strspn(sp, " \t");
	url = sp;
	sp = strpbrk(sp, " \t");
	if(NULL == sp)
	{
		SHOW_ERROR
		return BAD_REQUEST;
	}
	*sp++ = '\0';

	// 得到http协议版本
	sp += strspn(sp, " \t");
	version = sp;
	if(0 != strcasecmp(sp, "HTTP/1.1"))
	{
		// SHOW_ERROR
		return BAD_REQUEST;
	}

	if (strlen(url) == 1)
        strcat(url, "judge.html");

	printf("请求行解析完成!\n");
	if(method == GET)
		printf("GET ");
	if(method == POST)
		printf("POST ");
	printf("%s\n",url);
	return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_header(char *text)
{
	// 读到头部的空行（结尾）
	if('\0' == text[0])
	{
		if(0 != content_length)
		{
			m_state = CONTENT;
			return NO_REQUEST;
		}

		return GET_REQUEST;
	}
	else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            keep_alive = true;
        }
    }
	else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        content_length = atol(text);
    }
	else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        host = text;
    }
    else 
	{
		// printf("不关心的选项\n")
	}

	return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
	if (read_indx >= (content_length + checked_indx))
    {
        text[content_length] = '\0';
        //POST请求中最后为输入的用户名和密码
        content_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::respond()
{
    strcpy(real_file, root);
	int root_len = strlen(real_file);
    const char *p = strrchr(url, '/');

	if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        //根据标志判断是登录检测还是注册检测
        // char flag = url[1];
        char *url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real, "/");
        strcat(url_real, url + 2);
        strncpy(real_file + root_len, url_real, FILENAME_LEN - root_len - 1);
        free(url_real);

        //将用户名和密码提取出来
        char name[100], password[100];
        int i;
        for (i = 5; content_string[i] != '&'; ++i)
            name[i - 5] = content_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; content_string[i] != '\0'; ++i, ++j)
            password[j] = content_string[i];
        password[j] = '\0';

        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char sql_insert[200];
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

			//没有该用户
            if (mp.find(name) == mp.end())
            {
                sql_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                mp.insert(pair<string, string>(name, password));
                sql_lock.unlock();

                if (!res)
                    strcpy(url, "/log.html");
                else
                    strcpy(url, "/registerError.html");
            }
            else
                strcpy(url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (mp.find(name) != mp.end() && mp[name] == password)
                strcpy(url, "/welcome.html");
            else
                strcpy(url, "/logError.html");
        }
    }

	char real_url[200] = "";
	if (*(p + 1) == '0')
    {
        strcpy(real_url, "/register.html");
        strncpy(real_file + root_len, real_url, strlen(real_url));
    }
    else if (*(p + 1) == '1')
    {
        strcpy(real_url, "/log.html");
        strncpy(real_file + root_len, real_url, strlen(real_url));
    }
    else if (*(p + 1) == '5')
    {
        strcpy(real_url, "/picture.html");
        strncpy(real_file + root_len, real_url, strlen(real_url));
    }
    else if (*(p + 1) == '6')
    {
        strcpy(real_url, "/video.html");
        strncpy(real_file + root_len, real_url, strlen(real_url));
    }
    else if (*(p + 1) == '7')
    {
        strcpy(real_url, "/fans.html");
        strncpy(real_file + root_len, real_url, strlen(real_url));
    }
    else
        strncpy(real_file + root_len, url, FILENAME_LEN - root_len - 1);

	printf("real_file:%s\n", real_file);

    if (stat(real_file, &file_stat) < 0)
        return NO_RESOURCE;

    if (!(file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    if (S_ISDIR(file_stat.st_mode))
        return BAD_REQUEST;

    int fd = open(real_file, O_RDONLY);
    memory_of_file = (char *)mmap(0, file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// http解析
http_conn::HTTP_CODE http_conn::parse()
{
	LINE_STATE content_state = LINE_OK, request_or_header_state;
    HTTP_CODE ret = NO_REQUEST;
	char *text;

	while((m_state == CONTENT && content_state == LINE_OK) || 
			LINE_OK == (request_or_header_state = prepare_line()))
	{
		text = get_line();
		start_line_indx = checked_indx;
		switch (m_state)
		{
			case REQUESTLINE:
			{
				if(BAD_REQUEST == parse_request_line(text))
				{
					// SHOW_ERROR
					return BAD_REQUEST;
				}
				m_state = HEADER;
				break;
			}
			case HEADER:
			{
				ret = parse_header(text);
				if(GET_REQUEST == ret) 	//method是GET的情况下，解析完header就可以回应了
					return respond();
				else if(BAD_REQUEST == ret)
				{
					SHOW_ERROR
					return BAD_REQUEST;
				}
				break;
			}
			case CONTENT:
			{
				ret = parse_content(text);
				if (GET_REQUEST == ret) //密码存在psword_is_right
					return respond();
				content_state = LINE_OPEN;
				break;
			}
			default:
			{
				return INTERNAL_ERROR;
			}
		}
	}

	if(m_state != CONTENT && request_or_header_state == LINE_BAD)
	{
		SHOW_ERROR
		return BAD_REQUEST;
	}
	return NO_REQUEST;
}

http_conn::LINE_STATE http_conn::prepare_line()
{
	for(; checked_indx < read_indx; ++checked_indx)
	{
		char tmp = read_buffer[checked_indx];
		if('\r' == tmp)
		{
			if(checked_indx == read_indx - 1) // 到头了
				return LINE_OPEN;
			else if('\n' == read_buffer[checked_indx + 1]) //读到行尾
			{
				read_buffer[checked_indx] = read_buffer[checked_indx + 1 ] = '\0';
				checked_indx += 2;
				return LINE_OK;
			}
			SHOW_ERROR
			return LINE_BAD;
		}
		else if('\n' == tmp)
		{
			if('\r' == read_buffer[checked_indx - 1]) //读到行尾
			{
				read_buffer[checked_indx - 1] = read_buffer[checked_indx] = '\0';
				++checked_indx;
			}
			SHOW_ERROR
			return LINE_BAD;
		}
	}

	return LINE_OPEN;
}

void http_conn::show_read_buffer()
{
	printf("read_buffer:---------------------------------\n%s", read_buffer);
	printf("read_buffer end ----------------------------------------\n");
}

void http_conn::show_write_buffer()
{
	printf("write_buffer:---------------------------------\n%swrite_indx:%d\n", 
					write_buffer, write_indx);
	printf("write_buffer end ----------------------------------------\n");
}

void http_conn::unmap()
{
    if (memory_of_file)
    {
        munmap(memory_of_file, file_stat.st_size);
        memory_of_file = 0;
    }
}

bool http_conn::write()
{
    int temp = 0;

    if (bytes_to_send == 0)
    {
        modfd(EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        temp = writev(client_fd, io_vector, iv_count);

        if (temp < 0)
        {
			//写操作阻塞，但fd是nonblocking，等到有写空间再写，或者已经写完了
            if (errno == EAGAIN)
            {
                modfd(EPOLLOUT);
                return true;
            }
            unmap();
			perror("writev");
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        if (bytes_have_send >= io_vector[0].iov_len)
        {
            io_vector[0].iov_len = 0;
            io_vector[1].iov_base = memory_of_file + (bytes_have_send - write_indx);
            io_vector[1].iov_len = bytes_to_send;
        }
        else
        {
            io_vector[0].iov_base = write_buffer + bytes_have_send;
            io_vector[0].iov_len = io_vector[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)
        {
            unmap();
            modfd(EPOLLIN);
            if (keep_alive)
            {
				init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

http_conn::http_conn() = default;

// 初始化该客户连接：设置epoll，地址，根目录
void http_conn::init(int sockfd, const sockaddr_in &addr, const char *root)
{
	client_fd = sockfd;
	addfd();
    m_address = addr;
	this->root = root;							//getcwd + /root
	init();
}

// 初始化一些成员变量
void http_conn::init()
{
	bytes_to_send = 0;
    bytes_have_send = 0;
    m_state = REQUESTLINE;
    keep_alive = false;
    method = GET;
    url = NULL;
    version = NULL;
    content_length = 0;
    host = NULL;
    start_line_indx = 0;
    checked_indx = 0;
    read_indx = 0;
    write_indx = 0;
    cgi = 0;
    m_state = REQUESTLINE;
	ready_for_send = false;
    timer_flag = 0;
    improv = 0;

	memory_of_file = NULL;
    memset(read_buffer, '\0', READ_BUFFER_SIZE);
    memset(write_buffer, '\0', WRITE_BUFFER_SIZE);
    memset(real_file, '\0', FILENAME_LEN);
}

// 将该客户端的fd加入epoll监听
void http_conn::addfd()
{
    set_no_blocking(client_fd);
	epoll_event event;
    event.data.fd = client_fd;

	event.events = EPOLLONESHOT | EPOLLIN | EPOLLET | EPOLLRDHUP;
    epoll_ctl(http_conn::epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
}

void http_conn::modfd(unsigned mod)
{
	epoll_event e;
	e.events = mod | EPOLLET | EPOLLHUP | EPOLLONESHOT;
	e.data.fd = client_fd;
	epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &e);
}

void http_conn::close_conn()
{
	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, 0);
    close(client_fd);
	client_fd = -1;
}

char * http_conn::get_line() {return read_buffer + start_line_indx; }

void init_sql(connection_pool& p)
{
	MYSQL *mysql = http_conn::mysql = p.get_conn();

	if (mysql_query(mysql, "SELECT username,passwd FROM user"))
		SHOW_ERROR

	MYSQL_RES *result = mysql_store_result(mysql);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
		mp[temp1] = temp2;
    }

	mysql_free_result(result);
}

const char *get_mime_type(const char *name)
{
    const char* dot = strrchr(name, '.');	//自右向左查找‘.’字符;如不存在返回NULL
    /*
     *charset=iso-8859-1	西欧的编码，说明网站采用的编码是英文；
     *charset=gb2312		说明网站采用的编码是简体中文；
     *charset=utf-8			代表世界通用的语言编码；
     *						可以用到中文、韩文、日文等世界上所有语言编码上
     *charset=euc-kr		说明网站采用的编码是韩文；
     *charset=big5			说明网站采用的编码是繁体中文；
     *
     *以下是依据传递进来的文件名，使用后缀判断是何种文件类型
     *将对应的文件类型按照http定义的关键字发送回去
     */
    if (dot == (char*)0)
        return "text/plain; charset=utf-8";
    else if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    else if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    else if (strcmp(dot, ".gif") == 0)
        return "image/gif";
    else if (strcmp(dot, ".png") == 0)
        return "image/png";
    else if (strcmp(dot, ".css") == 0)
        return "text/css";
    else if (strcmp(dot, ".au") == 0)
        return "audio/basic";
    else if (strcmp( dot, ".wav") == 0)
        return "audio/wav";
    else if (strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";
    else if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";
    else if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg";
    else if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml";
    else if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    else if (strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";
    else if (strcmp(dot, ".ogg") == 0)
        return "application/ogg";
    else if (strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";

    return "text/plain; charset=utf-8";
}