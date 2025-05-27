#include"Server.h"
#include<arpa/inet.h>
#include<sys/epoll.h>
#include<stdio.h>
#include<fcntl.h>
#include<errno.h>
#include<string.h>
#include<strings.h>
#include<sys/stat.h>
#include<assert.h>
#include<sys/sendfile.h>
#include<dirent.h>
#include <stdlib.h>  // 部分系统需要此头文件
#include<unistd.h>
#include<pthread.h>
#include<ctype.h>
int initListenFd(unsigned short port)
{
	//1.创建监听的fd
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd == -1)
	{
		perror("socket");
		return -1;
	}
	//2.设置端口复用
	int opt = 1;
	int ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	if (ret == -1)
	{
		perror("setsockopt");
		return -1;
	}
	//3.绑定
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
	ret = bind(lfd, (struct sockaddr*)&addr, sizeof addr);
	if (ret == -1)
	{
		perror("bind");
		return -1;
	}
	//4.设置监听
	ret = listen(lfd, 128);
	if (ret == -1)
	{
		perror("listen");
		return -1;
	}
	//返回fd
	return lfd;
}

int epollRun(int lfd)
{
	//1.创建epoll实例
	int epfd = epoll_create(1);
	//2.lfd上树
	struct epoll_event ev;
	ev.data.fd = lfd;
	ev.events = EPOLLIN;
	int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
	if (ret == -1)
	{
		perror("epoll_ctl");
		return -1;
	}
	//3.检测
	struct epoll_event evs[1024];
	int size = sizeof(evs) / sizeof(struct epoll_event);
	while (1)
	{
		int num = epoll_wait(epfd, evs, size, -1);
		for (int i = 0;i < num;++i)
		{
			int fd = evs[i].data.fd;
			if (fd == lfd)
			{
				//建立新连接 accept
				acceptClient(lfd, epfd);
			}
			else
			{
				//主要是接受对端数据
				recvHttpRequest(fd, epfd);
			}
		}

	}
	return 0;
}

int acceptClient(int lfd, int epfd)
{
	//1.建立连接
	int cfd = accept(lfd, NULL, NULL);
	if (cfd == -1)
	{
		perror("accept");
		return -1;
	}
	//2.设置非阻塞
	int flag = fcntl(cfd, F_GETFL);
	flag |= O_NONBLOCK;
	fcntl(cfd, F_SETFL, flag);
	//3.cfd添加到epoll中
	struct epoll_event ev;
	ev.data.fd = cfd;
	ev.events = EPOLLIN | EPOLLET;
	int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
	if (ret == -1)
	{
		perror("epoll_ctl");
		return -1;
	}
	return 0;
}

int recvHttpRequest(int cfd, int epfd)
{
	printf("begin...\n");
	int len = 0, total = 0;
	char tmp[1024] = { 0 };
	char buf[4096] = { 0 };
	while ((len = recv(cfd, tmp, sizeof tmp, 0)) > 0)
	{
		if (total + len < sizeof buf)
		{
			memcpy(buf + total, tmp, len);
		}
		total += len;
	}
	//判断数据是否被接受完毕
	if (len == -1 && errno == EAGAIN)
	{
		//解析请求行
		char* pt = strstr(buf, "\r\n");
		int reqLen = pt - buf;
		buf[reqLen] = '\0';
		parseRequestLine(buf, cfd);
	}
	else if (len == 0)
	{
		//客户端断开了连接
		epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
		close(cfd);
	}
	else
	{
		perror("recv");
	}
	return 0;
}

int parseRequestLine(const char* line, int cfd)
{
	//解析请求行
	char method[12];
	char path[1024];
	sscanf(line, "%[^ ] %[^ ]", method, path);
	printf("method: %s, path: %s\n", method, path);
	if (strcasecmp(method, "get") != 0)
	{
		return -1;
	}
	decodeMsg(path, path);
	//处理客户端请求的静态资源(目录或者文件)
	char* file = NULL;
	if (strcmp(path, "/") == 0)
	{
		file = "./";
	}
	else
	{
		file = path + 1;
	}
	//获取文件属性
	struct stat st;
	int ret = stat(file, &st);
	if (ret == -1)
	{
		//文件不存在  --回复404
		senHeadMsg(cfd, 404, "Not Found", getFileType(".html"), -1);
		sendFile("404.html", cfd);
		return 0;
	}
	//判断文件类型
	if (S_ISDIR(st.st_mode))
	{
		//把这个目录中的内容发送给客户端
		senHeadMsg(cfd, 200, "OK", getFileType(".html"), -1);
		sendDir(file, cfd);
	}
	else
	{
		//把文件的内容发送给客户端
		senHeadMsg(cfd, 200, "OK", getFileType(file), st.st_size);
		sendFile(file, cfd);
		if (ret == -1) {
			printf("Failed to send file %s\n", file);
			// 可选：发送错误响应给客户端
			senHeadMsg(cfd, 500, "Internal Server Error", "text/html", -1);
			sendFile("500.html", cfd);
			return -1;
		}
	}
	return 0;
}

