#include<iostream>
//#include<sys/type.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<sys/wait.h>
#include<sys/stat.h>
#include<sys/ioctl.h>
#include<signal.h>
#include<fcntl.h>
#include<cstring>
#include<cstdlib>
#include<cstdio>
#include<sstream>
#include<algorithm>
#include<vector>
#include<map>

using namespace std;


#define MAX_BUF_SIZE        1025
#define MAX_ARGUMENT_LEN 	1001
#define MAX_CLIENT_NUM		32
#define IN					3
#define OUT					4
#define ERR					5


int DealWithCommand(int fd);
bool isFileExist(string filename, int fd);
void reaper(int s);
void Login(int msockfd, int clientfd, socklen_t szclient, fd_set activefds);
void Logout(int msockfd, int clientfd, fd_set activefds);

struct CLIENT_INFO{
	int fd;
	string name;
	string ip;
	int port;
	map<string, string> env;
	map<int, vector<int> >table;
	int counter;
};

struct PIPE_FD{
	int read_fd;
	int write_fd;
};

bool isidexist[MAX_CLIENT_NUM + 2];
map<int, struct CLIENT_INFO> clientinfo; 	 	 // id -> clientinfo
map<int, int> fdtoid;
map<int, PIPE_FD> pipefd[MAX_CLIENT_NUM + 2];	 // id -> pipe_fd


