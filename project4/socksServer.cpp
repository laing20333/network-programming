#include<iostream>
//#include<sys/type.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<netdb.h>
#include<sys/types.h>
#include<unistd.h>
#include<sys/wait.h>
#include<sys/stat.h>
#include<sys/ioctl.h>
#include<signal.h>
#include<fcntl.h>
#include<cstring>
#include<cstdlib>
#include<stdlib.h>
#include<cstdio>
#include<sstream>
#include<algorithm>
#include<vector>
#include<map>

using namespace std;

#define MAX_BUF_SIZE 999991
#define CONNECT 1
#define BIND 2

int ssockfd;
struct sockaddr_in server_addr, client_addr;
void proxy();
bool firewall(string dst_ip);

int main(int argc, char **argv){
	int sockfd, port;
	sscanf(argv[1], "%d", &port);

	// socket
	if( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		cerr << "Server: can't open stream socket" << endl;
		exit(0);
	}

	// ignore TCP TIME_WAIT state
    int on = 1;
    if((setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)
       perror("Setsockopt error");


	bzero((char *)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	// bind
	if(bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
		cerr << "Server: can't bind local address" << endl;
		exit(0);
	}

	// listen
	listen(sockfd, 10);

	while(1){
		// accept
		socklen_t szclient = sizeof(client_addr);
		ssockfd = accept(sockfd, (struct sockaddr *)&client_addr, &szclient);
		if(ssockfd < 0){
			cerr << "Server: accept error" << endl;
			exit(1);
		}
		printf("Accept connection from %s:%d\n",inet_ntoa(client_addr.sin_addr), (int)ntohs(client_addr.sin_port));

		pid_t pid = fork();
		if(pid == 0){
			// run proxy 
			close(sockfd);
			proxy();
			//cout << "close a connection" << endl;
			exit(0);
		}else if(pid > 0){
			// accept next connection
			close(ssockfd);
		}else {
			cerr << "fork error" << endl;
			exit(1);
		}
	}
	
	close(sockfd);
	return 0;
}

void proxy(){
	// receive request
	unsigned char buffer[MAX_BUF_SIZE] = {};
	read(ssockfd, buffer, sizeof(buffer));

	unsigned char VN = buffer[0] ;
	unsigned char CD = buffer[1] ;
	char dst_port[11];
	char dst_ip[101];

	int tmp = (int)buffer[2] * 256 + (int)buffer[3];
	sprintf(dst_port, "%d", tmp);
	sprintf(dst_ip, "%d.%d.%d.%d", (int)buffer[4], (int)buffer[5], (int)buffer[6], (int)buffer[7]);
	unsigned char* USER_ID = buffer + 8;
	
	cout << "VN: " << (int)VN << " CD: " << (int)CD << " DST_PORT: " << dst_port << " DST_IP: " << dst_ip << " USER_ID: " << USER_ID << endl;
	
	// check firewall
	if(firewall(dst_ip)){
		printf("Permit Src = %s(%d), Dst = %s(%s)\n", inet_ntoa(client_addr.sin_addr), (int)ntohs(client_addr.sin_port), dst_ip, dst_port);
		if((int)buffer[1] == CONNECT){
			cout << "SOCKS_CONNECT GRANTED ...." << endl;
		}else if((int)buffer[1] == BIND){
			cout << "SOCKS_BIND GRANTED ...." << endl;
		}


		int rsockfd;
		if((int)buffer[1] == CONNECT){

			// connect to httpserver
			if( (rsockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
				cerr << "Server: can't open stream socket" << endl;
				exit(0);
			}

			// ignore TCP TIME_WAIT state
			int on = 1;

			if((setsockopt(rsockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0){
				perror("Setsockopt error");
			}

			struct addrinfo hints, *server;
			memset(&hints, 0, sizeof(hints));
			getaddrinfo(dst_ip, dst_port, &hints, &server);

			bool isconnected = true;
			if ( connect(rsockfd, server->ai_addr, server->ai_addrlen) < 0 ){
				isconnected = false;
				unsigned char reply[8];
				reply[0] = 0;
				reply[1] = (unsigned char)91;
				reply[2] = 0;
				reply[3] = 0;
				reply[4] = 0;
				reply[5] = 0;
				reply[6] = 0;
				reply[7] = 0;
				write(ssockfd, reply, 8);
				close(rsockfd);
				close(ssockfd);
				exit(0);
			}

			// send reply to browser
			unsigned char reply[8];
			reply[0] = 0;
			reply[1] = isconnected ? (unsigned char)90 : (unsigned char)91;
			reply[2] = buffer[2];
			reply[3] = buffer[3];
			reply[4] = buffer[4];
			reply[5] = buffer[5];
			reply[6] = buffer[6];
			reply[7] = buffer[7];

			write(ssockfd, reply, 8);
			
			if(!isconnected){
				close(rsockfd);
				close(ssockfd);
				exit(0);
			}
			
			//cout << "reply" << endl;

		}else if(int(buffer[1]) == BIND){
			int psockfd;
			if( (psockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
				cerr << "Server: can't open stream socket" << endl;
				exit(0);
			}

			// ignore TCP TIME_WAIT state
			int on = 1;
			if((setsockopt(psockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0){
				perror("Setsockopt error");
			}


			struct sockaddr_in client_addr;
			int port = 9970;
			bzero((char *)&client_addr, sizeof(client_addr));
			client_addr.sin_family = AF_INET;
			client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
			client_addr.sin_port = htons(port);
			
			bool isbind = true;
			// bind
			if(bind(psockfd, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0){
				cerr << "Server: can't bind local address" << endl;
				isbind = false;
			}

			// listen
			listen(psockfd, 10);

			// send reply to browser
			unsigned char reply[8];
			reply[0] = 0;
			reply[1] = isbind ? (unsigned char)90 : (unsigned char)91;
			reply[2] = port / 256;
			reply[3] = port % 256;
			reply[4] = 0;
			reply[5] = 0;
			reply[6] = 0;
			reply[7] = 0;

			write(ssockfd, reply, 8);

			if(!isbind) {
				close(psockfd);
				close(ssockfd);
				exit(0);
			}

			// accept ftp server connection
			socklen_t szclient = sizeof(client_addr);
			rsockfd = accept(psockfd, (struct sockaddr *)&client_addr, &szclient);

			// send reply again to browser
			write(ssockfd, reply, 8);

			close(psockfd);
		}
		
		// listen browser request, httpserver response and forward it
		int nfds = 1024;
		fd_set rfds, afds;

		FD_ZERO(&afds);
		FD_SET(ssockfd, &afds);
		FD_SET(rsockfd, &afds);
		memset(buffer, 0, sizeof(buffer));
		while(1){
			memcpy(&rfds, &afds, sizeof(rfds));
			if ( select(nfds, &rfds, (fd_set *)0, (fd_set*)0, 0) < 0 ){
				exit(1);
			}

			if(FD_ISSET(ssockfd, &rfds)){
				int len = read(ssockfd, buffer, sizeof(buffer));
				//cout << buffer << endl;
				if(len <= 0) break;
					write(rsockfd, buffer, len);
			}
			if(FD_ISSET(rsockfd, &rfds)){
				int len = read(rsockfd, buffer, sizeof(buffer));
				if(len <= 0) break;
				//cout << buffer << endl;
				write(ssockfd, buffer, len);
			}
		}
		close(rsockfd);
	
	}else {
		printf("Deny Src = %s(%d), Dst = %s(%s)\n", inet_ntoa(client_addr.sin_addr), (int)ntohs(client_addr.sin_port), dst_ip, dst_port);
		if((int)buffer[1] == CONNECT){
			cout << "SOCKS_CONNECT REJECTED ...." << endl;
		}else if((int)buffer[1] == BIND){
			cout << "SOCKS_BIND REJECTED ...." << endl;
		}
		close(ssockfd);

		// send reply to browser
		unsigned char reply[8];
		reply[0] = 0;
		reply[1] = (unsigned char)91;
		reply[2] = 0;
		reply[3] = 0;
		reply[4] = 0;
		reply[5] = 0;
		reply[6] = 0;
		reply[7] = 0;
		write(ssockfd, reply, 8);
	}

	close(ssockfd);
	return;
}

bool firewall(string dst_ip){
	FILE *fin = fopen("socks.conf", "r");
	char buf[MAX_BUF_SIZE];
	fread (buf, 1, sizeof(buf), fin);
	
	char operation[11];
	char ip[101];
	sscanf(buf, "%s %s", operation, ip);
	
	char addr[4][11];
    char dst[4][11];
    sscanf(ip, "%[^.].%[^.].%[^.].%[^.]", addr[0], addr[1], addr[2], addr[3]);
    sscanf(dst_ip.c_str(), "%[^.].%[^.].%[^.].%[^.]", dst[0], dst[1], dst[2], dst[3]);

	if(!strcmp(operation, "permit")){
		for(int i=0; i<4; i++){
			if(strcmp(addr[i], "*") && strcmp(addr[i], dst[i])){
				return false;
			}
		}
		return true;

	}else if(!strcmp(operation, "deny")){
		for(int i=0; i<4; i++){
            if(strcmp(addr[i], "*") && strcmp(addr[i], dst[i])){
                return true;
            }
        }
		return false;
	}
	
}
