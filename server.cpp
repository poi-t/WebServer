#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>
#include <pthread.h>
#include <ctype.h>
#include <string.h>

#define NTHREADS 4
#define MAXNCLI 8
#define MAXLISTEN 4
pthread_t thread[NTHREADS];
int clifd[MAXNCLI];
int iget, iput;
pthread_mutex_t clifd_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t clifd_cond = PTHREAD_COND_INITIALIZER;   

void thread_make(int i);
void* thread_main(void*);
void web_server(int sockfd);
void unimplemented(int sockfd);
void err_file(int sockfd);
void send_file(int sockfd, const char *path);
void execute_cgi(int sockfd, const char *path, const char *parameter);

void thread_make(int i)
{
	pthread_create(&thread[i], NULL, &thread_main, NULL);
	return;
}

void* thread_main(void* t)
{
	int connfd;
	while(1)
	{
		pthread_mutex_lock(&clifd_mutex);
		
		while(iget == iput)
		{
			pthread_cond_wait(&clifd_cond, &clifd_mutex);
		}
		connfd = clifd[iget];
		if(++iget == MAXNCLI)
		{
			iget = 0;
		}
		
		pthread_mutex_unlock(&clifd_mutex);
		web_server(connfd);
		close(connfd);
	}
}

void web_server(int sockfd)
{
	char buf[1024];
	char method[32];
	char url[512];
	char path[512];
	int i = 0, j = 0, num = 0;
	num = read(sockfd, buf, sizeof(buf));
	while (!isspace(buf[i]) && (i < sizeof(method) - 1))
	{
		method[i] = buf[i];
		++i;
	}
	method[i] = '\0';
	j = i;
    
	if (strcmp(method, "GET"))
	{
		unimplemented(sockfd);
		return;
	}

	while (isspace(buf[j]) && (j < num))
	{
		++j;
	}
	
	i = 0;
	while (!isspace(buf[j]) && (i < sizeof(url) - 1) && (j < num))
	{
		url[i++] = buf[j++];
	}
 	url[i] = '\0';
	
	char *parameter = url;
	bool cgi = false;
	while((*parameter != '?') && (*parameter != '\0'))
	{
		++parameter;
	}
	if(*parameter == '?')
	{
		cgi = true;
		*parameter = '\0';
		++parameter;
	}
	
	sprintf(path, "myhttp%s", url);
	if (path[strlen(path) - 1] == '/')
	{
		strcat(path, "index.html");
	}

	struct stat st;
	if (stat(path, &st) == -1) 
	{
		printf("客户端请求文件失败：%s\n", path);
		err_file(sockfd);
	}
	else
	{
		if ((st.st_mode & S_IFMT) == S_IFDIR)//目录 
		{
			strcat(path, "/index.html");
		}
		
		if ((st.st_mode & S_IXUSR) ||  (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
		{
			cgi = true;
		}
		
		if(cgi) execute_cgi(sockfd, path, parameter);
		else send_file(sockfd, path);
	}
}

void unimplemented(int sockfd)
{
	char buf[1024];
	
	sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
	send(sockfd, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
	send(sockfd, buf, strlen(buf), 0);
	sprintf(buf, "Connection: close\r\n\r\n");
	send(sockfd, buf, strlen(buf), 0);
	sprintf(buf, "<html>\r\n");
	send(sockfd, buf, strlen(buf), 0);
	sprintf(buf, "<head><title>501 Method Not Implemented</title></head>\r\n");
	send(sockfd, buf, strlen(buf), 0);
	sprintf(buf, "<body bgcolor=\"white\">\r\n");
	send(sockfd, buf, strlen(buf), 0);
	sprintf(buf, "<div style=\"text-align:center;\">501 Method Not Implemented</div>\r\n");
	send(sockfd, buf, strlen(buf), 0);
	sprintf(buf, "</body>\r\n");
	send(sockfd, buf, strlen(buf), 0);
	sprintf(buf, "<html>\r\n");
	send(sockfd, buf, strlen(buf), 0);
}

void err_file(int sockfd)
{
	char buf[1024];
	
	sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
	send(sockfd, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
	send(sockfd, buf, strlen(buf), 0);
	sprintf(buf, "Connection: close\r\n\r\n");
	send(sockfd, buf, strlen(buf), 0);
	sprintf(buf, "<html>\r\n");
	send(sockfd, buf, strlen(buf), 0);
	sprintf(buf, "<head><title>404 Not Found</title></head>\r\n");
	send(sockfd, buf, strlen(buf), 0);
	sprintf(buf, "<body bgcolor=\"white\">\r\n");
	send(sockfd, buf, strlen(buf), 0);
	sprintf(buf, "<div style=\"text-align:center;\">404 Not Found</div>\r\n");
	send(sockfd, buf, strlen(buf), 0);
	sprintf(buf, "</body>\r\n");
	send(sockfd, buf, strlen(buf), 0);
	sprintf(buf, "<html>\r\n");
	send(sockfd, buf, strlen(buf), 0);
}

void execute_cgi(int sockfd, const char *path, const char *parameter)
{
	return;
}

void send_file(int sockfd, const char *path)
{
	FILE *file =  fopen(path, "r");
	if (file == NULL)
		err_file(sockfd);
	else
	{
		char buf[1024];
		sprintf(buf, "HTTP/1.0 200 OK\r\n");
		send(sockfd, buf, strlen(buf), 0);
		sprintf(buf, "Content-Type: text/html\r\n");
		send(sockfd, buf, strlen(buf), 0);
		sprintf(buf, "Connection: close\r\n\r\n");
		send(sockfd, buf, strlen(buf), 0);
		
    	fgets(buf, sizeof(buf), file);
    	while (!feof(file))
    	{
        	send(sockfd, buf, strlen(buf), 0);
        	fgets(buf, sizeof(buf), file);
		}
	}
	fclose(file);
}

int main(void)
{
	/*实际使用时取消下一行的注释，使其成为守护进程*/
	/*daemon(1,0); */
	int listenfd, connfd;
	struct sockaddr_in cliaddr;
	socklen_t clilen = sizeof(cliaddr);
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	bzero(&cliaddr, sizeof(cliaddr));
	cliaddr.sin_family = AF_INET;
	cliaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	cliaddr.sin_port = htons(80);
	bind(listenfd, (struct sockaddr*) &cliaddr, clilen);
	listen(listenfd, MAXLISTEN);
	
	iget = iput = 0;
	
	for(int i = 0; i < NTHREADS; ++i)
	{
		thread_make(i);
	}
	while(1)
	{
		connfd = accept(listenfd, (struct sockaddr*) &cliaddr, &clilen);
		pthread_mutex_lock(&clifd_mutex);
		
		clifd[iput] = connfd;
		if(++iput == MAXNCLI)
		{
			iput = 0;
		}
		while(iput == iget)
		{
			pthread_cond_signal(&clifd_cond);
			pthread_mutex_unlock(&clifd_mutex);
			sleep(1);
			pthread_mutex_lock(&clifd_mutex);
		}
		
		pthread_cond_signal(&clifd_cond);
		pthread_mutex_unlock(&clifd_mutex);
	}
	close(listenfd);
	return 0;
}

