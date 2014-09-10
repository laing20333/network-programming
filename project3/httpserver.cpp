#include<iostream>
//#include<sys/type.h>
#include<sys/socket.h>
#include<sys/stat.h>
#include<netinet/in.h>
#include<unistd.h>
#include<cstring>
#include<cstdlib>
#include<cstdio>
#include<sstream>
using namespace std;


#define MAX_BUF_SIZE	 99999
#define MAX_FILE_LEN	 10001
#define MAX_QUERY_LEN    10001

void httpserver(int newsockfd);

int main(int argc, char** argv){

	int sockfd, port;
	struct sockaddr_in server_addr, client_addr;
	
	if(argc != 2){
		cerr << "Usage: ./httpserver <port>" << endl;
		exit(1);
	}
	sscanf(argv[1], "%d", &port);

	// socket
	if( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		cerr << "Server: can't open stream socket" << endl;
		exit(0);
	}

	// ignore TCP TIME_WAIT state
    int on = 1;
    if((setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0){
       perror("Setsockopt error");
	}
	
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

	int newsockfd;
	socklen_t szclient = sizeof(client_addr);
	
	while(1){
		// accept
		newsockfd = accept(sockfd, (struct sockaddr *)&client_addr, &szclient);
			
		if(newsockfd < 0){
			cerr << "Server: accept error" << endl;
		}else {
			cerr << "Accept a connection successfully " << endl;
		}

		pid_t pid = fork();
		if(pid < 0){
			cerr << "Fork Error" << endl;
		}else if(pid == 0){
			close(sockfd);
			httpserver(newsockfd);
		}else if(pid > 0){
			close(newsockfd);
			// do nothing
		}

	}

	close(sockfd);
}


void httpserver(int newsockfd){
	// get http requset and parsing
	char buf[MAX_BUF_SIZE];
	read(newsockfd, buf, sizeof(buf));

	string tmp;
	stringstream ss(buf);
	ss >> tmp; ss >> tmp;

	char filename[MAX_FILE_LEN];
	char query[MAX_QUERY_LEN];
	sscanf(tmp.c_str(), "/%[^?]?%s", filename, query);

	// set environment variable
	setenv("QUERY_STRING", query, 1);
	setenv("CONTENT_LENGTH", "1", 1);
	setenv("REQUEST_METHOD", "2", 1);
	setenv("SCRIPT_NAME", "3", 1);
	setenv("REMOTE_HOST", "4", 1);
	setenv("REMOTE_ADDR", "5", 1);
	setenv("AUTH_TYPE", "6", 1);
	setenv("REMOTE_USER", "7", 1);
	setenv("REMOTE_IDENT", "8", 1);
	
	dup2(newsockfd, 0);
	dup2(newsockfd, 1);

	// get extension
	int idx = strlen(filename);
	string extension;
	for(int i=0; i<strlen(filename); i++){
		if(filename[i] == '.'){
			for(int k=i+1; k<strlen(filename); k++){
				extension += filename[k];
			}
			break;
		}
	}

	// return status
	struct stat fileStat;
	if( stat(filename, &fileStat) != 0 ){
		// file not found
		cout << "HTTP/1.1 404 Not Found" << endl;
		exit(0);
	}else if( extension != "cgi" && extension != "htm" && extension != "html" ){
		// extension is not correct
		cout << "HTTP/1.1 403 Forbidden" << endl;
		exit(0);
	}else{
		cout << "HTTP/1.1 200 OK" << endl;
	}
	
	if(extension == "cgi"){
		setenv("PATH", ".", 1);
		if(execlp(filename, filename, NULL) == -1){
			cout << "Unknown command: [" << filename << "]." << endl;
			exit(1);
		}
	}else if(extension == "htm" || extension == "html"){
		FILE *fin = fopen(filename, "r");
		char buf[MAX_BUF_SIZE];

		cout << "Content-type: text/html\n\n" ;
		while(fread(buf, 1, sizeof(buf), fin) > 0){
			cout << buf << endl;
		}
		fclose(fin);
		exit(0);
	}
	return ;
}
