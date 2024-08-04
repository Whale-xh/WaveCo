
/*
 __          __              _____      
 \ \        / /             / ____|     
  \ \  /\  / /_ ___   _____| |     ___  
   \ \/  \/ / _` \ \ / / _ \ |    / _ \ 
    \  /\  / (_| |\ V /  __/ |___| (_) |
     \/  \/ \__,_| \_/ \___|\_____\___/ 

---------------------------------------------------------------------------------------
   Author : Whale-xh , email : 1870211055@qq.com
  
   Copyright Statement:
   --------------------
   This software is protected by Copyright and the information contained
   herein is confidential. The software may not be copied and the information
   contained herein may not be used or disclosed except with the written
   permission of Author. (C) 2024                                                                             


*
*/



#include "../include/wave_coroutine.h"
#include <arpa/inet.h>

#define MAX_LOCAL_PORT_NUM 100
#define MAX_CLIENT_NUM 1000000
#define TIME_SUB_MS(tv1, tv2) ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)

void server_reader_sender(void *arg)
{
	int fd = *(int *)arg;
	int ret = 0;

	struct pollfd fds;
	fds.fd = fd;
	fds.events = POLLIN;

	while (1)
	{

		char buf[1024] = {0};
		ret = wave_recv(fd, buf, 1024, 0);
		if (ret > 0)
		{
			if (fd > MAX_CLIENT_NUM)
				printf("read from server: %.*s\n", ret, buf);

			ret = wave_send(fd, buf, strlen(buf), 0);
			if (ret == -1)
			{
				wave_close(fd);
				break;
			}
		}
		else if (ret == 0)
		{
			wave_close(fd);
			break;
		}
	}
}

void server(void *arg)
{

	unsigned short port = *(unsigned short *)arg;
	free(arg);

	int fd = wave_socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return;

	struct sockaddr_in local, remote;
	local.sin_family = AF_INET;
	local.sin_port = htons(port);
	local.sin_addr.s_addr = INADDR_ANY;
	bind(fd, (struct sockaddr *)&local, sizeof(struct sockaddr_in));

	listen(fd, 20);
	printf("listen port : %d\n", port);

	struct timeval tv_begin;
	gettimeofday(&tv_begin, NULL);

	while (1)
	{
		socklen_t len = sizeof(struct sockaddr_in);
		int cli_fd = wave_accept(fd, (struct sockaddr *)&remote, &len);
		if (cli_fd % 1000 == 999)
		{

			struct timeval tv_cur;
			memcpy(&tv_cur, &tv_begin, sizeof(struct timeval));

			gettimeofday(&tv_begin, NULL);
			int time_used = TIME_SUB_MS(tv_begin, tv_cur);

			printf("client fd : %d, time_used: %d s\n", cli_fd, time_used / 1000);
		}

		wave_coroutine *read_co;
		wave_coroutine_create(&read_co, server_reader_sender, &cli_fd);
	}
}

int main(int argc, char *argv[])
{
	wave_coroutine *co = NULL;

	unsigned short base_port = 8888;
	for (int i = 0; i < MAX_LOCAL_PORT_NUM; ++i)
	{
		unsigned short *port = calloc(1, sizeof(unsigned short));
		*port = base_port + i;
		wave_coroutine_create(&co, server, port); // no run
	}

	wave_schedule_run(); // run

	return 0;
}
