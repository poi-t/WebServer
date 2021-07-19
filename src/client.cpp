#include <cstdio>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdlib>
#include <strings.h>
#include <fcntl.h>

int setnoblocking(int fd) {
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
    return old_flag;
}

int main(int argc, char **argv)
{
	int fd, n1, n2;
	char recvline[1024];
	char line[1024];
	struct sockaddr_in servaddr;
	fd = socket(AF_INET, SOCK_STREAM, 0);
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(80);
	inet_pton(AF_INET, argv[1], &servaddr.sin_addr);
	connect(fd, (struct sockaddr*) &servaddr, sizeof(servaddr));
	setnoblocking(fd);
	n1 = snprintf(line, sizeof(line), "GET /  HTTP/1.0\r\n");
	n2 = snprintf(line + n1, sizeof(recvline) - n1, "Connection: keep-alive\r\n\r\n");
	while(1) {
		int t = rand() % 80;
		sleep(t);
		printf("send 1 : %ds\n", t);
		write(fd, line, n1 + n2);
		sleep(1);
		while(recv(fd, recvline, 1024, 0) > 0) {
			fputs(recvline, stdout);
		}
	}
	close(fd);
	exit(0);
}

