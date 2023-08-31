#include "total.h"
#include "url_conver.h"

#define HTTP_CLOSE "Connection: close\r\n"\


int response_http(struct bufferevent *bev, const char *method, char *path)
{
	if (strcasecmp("GET", method) == 0) {
		strdecode(path, path);	// 解码
		char *file_path = &path[1];

		if (strcmp(path, "/") == 0 || strcmp(path, "/.") == 0)
			file_path = "./";
		printf("Http Request Resource Path =  %s, File_path = %s\n", path, file_path);

		struct stat fs;	// fileStatus 存储文件或目录的状态信息
		if (stat(file_path, &fs) < 0) {
			perror("open file err: ");
			send_error(bev);
			return -1;
		}

		if (S_ISDIR(fs.st_mode)) {	// 处理目录
			// 显示目录列表
			send_header(bev, 200, "OK", get_file_type(".html"), -1);
			send_dir(bev, file_path);
	 	} else {					// 处理文件
	 		send_header(bev, 200, "Ok", get_file_type(file_path), fs.st_size);
	 		send_file_to_http(file_path, bev);
	 	}
	}
	return 0;
}

int send_file_to_http(const char *filename, struct bufferevent *bev)
{
	int fd = open(filename, O_RDONLY);
	int ret = 0;
	char buf[4096] = {0};

	while ((ret = read(fd ,buf, sizeof(buf))) )	// ()明确判断 read 函数的返回值是否大于 0
	{
		bufferevent_write(bev, buf, ret);
		memset(buf, 0, ret);
	}
	close(fd);
	return 0;
}

int send_header(struct bufferevent *bev, int no, const char *desp, const char *type, long len)
{
	char buf[256] = {0};

	//HTTP/1.1 200 OK\r\n
	sprintf(buf, "HTTP/1.1 %d %s \r\n", no, desp);
	bufferevent_write(bev, buf, strlen(buf));
	// 文件类型
	sprintf(buf, "Content-Type:%s\r\n", type);
	bufferevent_write(bev, buf, strlen(buf));
	// 文件大小
	sprintf(buf, "Content-Length:%ld\r\n", len);
	bufferevent_write(bev, buf, strlen(buf));
	// Connection: close
	bufferevent_write(bev, HTTP_CLOSE, strlen(HTTP_CLOSE));
	// send \r\n
	bufferevent_write(bev, "\r\n", 2);

	return 0;
}

int send_error(struct bufferevent *bev)
{
	send_header(bev, 404, "File Not Found", "text/html", -1);
	send_file_to_http("/WebServer/404page/404.html", bev); 	// 此处填写绝对路径，如 /opt/WebServer/404page/404.html

	return 0;
}