int main(int argc, char** argv){
	int msockfd, port;

	if(argc != 2){		
		cerr << "Usage : ./np2-1 <server port>" << endl;
		exit(1);
	}
	
	// prevent from zombie process
	(void) signal(SIGCHLD, reaper);
    struct sockaddr_in server_addr, client_addr;
	
	// init id list
	for(int i=1; i<=MAX_CLIENT_NUM; i++){
		isidexist[i] = false;
	}

	// change wokring directory
	if(chdir("./rwg") == -1){
		cerr << "Change working directory fail" << endl;
	}
	
	// clean all environment variable
	clearenv();

	// get port number
	sscanf(argv[1], "%d", &port);

	// socket
	if( (msockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		cerr << "Can't open stream socket" << endl;
		exit(1);
	}
	
	// ignore TCP TIME_WAIT state	
	int on = 1;
	if((setsockopt(msockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)
	   perror("Setsockopt error");
	

	bzero((char *)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	// bind
	if(bind(msockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
		cerr << "Can't bind local address" << endl;
		exit(1);
	}

	// listen
	listen(msockfd, 10);

	fd_set readfds;
	fd_set activefds;

	FD_ZERO(&activefds);
	FD_SET(msockfd, &activefds);
	
	// backup stdin, stdout, stderr fd
	dup(0);
	dup(1);
	dup(2);

	while(1){
		
		int nfds = getdtablesize();
		memcpy(&readfds, &activefds, sizeof(readfds));

		//Listen all fds
		if(select(nfds, &readfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0) < 0){
			cerr << "Select error" << endl;
		}
		
		// Accept new connection 
		if(FD_ISSET(msockfd, &readfds)){
			socklen_t szclient = sizeof(client_addr);

			// accept
			int newsockfd;
			newsockfd = accept(msockfd, (struct sockaddr *)&client_addr, &szclient);

			if(newsockfd < 0){
				cerr << "Accept error" << endl;
			}else {
				FD_SET(newsockfd, &activefds);
				Login(msockfd, newsockfd, szclient, activefds);
			}
		}else {
			// Deal with command
			for(int fd=0; fd < nfds; fd++){
				if(fd != msockfd && FD_ISSET(fd, &readfds)){
					if(DealWithCommand(fd) < 0) {
						Logout(msockfd, fd, activefds);
					    FD_CLR(fd, &activefds);
					}
				}
			}		
		}

	}
	close(msockfd);
	return 0;
}

void Login(int msockfd, int clientfd, socklen_t szclient, fd_set activefds){
	// redirect stdin, stdout, stderr
	dup2(clientfd, 0);
	dup2(clientfd, 1);
	dup2(clientfd, 2);

	// welcom message
	cout << "****************************************" << endl;
	cout << "** Welcome to the information server. **" << endl;
	cout << "****************************************" << endl;

	int nfds = getdtablesize();
	struct sockaddr_in client;

	getpeername(clientfd, (struct sockaddr *)&client, &szclient);

	char ip[101];
	inet_ntop(AF_INET, &client.sin_addr, ip, sizeof(ip));
	int port = ntohs(client.sin_port);

	char cport[101];
	sprintf(cport, "%d", port);
	
	// broadcast enter msg
	string buf = std::string("*** User '(no name)' entered from ") + ip + "/" + cport + "." + " ***\n";
	for(int fd = 0; fd < nfds; fd++){
		if(fd != msockfd && FD_ISSET(fd, &activefds)){
			write(fd, buf.c_str(), buf.size());
		}
	}
	
	// enter's %
	cout << "% ";
	fflush(stdout);

	// init client information
	for(int i=1; i<=MAX_CLIENT_NUM; i++){
		if(!isidexist[i]){
			clientinfo[i].fd = clientfd;
			clientinfo[i].name = "(no name)";
			clientinfo[i].ip = ip;
			clientinfo[i].port = port;
			clientinfo[i].env["PATH"] = "bin:.";
			clientinfo[i].table.clear();
			clientinfo[i].counter = 1;
			isidexist[i] = true;
			fdtoid[clientfd] = i;
			break;
		}
	}

	return;
}

void Logout(int msockfd, int clientfd, fd_set activefds){
	int nfds = getdtablesize();
	string buf = std::string("*** User ") + "'" + clientinfo[fdtoid[clientfd]].name + "'" +" left. ***\n";
	for(int fd = 0; fd < nfds; fd++){
	    if(fd != msockfd && FD_ISSET(fd, &activefds)){
			write(fd, buf.c_str(), buf.size());
		}
	}
	
	//close every pipe related to me
	for(int i=1; i<=MAX_CLIENT_NUM; i++){
		if(pipefd[fdtoid[clientfd]].find(i) !=  pipefd[fdtoid[clientfd]].end()){
			close(pipefd[fdtoid[clientfd] ][i].read_fd);
			close(pipefd[fdtoid[clientfd] ][i].write_fd);
			pipefd[fdtoid[clientfd]].erase(i);
		}
		if(pipefd[i].find(fdtoid[clientfd]) != pipefd[i].end()){
			close(pipefd[i][fdtoid[clientfd] ].read_fd);
			close(pipefd[i][fdtoid[clientfd] ].write_fd);
			pipefd[i].erase(fdtoid[clientfd]);
		}
	}
	
	// update client information table
    isidexist[fdtoid[clientfd]] = false;
	clientinfo.erase(fdtoid[clientfd]);
	fdtoid.erase(clientfd);
	close(clientfd);
	return;
}

int DealWithCommand(int fd){
	string command;
	bool iscommandlegal = true;
	bool ispipefrom = false;
	bool ispipeto = false;
	bool isredirection = false;
	string pipefrom_msg;
	string pipeto_msg;

	// stdin, stdout redirection
	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);

	pid_t childpid2; // for sync with "execl output" and "% "
	getline(cin, command);         
   	    // parsing command
        stringstream ss(command);
        string precommand;
		ss >> precommand;
		
		if(precommand == "exit"){									
			dup2(IN, 0);
			dup2(OUT, 1);
			dup2(ERR, 2);
			return -1;
		}else {
			// set PATH environment var first
			if(setenv("PATH", clientinfo[fdtoid[fd]].env["PATH"].c_str(), 1) != 0){
		        cerr<<"Set PATH error"<<endl;
		    }

			if(precommand == "printenv"){
				/** printenv **/
				string env_var;
				ss >> env_var;
				if(clientinfo[fdtoid[fd]].env[env_var].empty()){
					// this environment variable dose not exist
				}else {
					cout << env_var << "=" << clientinfo[fdtoid[fd]].env[env_var] << endl;
				}
			}else if(precommand == "setenv"){
				/** setenv **/
				string env_var, value;
				ss >> env_var;
				getline(ss, value);
				value.erase(value.begin());
				while(value[value.size()-1] == '\r' || value[value.size()-1] == '\n'){
	                value.erase(value.end()-1);
				}
				clientinfo[fdtoid[fd]].env[env_var] = value;
				
			}else if(precommand == "tell"){
				int id;
				string msg;
				ss >> id;
				getline(ss, msg);
				msg.erase(msg.begin());
				while(msg[msg.size()-1] == '\r' || msg[msg.size()-1] == '\n'){
	                msg.erase(msg.end()-1);
				}

				if(!isidexist[id]){
					cout << "*** Error: user #" << id  << " does not exist yet. ***" << endl;
				}else {
					string buf = std::string("*** ") + clientinfo[fdtoid[fd]].name + " told you ***: " + msg + '\n';
					write(clientinfo[id].fd, buf.c_str(), buf.size());
				}
			}else if(precommand == "yell"){
				string msg;
				getline(ss, msg);
				msg.erase(msg.begin());
				while(msg[msg.size()-1] == '\r' || msg[msg.size()-1] == '\n'){
	                msg.erase(msg.end()-1);
				}

				string buf = std::string("*** ") + clientinfo[fdtoid[fd]].name + " yelled ***: " + msg + '\n';

				for(int i=1; i<=MAX_CLIENT_NUM; i++){				
					if(isidexist[i]){
						write(clientinfo[i].fd, buf.c_str(), buf.size());
					}
				}				

			}else if(precommand == "name"){
				string name;
				getline(ss, name);
				name.erase(name.begin());
				while(name[name.size()-1] == '\r' || name[name.size()-1] == '\n'){
	                name.erase(name.end()-1);
				}

				// test whether the same name exist
				bool isok = true;
				for(int i=1; i<=MAX_CLIENT_NUM; i++){
					if(clientinfo[i].name == name){
						cout << std::string("*** User '") + name + "' already exists. ***" << endl;
						isok = false;
						break;
					}
				}
				if(isok){
					char cport[11];
					sprintf(cport, "%d", clientinfo[fdtoid[fd]].port);
					string buf = std::string("*** ") + "User from " + clientinfo[fdtoid[fd]].ip + "/" + cport + " is named '"
							     + name + "'. ***\n";

					clientinfo[fdtoid[fd]].name = name;
		            for(int i=1; i<=MAX_CLIENT_NUM; i++){
						if(isidexist[i]){
							write(clientinfo[i].fd, buf.c_str(), buf.size());
						}
					}		
				}

			}else if(precommand == "who"){
				cout << "<ID>\t<nickname>\t<IP/port>\t<indicate me>" << endl;
				for(int i=1; i<=MAX_CLIENT_NUM; i++){
					if(isidexist[i]){
						cout << i << "\t" << clientinfo[i].name << "\t" << clientinfo[i].ip
						<< "/" << clientinfo[i].port;
						if(clientinfo[i].fd == fd){
							cout << "\t" << "<-me";
						}
						cout << endl;
					}
				}

			}else if(isFileExist(precommand, fd)){
				// if someone pipe to me, stdin -> pipe_read
				if(!clientinfo[fdtoid[fd]].table[clientinfo[fdtoid[fd]].counter].empty()){								
					dup2(clientinfo[fdtoid[fd]].table[clientinfo[fdtoid[fd]].counter][0], 0);					
					close(clientinfo[fdtoid[fd]].table[clientinfo[fdtoid[fd]].counter][1]);				
					close(clientinfo[fdtoid[fd]].table[clientinfo[fdtoid[fd]].counter][0]);
				}

				// set path and argument
				string command_path = "./bin/" + precommand;
				char *arg[MAX_ARGUMENT_LEN];
				int index = 1;
				memset(arg, 0, sizeof(arg));    //in case of previous allocate argument
				arg[0] = new char[command_path.size() + 1];
				strcpy(arg[0], command_path.c_str());
				string stmp;
				int filefd;				

				// deal with remain comment
				while(ss >> stmp){	
					if(stmp == "|" || stmp == "!"){
						/** Pipe in a single command **/
						
						// create pipe
						int pipe_fd[2];
						if(pipe(pipe_fd) < 0){
							cerr << "Pipe error" << endl;
						}
						
						pid_t childpid = fork();
						if(childpid < 0){
							cerr << "Fork error" << endl;
							exit(1);
						}else if(childpid == 0){
							// child process for current command
							close(pipe_fd[0]);
							dup2(pipe_fd[1], 1);
							
							if(stmp == "!"){
								close(2);
								dup(pipe_fd[1]);
							}
							close(pipe_fd[1]);
							arg[index] = new char(1);
		                    arg[index] = (char *)0;
							
							if(execvp(precommand.c_str(), arg) == -1){
								cout << "Unknown command: [" << precommand << "]." << endl;
								exit(1);
							}

						}else {
							// prevent from reading comment too fast, leading to fork error  
							int status;
							waitpid(childpid, &status, WUNTRACED | WCONTINUED);
									

							// parent process for next command
							close(pipe_fd[1]);
							dup2(pipe_fd[0], 0);
							close(pipe_fd[0]);

							// init next command
							ss >> precommand;
							
							// if this command is not found
							if(!isFileExist(precommand, fd)) {
								cout << "Unknown command: [" << precommand << "]." << endl;
								iscommandlegal = false;
								break;  
							}

							command_path = "./bin/" + precommand;
							index = 1;
							memset(arg, 0, sizeof(arg));    //in case of previous allocated arguments
							arg[0] = new char[command_path.size() + 1];
							strcpy(arg[0], command_path.c_str());
							
						}
					}else if(stmp == ">"){
						/** For redirection **/									
						ss >> stmp;
						filefd = open(stmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
						dup2(filefd, 1);
						isredirection = true;

					}else if( ( stmp[0] == '|' && isdigit(stmp[1]) ) || (stmp[0] == '!' && isdigit(stmp[1])) ) {								
						/** numbered-pipe **/	
						int next_cnt;
						sscanf(stmp.substr(1).c_str(), "%d", &next_cnt);
										
						int dest = clientinfo[fdtoid[fd]].counter + next_cnt;
									
						if(!clientinfo[fdtoid[fd]].table[dest].empty()){
							// someone's pipe dest is same as me
							dup2(clientinfo[fdtoid[fd]].table[dest][1], 1);
						}else {
							// someone's pipe dest is not same as me

							// create pipe
							int pipe_fd[2];									
							if(pipe(pipe_fd) < 0){
								cerr << "Pipe error" << endl;
							}
										
							// stdout -> pipe_write
							dup2(pipe_fd[1], 1);

							// stderr -> pipe_write
							if(stmp[0] == '!'){
								dup2(pipe_fd[1], 2);
							}

							// save pipe_read, pipe_write									
							clientinfo[fdtoid[fd]].table[dest].push_back(pipe_fd[0]);
							clientinfo[fdtoid[fd]].table[dest].push_back(pipe_fd[1]);
						}	
					}else if(stmp[0] == '>' && isdigit(stmp[1])){
					
						// parsing id 
						int toid;
						sscanf(stmp.c_str(), ">%d", &toid);

						if(!isidexist[toid]){
							cout << "*** Error: user #" << toid << " does not exist yet. ***" << endl;							
							iscommandlegal = false;
							break;
						}else if(pipefd[fdtoid[fd]].find(toid) != pipefd[fdtoid[fd]].end()){
							cout << "*** Error: the pipe #" << fdtoid[fd] << "->#" << toid <<" already exists. ***" << endl;
							iscommandlegal = false;
							break;
						}else {

							// create pipe
							int pipe_fd[2];
							if(pipe(pipe_fd) < 0){
								cerr << "Pipe error" << endl;
							}
						
							pipefd[fdtoid[fd]][toid].read_fd = pipe_fd[0];
							pipefd[fdtoid[fd]][toid].write_fd = pipe_fd[1];
							
						
							// stdout, stderr -> pipe_write							
							dup2(pipe_fd[1], 1);
							dup2(pipe_fd[1], 2);
							close(pipe_fd[1]);			
						

							// make broadcast msg
							char buf[MAX_BUF_SIZE] = {};
							string inst = command;
							while(inst[inst.size()-1] == '\n' || inst[inst.size()-1] == '\r'){
								inst.erase(inst.end()-1);
							}
							sprintf(buf, "*** %s (#%d) just piped '%s' to %s (#%d) ***\n", clientinfo[fdtoid[fd]].name.c_str(), 
							fdtoid[fd], inst.c_str(), clientinfo[toid].name.c_str(), toid);
							
							ispipeto = true;
							pipeto_msg = buf;
						
						}
					
					}else if(stmp[0] == '<' && isdigit(stmp[1])){
					
						int fromid;
						sscanf(stmp.c_str(), "<%d", &fromid);

						if(pipefd[fromid].find(fdtoid[fd]) == pipefd[fromid].end()){
							cout << "*** Error: the pipe #" << fromid << "->#" << fdtoid[fd] <<" does not exist yet. ***" << endl;
							iscommandlegal = false;
							break;
						}else{
						 	// stdin -> pipe_read, clear pipe table
							dup2(pipefd[fromid][fdtoid[fd]].read_fd, 0);
							close(pipefd[fromid][fdtoid[fd]].read_fd); 
							pipefd[fromid].erase(fdtoid[fd]);							

							// make broadcast msg
							char buf[MAX_BUF_SIZE] = {};
							string inst = command;
							while(inst[inst.size()-1] == '\n' || inst[inst.size()-1] == '\r'){
								inst.erase(inst.end()-1);
							}
							sprintf(buf, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n", clientinfo[fdtoid[fd]].name.c_str(),
							fdtoid[fd], clientinfo[fromid].name.c_str(), fromid, inst.c_str());
							
							ispipefrom = true;
							pipefrom_msg = buf;
							
						}

					}else {	
						// program arguments 
						arg[index] = new char[stmp.size() + 1];
						strcpy(arg[index], stmp.c_str());
						index++;
					}

				}

				childpid2 = fork();
				if(childpid2 == 0){						
					// for last command exec
					arg[index] = new char(1);
					arg[index] = (char *)0;
					if(!isFileExist(precommand, fd)) {
		        		cout << "Unknown command: [" << precommand << "]." << endl;
						exit(1);
					}else if(!iscommandlegal){
						exit(0);
					}else {
						execvp(precommand.c_str(), arg);
					}					
				}else {														
					// recover stdin,stdout, stderr
					dup2(fd, 0);
					dup2(fd, 1);
					dup2(fd, 2);						
				}				

				if(isredirection){
					close(filefd);
				}

			}else {
				cout << "Unknown command: [" << precommand << "]." << endl;
			}
		}	

		// wait for child process exec external program, and then print '%'
		int status;
		waitpid(childpid2, &status, WUNTRACED | WCONTINUED);
		if(ispipefrom){
			 // broadcast msg to everyone
			 for(int i=1; i<=MAX_CLIENT_NUM; i++){
			 	if(isidexist[i]){
					write(clientinfo[i].fd, pipefrom_msg.c_str(), pipefrom_msg.size());
				}                                                                                                                                                         }
		}
		if(ispipeto){
			// broadcast msg to everyone
			for(int i=1; i<=MAX_CLIENT_NUM; i++){
				if(isidexist[i]){
					write(clientinfo[i].fd, pipeto_msg.c_str(), pipeto_msg.size());
				}
			}
		}
		cout << "% ";
		fflush(stdout);
		clientinfo[fdtoid[fd]].counter++;	
		return 0;
}
bool isFileExist(string filename, int fd){
	struct stat fileStat;
	string PATH = clientinfo[fdtoid[fd]].env["PATH"];
	string subpath;
	stringstream ss(PATH);

	while(getline(ss, subpath, ':') != NULL){
		string dest = "./";
		if(subpath == "."){
			dest = filename;
		}else dest = dest + subpath + '/' + filename;
		
		if(stat(dest.c_str(), &fileStat) == 0){		
			return true;
		}
	}
	return false;
}

void reaper(int s){
	union wait status;
	while(wait3(&status, WNOHANG, (struct rusage *)0) > 0);
}
