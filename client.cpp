#include <cstdio>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdlib>
#include <strings.h>

int main(int argc, char **argv)
{
	int fd, n;
	char recvline[255];
	char line[255];
	struct sockaddr_in servaddr;
	fd = socket(AF_INET, SOCK_STREAM, 0);
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(80);
	inet_pton(AF_INET, argv[1], &servaddr.sin_addr);
	connect(fd, (struct sockaddr*) &servaddr, sizeof(servaddr));
	n = snprintf(line, sizeof(line), "GET / HTTP/1.0\r\n\r\n");
	write(fd, line, n);
	while((n = read(fd, recvline, 1024)) > 0)
	{
		recvline[n] = 0;
		fputs(recvline, stdout);
	}
	exit(0);
}


