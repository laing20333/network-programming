#include<iostream>
//#include<sys/type.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<netdb.h>
#include<unistd.h>
#include<sys/wait.h>
#include<sys/stat.h>
#include<sys/ioctl.h>
#include<signal.h>
#include<fcntl.h>
#include<cstring>
#include<cstdlib>
#include<cstdio>
#include<cerrno>
#include<sstream>
#include<algorithm>
#include<vector>
#include<map>

using namespace std;

#define MAX_BUF_SIZE 99999
#define MAX_TARGET_SERVER 5
#define F_CONNECTING 0
#define F_READING 1
#define F_WRITING 2
#define F_DONE 3

struct SERVER{
	string ip;
	string port;
	string filename;
	int sockfd;
	int filefd;
};

string linebuffer[MAX_TARGET_SERVER];
int readline(int fd, string &linebuffer);

int main(int argc, char** argv){
	
	int csock;
	struct addrinfo hints, *server;

	/* get remote server ip, port, batch filename */
    string env = getenv("QUERY_STRING");
	string tmp;
	stringstream ss(env);
	vector<struct SERVER> serverinfo;
	struct SERVER tmp_server;
	while(getline(ss, tmp, '=') != NULL){
		string ip, port, filename;
		getline(ss, ip, '&');
		
		getline(ss, tmp, '=');
		getline(ss, port, '&');

		getline(ss, tmp, '=');
        getline(ss, filename, '&');

		if(!ip.empty()  && !port.empty() && !filename.empty()){
			tmp_server.ip = ip;
			tmp_server.port = port;
			tmp_server.filename = filename;
			tmp_server.sockfd = -1;
			serverinfo.push_back(tmp_server);
		}
	}

	int status[MAX_TARGET_SERVER];
	memset(status, -1, sizeof(status));
    fd_set rfds; /* readable file descriptors*/
    fd_set wfds; /* writable file descriptors*/
    fd_set rs; /* active file descriptors*/
    fd_set ws; /* active file descriptors*/

    int conn = serverinfo.size(); 
    int nfds = 1001;

    FD_ZERO(&rfds); FD_ZERO(&wfds); FD_ZERO(&rs); FD_ZERO(&ws);

	/* connect to server and set fd_set */
	for(int i=0; i<conn; i++){
		memset(&hints, 0, sizeof(hints));
		getaddrinfo(serverinfo[i].ip.c_str(), serverinfo[i].port.c_str(), &hints, &server);
		status[i] = F_CONNECTING;
		
		// socket
		int csock;
	    if( (csock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
	        cerr << "Can't open stream socket" << endl;
	        exit(1);
	    }
		serverinfo[i].sockfd = csock;	


		// open batchfile and get its fd
		FILE *fin = fopen(serverinfo[i].filename.c_str(), "r");
		serverinfo[i].filefd = fileno(fin);
		
		// for nonblocking i/o
	    int flag = fcntl(csock, F_GETFL, 0);
	    fcntl(csock, F_SETFL, flag | O_NONBLOCK);
		
		// connect
		if ( connect(csock, server->ai_addr, server->ai_addrlen) < 0 ){
			if (errno != EINPROGRESS) {
			    return -1;
			}
			//exit(1);
		}
		//cout << "connect over" << endl;
		
		// set fd
		FD_SET(csock, &rs);
	    FD_SET(csock, &ws);
	}
	cout << "Content-type: text/html\n\n";
	cout << "<html>" << endl;
	cout << "<head>" << endl;
	cout << "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />" << endl;
	cout << "<title>Network Programming Homework 3</title>"  << endl;
	cout << "</head>" << endl;
	cout << "<body bgcolor=#336699>" << endl;
	cout << "<font face=\"Courier New\" size=2 color=#FFFF99>" << endl;
	cout << "<table width=\"800\" border=\"1\">" << endl;
	cout << "<tr>" << endl;

	//usleep(1000);

	for(int i=0; i<serverinfo.size(); i++){
		cout << "<td>" << serverinfo[i].ip << "</td>"; 
	}
	cout << endl;

	cout << "<tr>" << endl;
	for(int i=0; i<serverinfo.size(); i++){
		cout << "<td valign=\"top\" id=\"" << "m" << i <<"\"></td>";
	}
	cout << endl;
	cout << "</table>" << endl;
	
    int needwrite[MAX_TARGET_SERVER];
    bool isend[MAX_TARGET_SERVER];
    memset(needwrite, 0, sizeof(needwrite));
    memset(isend, 0, sizeof(isend));
	for(int i=0; i<MAX_TARGET_SERVER; i++){
		linebuffer[i].clear();
	}
	
	int max_conn = serverinfo.size();
	/* write batchfile to server, read result and send it back to browser */
	while (conn > 0) {
		memcpy(&rfds, &rs, sizeof(rfds)); memcpy(&wfds, &ws, sizeof(wfds));
		
		if ( select(nfds, &rfds, &wfds, (fd_set*)0, (struct timeval*)0) < 0 ){
			exit(1);
		}
		
		for(int i=0; i<max_conn; i++){
			if (status[i] == F_CONNECTING && (FD_ISSET(serverinfo[i].sockfd, &rfds) || FD_ISSET(serverinfo[i].sockfd, &wfds))){
				int error;
				socklen_t n = sizeof(error);
				if (getsockopt(serverinfo[i].sockfd, SOL_SOCKET, SO_ERROR, &error, &n) < 0 ) {
					// non-blocking connect failed
					return (-1);
				}
				status[i] = F_READING;
                FD_CLR(serverinfo[i].sockfd, &ws);
			}else if (status[i] == F_WRITING && FD_ISSET(serverinfo[i].sockfd, &wfds) ) {
				
				// read input from batchfile
				if(needwrite[i] <= 0){
					linebuffer[i].clear();
					int nread = readline(serverinfo[i].filefd, linebuffer[i]);
					// write data to browser
					if(nread > 0){
						needwrite[i] = nread;
						for(int k=0; k<linebuffer[i].size(); k++){
							if(linebuffer[i][k] == '\n'){
								cout << "<script>document.all['m" << i << "'].innerHTML += \"" << "<br>" << "\";</script>";
							}else {
								cout << "<script>document.all['m" << i << "'].innerHTML += \"" << linebuffer[i][k] << "\";</script>";
							}
						}
						fflush(stdout);
					}else {
						// nothing to read from file
						FD_CLR(serverinfo[i].sockfd, &ws);
		    	        FD_SET(serverinfo[i].sockfd, &rs);
		        	    status[i] = F_READING;
						isend[i] = true;
						continue;
					}
					// set end_flag for exit
					if( linebuffer[i] == "exit" || linebuffer[i] == "exit\n" ){
						isend[i] = true;
					}
				}

				// write data to server
				if(needwrite[i] > 0){
					int nwrite;
					nwrite = write(serverinfo[i].sockfd, linebuffer[i].c_str(), linebuffer[i].size());
					if(nwrite > 0){
						needwrite[i] -= nwrite;
						linebuffer[i] = linebuffer[i].substr(nwrite); // remove precommand
					}
					if(needwrite[i] <= 0){
						FD_CLR(serverinfo[i].sockfd, &ws);
						FD_SET(serverinfo[i].sockfd, &rs);
						status[i] = F_READING;
						linebuffer[i].clear();
					}
				}
				
			}else if (status[i] == F_READING && FD_ISSET(serverinfo[i].sockfd, &rfds) ) {
				int nread;
				char buf[MAX_BUF_SIZE];
				
				linebuffer[i].clear();
				nread = readline(serverinfo[i].sockfd, linebuffer[i]);

				if (nread <= 0) {
					if(isend[i]){
						FD_CLR(serverinfo[i].sockfd, &rs);
						FD_CLR(serverinfo[i].sockfd, &ws);

						status[i] = F_DONE;
						shutdown(serverinfo[i].sockfd, SHUT_RDWR);
						close(serverinfo[i].sockfd);
						close(serverinfo[i].filefd);
						conn--;
						//break;
					}else {
						status[i] = F_WRITING;
						FD_CLR(serverinfo[i].sockfd, &rs);
                        FD_SET(serverinfo[i].sockfd, &ws);
					}
				}else {
					if(linebuffer[i].substr(0, 2) == "% "){
						status[i] = F_WRITING;
                        FD_CLR(serverinfo[i].sockfd, &rs);
                        FD_SET(serverinfo[i].sockfd, &ws);
					}
					for(int k=0; k<linebuffer[i].size(); k++){
						if(linebuffer[i][k] == '\n'){
							cout << "<script>document.all['m" << i << "'].innerHTML += \"" << "<br>" << "\";</script>";
						}else {
							cout << "<script>document.all['m" << i << "'].innerHTML += \"" << linebuffer[i][k] << "\";</script>";
						}
					}
					fflush(stdout);
				}
			}
		}
	}

	cout << "</font>" << endl;
	cout << "</body>" << endl;
    cout << "</html>" << endl;
	fflush(stdout);

	return 0;
}

int readline(int fd, string &linebuffer){
	int nread = 0;
	char c;
	while(1){
		int n = read(fd, &c, sizeof(char));
		if(n == 1){
			nread++;
			linebuffer += c;
			if(c == '\n'){
				break;
			}
		}else {
			if(nread == 0){
				return -1;
			}else {
				//linebuffer += '\n';
				//nread++;
				return nread;
			}
		}
	}
	return nread;
}

