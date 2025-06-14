#pragma once
//初始化用于监听的套接字
int initListenFd(unsigned short port);
//启动epoll
int epollRun(int lfd);
//和客户端建立连接
//int acceptClient(int lfd,int epfd);
int acceptClient(int lfd, int epfd);
//接收http请求
//int recvHttpRequest(int cfd, int epfd);
int recvHttpRequest(int cfd, int epfd);
//解析请求行
int parseRequestLine(const char* line, int cfd);
//发送文件
int sendFile(const char* fileName, int cfd);
//发送响应头（状态行和响应头）
int senHeadMsg(int cfd, int status, const char* descr, const char* type, int length);
//得到文件类型
const char* getFileType(const char* name);
//发送目录
int sendDir(const char* dirName, int cfd);
int hexToDec(char c);
void decodeMsg(char* to, char* from);
// 在Server.h中添加声明
//int continueSending(struct FdInfo* info);