int sendFile(const char* fileName, int cfd)
{
	//1.打开文件
	int fd = open(fileName, O_RDONLY);
	if (fd == -1) {
		perror("open failed");
		return -1; // 返回错误，不崩溃
	}
	//assert(fd > 0);
	printf("sendFile: file opened successfully, fd = %d\n", fd);

#if 0
	while (1)
	{
		char buf[1024];
		int len = read(fd, buf, sizeof buf);
		if (len > 0)
		{
			send(cfd, buf, len, 0);
			usleep(10);
		}
		else if (len == 0)
		{
			break;
		}
		else
		{
			perror("read file");
		}
	}
#else
	//// 2. 获取文件大小
	//struct stat st;
	//fstat(fd, &st);  // 直接通过fd获取文件属性，避免重复stat
	//off_t size = st.st_size;

	//// 3. 使用sendfile一次性发送（内核自动分块，但客户端感知为整体）
	//off_t offset = 0;
	//sendfile(cfd, fd, &offset, size);  // 无需循环，sendfile会发送全部数据
	off_t offset = 0;
	int size = lseek(fd, 0, SEEK_END);
	if (size == -1) {
		perror("lseek failed");
		close(fd);
		return -1;
	}
	//printf("sendFile: file size = %d\n", size);
	lseek(fd, 0, SEEK_SET);
	while (offset < size) {
		sendfile(cfd, fd, &offset, size - offset);
		//if (ret == -1 ) {
		//	//printf("no data...\n");
		//	perror("sendfile failed");
		//	//close(fd);
		//	//return -1; // 错误处理，避免崩溃
		//}
		//printf("sendFile: sent %zd bytes, offset now %ld\n", ret, offset);
		//sendfile(cfd, fd, &offset, size-offset);
	}
	//sendfile(cfd, fd, &offset, size);
#endif
	close(fd);
	printf("sendFile: done with %s\n", fileName);
	return 0;
}

int senHeadMsg(int cfd, int status, const char* descr, const char* type, int length)
{
	//状态行
	char buf[4096] = { 0 };
	sprintf(buf, "http/1.1 %d %s\r\n", status, descr);
	//响应头
	sprintf(buf + strlen(buf), "content-type: %s\r\n", type);
	sprintf(buf + strlen(buf), "content-length: %lld\r\n\r\n", (long long)length);
	//sprintf(buf + strlen(buf), "\r\n");  // 添加关键分隔符
	send(cfd, buf, strlen(buf), 0);
	return 0;
}

const char* getFileType(const char* name)
{
	const char* dot = strchr(name, '.');
	if (dot == NULL)
		return "text/plain; charset = utf-8";
	if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
		return "text/html; charset=utf-8";
	if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
		return "image/jpeg";
	if (strcmp(dot, ".gif") == 0)
		return "image/gif";
	if (strcmp(dot, ".png") == 0)
		return "image/png";
	if (strcmp(dot, ".css") == 0)
		return "text/css";
	if (strcmp(dot, ".au") == 0)
		return "audio/basic";
	if (strcmp(dot, ".wav") == 0)
		return "audio/wav";
	if (strcmp(dot, ".avi") == 0)
		return "video/x-msvideo";
	if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
		return "video/quicktime";
	if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
		return "video/mpeg";
	if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
		return "model/vrml";
	if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
		return "audio/midi";
	if (strcmp(dot, ".mp3") == 0)
		return "audio/mpeg";
	if (strcmp(dot, ".ogg") == 0)
		return "application/ogg";
	if (strcmp(dot, ".pac") == 0)
		return "application/x-ns-proxy-autoconfig";
	return "text/plain; charset=utf-8";
}

// 自定义alphasort函数（等效于POSIX标准实现）
int my_alphasort(const struct dirent** a, const struct dirent** b) {
	return strcmp((*a)->d_name, (*b)->d_name);
}

int sendDir(const char* dirName, int cfd)
{
	char buf[4096] = { 0 };
	sprintf(buf, "<html><head><title>%s</title></head><body><table>", dirName);
	struct dirent** namelist;
	int num = scandir(dirName, &namelist, NULL, my_alphasort);
	for (int i = 0;i < num;++i) {
		//取出文件名 namelist 指向的是一个指针数组 struct dirent* tmp[]
		char* name = namelist[i]->d_name;
		struct stat st;
		char subPath[1024] = { 0 };
		sprintf(subPath, "%s/%s", dirName, name);
		stat(subPath, &st);
		if (S_ISDIR(st.st_mode)) {
			//a标签 <a href="">name</a>
			sprintf(buf + strlen(buf),
				"<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>",
				name, name, st.st_size);
		}
		else
		{
			sprintf(buf + strlen(buf),
				"<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
				name, name, st.st_size);
		}
		send(cfd, buf, strlen(buf), 0);
		memset(buf, 0, sizeof(buf));
		free(namelist[i]);
	}
	sprintf(buf, "</table></body></html>");
	send(cfd, buf, strlen(buf), 0);
	free(namelist);
	return 0;
}

int hexToDec(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return 0;
}

void decodeMsg(char* to, char* from) {
	for (; *from != '\0'; ++to, ++from) {
		if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) {
			// 正确解码：高位×16 + 低位
			int high = hexToDec(from[1]);  // 第一个字符转16进制值（如'E'→14）
			int low = hexToDec(from[2]);   // 第二个字符转16进制值（如'8'→8）
			*to = (high << 4) | low;       // 14×16 + 8 = 232（即0xE8）
			from += 2;  // 跳过已处理的两个字符
		}
		else {
			*to = *from;  // 非%XX字符直接复制（如'/'、字母等）
		}
	}
	*to = '\0';  // 手动添加字符串结尾
}
//void decodeMsg(char* to, char* from)
//{
//	for (; *from != '\0';++to, ++from)
//	{
//		if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2]))
//		{
//			*to = hexToDec(from[1]) * 16 + hexToDec(from[2]);
//			from += 2;
//		}
//		else
//		{
//			*to = *from;
//		}
//	}
//}