int send_dir(struct bufferevent *bev,const char *dirname)
{
	char encode_name[1024];
	char path[1024];
	char timestr[64];
	struct stat fs;				// fileStatus 存储文件或目录的状态信息
	struct dirent **dirinfo; 	// 存储目录中的单个目录项的信息
	int i;
	unsigned long long total_size = 0;

	char buf[4096] = {0};
	sprintf(buf, "<html><head><meta charset=\"utf-8\"><title>Index of ：%s</title></head>", dirname);
	sprintf(buf+strlen(buf), "<body><h1>Index of ：%s</h1><table>", dirname);

	// 添加目录内容
	// scandir: 读取一个目录中的所有文件和子目录的信息，并按字母顺序进行排序
	int num = scandir(dirname, &dirinfo, NULL, alphasort);	// 返回数组的大小, 即目录项的数量
	for (i = 0; i < num; i++)
	{
		// 编码
		strencode(encode_name, sizeof(encode_name), dirinfo[i]->d_name);
	
		sprintf(path, "%s%s", dirname, dirinfo[i]->d_name);	// 目录的路径+目录中的文件名
		printf("Path = %s\n", path);

		if (lstat(path, &fs) < 0) {	// 获取文件或符号链接的元数据
			sprintf(buf + strlen(buf),
				"<tr><td><a href=\"%s\">%s</a></td></tr>\n",
				encode_name, dirinfo[i]->d_name);
		} else {
			strftime(timestr, sizeof(timestr),
				"  %d  %b   %Y  %H:%M", localtime(&fs.st_mtime));
			if (S_ISDIR(fs.st_mode)) {
				calculate_folder_size(dirname, &total_size);
				char size_str[20];
				double size_mb = (double)total_size / (1024 * 1024);
				sprintf(size_str, "%.2f MB", size_mb); 
			    sprintf(buf+strlen(buf), 
			            "<tr><td><a href=\"%s/\">%s/</a></td><td>%s</td><td>%s</td></tr>\n",
			            encode_name, dirinfo[i]->d_name, timestr, size_str);
			} else {
			    char size_str[20];
			    double size_mb = (double)fs.st_size / (1024 * 1024); 
			    sprintf(size_str, "%.2f MB", size_mb); 
			    sprintf(buf+strlen(buf), 
			            "<tr><td><a href=\"%s\">%s</a></td><td>%s</td><td>%s</td></tr>\n", 
			            encode_name, dirinfo[i]->d_name, timestr, size_str);
			}
		}
		
		bufferevent_write(bev, buf, strlen(buf));
		memset(buf, 0, sizeof(buf));
	}
	sprintf(buf+strlen(buf), "</table></body></html>");
	bufferevent_write(bev, buf, strlen(buf));

	printf("Dir Read OK \n");

	return 0;
}

void conn_readcd(struct bufferevent *bev, void *user_data)
{
	printf("Begin call %s.........\n",__FUNCTION__);

	char buf[4096] = {0};
	char method[50], path[4096], protocol[32];

	bufferevent_read(bev, buf, sizeof(buf));	// 读取并存储在buf
	printf("\n%s\n", buf);
	sscanf(buf, "%[^ ] %[^ ] %[^ \r\n]", method, path, protocol);
	printf("Method[%s]  Path[%s]  Protocol[%s]\n", method, path, protocol);

	if (strcasecmp(method, "GET") == 0) {	// 如果method为GET进行响应
		response_http(bev, method, path);
	}

	printf("End call %s.........\n", __FUNCTION__);
}

void conn_eventcb(struct bufferevent *bev, short events, void *user_data)
{
	printf("Begin call %s.........\n", __FUNCTION__);

	if (events & BEV_EVENT_EOF) {
		printf("Connection closed.\n");
	} else if (events & BEV_EVENT_ERROR) {
		fprintf(stderr, "Got an error on the connection: %s\n", strerror(errno));
	}

	bufferevent_free(bev);

	printf("End call %s.........\n", __FUNCTION__);
}


// 捕获中断信号并进行优雅的退出处理
void signal_cb(evutil_socket_t sig, short events, void *user_data)
{
	struct event_base *base = user_data;
	struct timeval delay = {1, 0};

	printf("Caught an interrupt signal; exiting cleanly in one seconds.\n");
	event_base_loopexit(base, &delay);	// 设置 event base（事件循环）在指定的时间后退出
}

void listener_cb(struct evconnlistener *listener,
				evutil_socket_t fd, struct sockaddr *sa, int socklen, void *user_data)
{
	printf("Begin call-------%s\n",__FUNCTION__);
	printf("fd is %d\n", fd);

	struct event_base *base = user_data;
	struct bufferevent *bev;

	bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE );
	if (!bev)
	{
		fprintf(stderr, "Error constructing bufferevnent!");
		event_base_loopbreak(base);
		return ;
	}

	// 配置回调函数和事件监听
	bufferevent_flush(bev, EV_READ | EV_WRITE, BEV_NORMAL);
	bufferevent_setcb(bev, conn_readcd, NULL, conn_eventcb, NULL);
	bufferevent_enable(bev, EV_READ | EV_WRITE);

	printf("End call-------%s\n",__FUNCTION__);
}

