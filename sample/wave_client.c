
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


#include "wave_coroutine.h"
#include <arpa/inet.h>

#define WAVE_SERVER_IPADDR  "127.0.0.1"
#define WAVE_SERVER_PORT    8888

int init_client(void) {

	int clientfd = wave_socket(AF_INET, SOCK_STREAM, 0);
	if (clientfd <= 0) {
		perror("socket");
		return -1;
	}

	struct sockaddr_in serveraddr = {0};
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(WAVE_SERVER_PORT);
	serveraddr.sin_addr.s_addr = inet_addr(WAVE_SERVER_IPADDR);

	int result = wave_connect(clientfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
	if (result != 0) {
		perror("connect");
		return -2;
	}

	return clientfd;
	
}

void client(void *arg) {

	int clientfd = init_client();
	char *buffer = "WaveCo_client\r\n";

	while (1) {

		int length = wave_send(clientfd, buffer, strlen(buffer), 0);
		printf("echo length : %d\n", length);

		sleep(1);
	}

}


int main(int argc, char *argv[]) {
	wave_coroutine *co = NULL;

	wave_coroutine_create(&co, client, NULL);
	
	wave_schedule_run(); //run

	return 0;
}