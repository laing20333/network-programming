#include<iostream>
//#include<sys/type.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<unistd.h>
#include<sys/wait.h>
#include<sys/stat.h>
#include<signal.h>
#include<fcntl.h>
#include<cstring>
#include<cstdlib>
#include<cstdio>
#include<sstream>
#include<vector>
#include<map>

using namespace std;


int sockfd, newsockfd, port;

#define MAX_ARGUMENT_LEN 	1001
void DealWithCommand();
bool isFileExist(string filename);
void reaper(int s);


int main(int argc, char** argv){
	if(argc != 2){
		cerr << "Usage : ./np1 <server port>" << endl;
		exit(1);
	}

	// prevent from zombie process
	signal(SIGCHLD, reaper);
    struct sockaddr_in server_addr, client_addr;

	// change wokring directory
	if(chdir("./ras") == -1){
		cerr << "Change working directory fail" << endl;
	}

	// get port number, init PATH
	sscanf(argv[1], "%d", &port);
	if(setenv("PATH", "bin:.", 1)!=0){
		cerr<<"Set PATH error"<<endl;
	}

	// socket
	if( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		cerr << "Can't open stream socket" << endl;
		exit(0);
	}

	bzero((char *)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	// bind
	if(bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
		cerr << "Can't bind local address" << endl;
		exit(0);
	}

	// listen
	listen(sockfd, 10);

	while(1){

		socklen_t szclient = sizeof(client_addr);

		// accept
		newsockfd = accept(sockfd, (struct sockaddr *)&client_addr, &szclient);
		if(newsockfd < 0){
			cerr << "Accept error" << endl;
		}
		pid_t childpid = fork();
		if(childpid < 0){
			cerr << "Fork error" << endl;
			exit(0);
		}else if(childpid == 0){
			// child process: for command
			DealWithCommand();
		}else {
			// parent process: for next connection 
			close(newsockfd);
		}   

	}
	close(sockfd);
	return 0;
}

void DealWithCommand(){
	string command;
	map<int, vector<int> >table;
	int counter = 1;
	bool isredirection = false;
	
	// stdin, stdout redirection
	close(0);
	dup(newsockfd);
	close(1);
	dup(newsockfd);
	close(2);
	dup(newsockfd);
	//close(newsockfd);
	close(sockfd);

	// welcom message
	cout << "****************************************" << endl;
	cout << "** Welcome to the information server. **" << endl;
	cout << "****************************************" << endl;
	cout << "% ";

	pid_t childpid2; // for sync with "execl output" and "% "
	while(getline(cin, command) != (char *)0){
        
   	    // parsing command
        stringstream ss(command);
        string precommand;
		ss >> precommand;
		
		if(precommand == "exit"){			
			break;
		}else {

			if(precommand == "printenv"){
				/** printenv **/
				string env_var;
				ss >> env_var;
				cout << env_var << "=" << getenv(env_var.c_str()) << endl;
			}else if(precommand == "setenv"){
				/** setenv **/
				string env_var, value;
				ss >> env_var >> value;
				if(setenv(env_var.c_str(), value.c_str(), 1) == -1){
					cerr << "Setenv error" << endl;
				}
				
			}else if(isFileExist(precommand)){
				// if someone pipe to me, stdin -> pipe_read
				if(!table[counter].empty()){								
					close(0);
					dup(table[counter][0]);
					close(table[counter][1]);				
					close(table[counter][0]);
				}

				// set path and argument
				string command_path = "./bin/" + precommand;
				char *arg[MAX_ARGUMENT_LEN];
				int index = 1;
				memset(arg, 0, sizeof(arg));    //in case of previous allocate argument
				arg[0] = new char[command_path.size() + 1];
				strcpy(arg[0], command_path.c_str());
				string stmp;
				int fd;

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
							exit(0);
						}else if(childpid == 0){
							// child process for current command
							close(pipe_fd[0]);
							close(1);
							dup(pipe_fd[1]);
							
							if(stmp == "!"){
								close(2);
								dup(pipe_fd[1]);
							}
							close(pipe_fd[1]);
							arg[index] = new char(1);
		                    arg[index] = (char *)0;
							if(execvp(precommand.c_str(), arg) == -1){
								cout << "Unknown command: [" << precommand << "]." << endl;
								exit(0);
							}

						}else {
							// prevent from reading comment too fast, leading to fork error  
							int status;
							waitpid(childpid, &status, WUNTRACED | WCONTINUED);
									

							// parent process for next command
							close(pipe_fd[1]);
							close(0);
							dup(pipe_fd[0]);
							close(pipe_fd[0]);

							// init next command
							ss >> precommand;
							
							// if this command is not found
							if(!isFileExist(precommand)) {
								//cout << "Unknown command: [" << precommand << "]." << endl;
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
						fd = open(stmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
						dup2(fd, 1);
						isredirection = true;

					}else if( ( stmp[0] == '|' && isdigit(stmp[1]) ) || (stmp[0] == '!' && isdigit(stmp[1])) ) {								
						/** numbered-pipe **/
	
						int next_cnt;
						sscanf(stmp.substr(1).c_str(), "%d", &next_cnt);
										
						int dest = counter + next_cnt;
									
						if(!table[dest].empty()){
							// someone's pipe dest is same as me
							close(1);
							dup(table[dest][1]);										
						}else {
							// someone's pipe dest is not same as me

							// create pipe
							int pipe_fd[2];									
							if(pipe(pipe_fd) < 0){
								cerr << "Pipe error" << endl;
							}
										
							// stdout -> pipe_write
							close(1);
							dup(pipe_fd[1]);

							// stderr -> pipe_write
							if(stmp[0] == '!'){
								close(2);
								dup(pipe_fd[1]);
							}

							// save pipe_read, pipe_write									
							table[dest].push_back(pipe_fd[0]);
							table[dest].push_back(pipe_fd[1]);
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
					close(newsockfd);									

					// for last command exec
					arg[index] = new char(1);
					arg[index] = (char *)0;
					if(!isFileExist(precommand)) {
	                    cout << "Unknown command: [" << precommand << "]." << endl;
						exit(0);
					}else {
						execvp(precommand.c_str(), arg);
					}	
					
				}else {														
					// recover stdin, stdout, stderr
					close(0);
					dup(newsockfd);
					close(1);
					dup(newsockfd);				
					close(2);
					dup(newsockfd);										
				}		
				if(isredirection){
					isredirection = false;
					close(fd);
				}
			}else {
				cout << "Unknown command: [" << precommand << "]." << endl;
			}
		}
		// wait for child process exec external program, and then print '%'
		int status;
		waitpid(childpid2, &status, WUNTRACED | WCONTINUED);
		cout << "% ";
		counter++;
	}
	
	close(newsockfd);
	exit(0);
}
bool isFileExist(string filename){

	struct stat fileStat;
	string PATH = getenv("PATH");
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
	//union wait status;
	while(wait3(NULL, WNOHANG, NULL) >= 0);
}
