#pragma once
//��ʼ�����ڼ������׽���
int initListenFd(unsigned short port);
//����epoll
int epollRun(int lfd);
//�Ϳͻ��˽�������
//int acceptClient(int lfd,int epfd);
int acceptClient(int lfd, int epfd);
//����http����
//int recvHttpRequest(int cfd, int epfd);
int recvHttpRequest(int cfd, int epfd);
//����������
int parseRequestLine(const char* line, int cfd);
//�����ļ�
int sendFile(const char* fileName, int cfd);
//������Ӧͷ��״̬�к���Ӧͷ��
int senHeadMsg(int cfd, int status, const char* descr, const char* type, int length);
//�õ��ļ�����
const char* getFileType(const char* name);
//����Ŀ¼
int sendDir(const char* dirName, int cfd);
int hexToDec(char c);
void decodeMsg(char* to, char* from);
// ��Server.h���������
//int continueSending(struct FdInfo* info);