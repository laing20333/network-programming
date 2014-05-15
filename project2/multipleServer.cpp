#include<iostream>
//#include<sys/type.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<unistd.h>
#include<sys/wait.h>
#include<sys/stat.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<sys/ioctl.h>
#include<signal.h>
#include<fcntl.h>
#include<cstring>
#include<cstdlib>
#include<cstdio>
#include<sstream>
#include<vector>
#include<map>

using namespace std;

#define MAX_ARGUMENT_LEN    1001
#define MAX_CLIENT_NUM		32
#define MAX_COMMAND_LEN 	100002
#define MAX_BUF_SIZE		1025
#define MAX_NAME_LEN		51
#define MAX_IP_LEN			101
#define KEY_NUM				5663


struct CLIENT_INFO{
	char name[MAX_NAME_LEN];
	char ip[MAX_IP_LEN];
	int port;
	char msgbuf[MAX_BUF_SIZE * 10];	
	bool isidexist;
	bool ismsgexist;
};

int sockfd, newsockfd, port;
struct sockaddr_in server_addr, client_addr;
int shmid;
int id = 1;
key_t key = KEY_NUM;
CLIENT_INFO *clientinfo; // id -> clientinfo


void DealWithCommand();
bool isFileExist(string filename);
void reaper(int st);
void Login();
void Logout();

int main(int argc, char** argv){
	if(argc != 2){		
		cerr << "Usage : ./concurrent <server port>" << endl;
		exit(1);
	}

	// prevent from zombie process
	(void)signal(SIGCHLD, reaper);

	// change wokring directory
	if(chdir("./rwg") == -1){
		cerr << "Change working directory fail" << endl;
	}
	
	// clean all environment variable
	clearenv();

	// get port number, init PATH
	sscanf(argv[1], "%d", &port);
	if(setenv("PATH", "bin:.", 1)!=0){
		cerr<<"Set PATH error"<<endl;
	}

	// socket
	if( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		cerr << "Can't open stream socket" << endl;
		exit(1);
	}

	// ignore TCP TIME_WAIT state	
	int on = 1;
	if((setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)
	   perror("setsockopt error");

	bzero((char *)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	// bind
	if(bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
		cerr << "Can't bind local address" << endl;
		exit(1);
	}

	// listen
	listen(sockfd, 10);

	// create shared memory
	if ((shmid = shmget(key, (MAX_CLIENT_NUM + 1) * sizeof(struct CLIENT_INFO), IPC_CREAT | 0666)) < 0) {
		cerr << "Fail to create shared memory" << endl;
        exit(1);
    }
	if ((clientinfo = (struct CLIENT_INFO*)shmat(shmid, NULL, 0)) == (struct CLIENT_INFO*)-1) {
        cerr << "Fail to attach shared memory to current process" << endl;
		exit(1);
	}

	// init shared memory
    for(int i=1; i<=MAX_CLIENT_NUM; i++){
		clientinfo[i].isidexist = false;
		clientinfo[i].ismsgexist = false;
	}
	

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
			exit(1);
		}else if(childpid == 0){
			// child process: for command
			Login();
			DealWithCommand();
			Logout();
		}else {
			// parent process: for next connection 
			close(newsockfd);
		}   

	}
	// remove share memory
    if (shmctl(shmid, IPC_RMID, (struct shmid_ds *) 0) < 0){
		cerr << "Fail to Remove Shared memory " << endl;
	}

	close(sockfd);
	return 0;
}

void DealWithCommand(){
	string command;
	map<int, vector<int> >table;
	int counter = 1;
	
	pid_t childpid2; // for sync with "execl output" and "% 

	while(1){
		bool iscommandlegal = true;
		bool isfifofrom = false;
		bool isfifoto = false;
		bool isredirection = false;
		pair<int, int> fifoid;
		string fifofrom_msg;
		string fifoto_msg;
		int filefd;

		// if msgbuf is not empty, print it 
		if(clientinfo[id].ismsgexist){
			cout << clientinfo[id].msgbuf;
			fflush(stdout);
			clientinfo[id].ismsgexist = false;
		}
		
		// read input from client
		char *input = new char[MAX_COMMAND_LEN];
	    for(int i=0;i<MAX_COMMAND_LEN;i++){
	        input[i]='\0';
		}
		
		if(read(STDIN_FILENO, input, MAX_COMMAND_LEN) <= 0){			
			delete [] input;
			continue;		
		}
			
		command = input;

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
				if(getenv(env_var.c_str()) == NULL){
					// this environment variable dose not exist
				}else {
					cout << env_var << "=" << getenv(env_var.c_str()) << endl;
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

				if(setenv(env_var.c_str(), value.c_str(), 1) == -1){
					cerr << "Setenv error" << endl;
				}
			}else if(precommand == "tell"){
				int toid;
				string msg;
				ss >> toid;

				// deal with front space and \r\n 
				getline(ss, msg);
				msg.erase(msg.begin());
				while(msg[msg.size()-1] == '\r' || msg[msg.size()-1] == '\n'){
					msg.erase(msg.end()-1);
				}				

				if(clientinfo[toid].isidexist){
					clientinfo[toid].ismsgexist = true;
					sprintf(clientinfo[toid].msgbuf, "*** %s told you ***: %s\n", clientinfo[id].name, msg.c_str());
				}else {
					cout << "*** Error: user #" << toid << " does not exist yet. ***" << endl;
				}
			}else if(precommand == "yell"){
				string msg;

			    // deal with front space and \r\n
				getline(ss, msg);
				msg.erase(msg.begin());
				while(msg[msg.size()-1] == '\r' || msg[msg.size()-1] == '\n'){
					msg.erase(msg.end()-1);
				}				
				
				for(int i=1; i<=MAX_CLIENT_NUM; i++){
					clientinfo[i].ismsgexist = true;
					sprintf(clientinfo[i].msgbuf, "*** %s yelled ***: %s\n", clientinfo[id].name, msg.c_str());				
				}

			}else if(precommand == "name"){
				string name;

				 // deal with front space and \r\n
	            getline(ss, name);
				name.erase(name.begin());
				while(name[name.size()-1] == '\r' || name[name.size()-1] == '\n'){
					name.erase(name.end()-1);
				}				
				
				bool isok = true;
				for(int i=1; i<=MAX_CLIENT_NUM; i++){
					if(name == clientinfo[i].name && clientinfo[i].isidexist){						
						isok = false;
						clientinfo[id].ismsgexist = true;
						sprintf(clientinfo[id].msgbuf, "*** User '%s' already exists. ***\n", clientinfo[i].name);
						break;
					}
				}

				if(isok){
					for(int i=1; i<=MAX_CLIENT_NUM; i++){
						clientinfo[i].ismsgexist = true;
						sprintf(clientinfo[i].msgbuf, "*** User from %s/%d is named '%s'. ***\n", clientinfo[id].ip, clientinfo[id].port, name.c_str());
					}
					sprintf(clientinfo[id].name, "%s", name.c_str());
				}

			}else if(precommand == "who"){
				cout << "<ID>\t<nickname>\t<IP/port>\t<indicate me>" << endl;
				for(int i=1; i<=MAX_CLIENT_NUM; i++){
					if(clientinfo[i].isidexist){
						cout << i << "\t" << clientinfo[i].name << "\t" << clientinfo[i].ip	<< "/" << clientinfo[i].port;
						if(i == id){
							cout << "\t" << "<-me";
						}
						cout << endl;
					}
				}


			}else if(isFileExist(precommand)){
				// if someone pipe to me, stdin -> pipe_read
				if(!table[counter].empty()){								
					dup2(table[counter][0], 0);
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
								dup2(pipe_fd[1], 2);
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
						filefd = open(stmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
						dup2(filefd, 1);
						isredirection = true;

					}else if( ( stmp[0] == '|' && isdigit(stmp[1]) ) || (stmp[0] == '!' && isdigit(stmp[1])) ) {								
						/** numbered-pipe **/
	
						int next_cnt;
						sscanf(stmp.substr(1).c_str(), "%d", &next_cnt);
										
						int dest = counter + next_cnt;
									
						if(!table[dest].empty()){
							// someone's pipe dest is same as me
							dup2(table[dest][1], 1);
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
							table[dest].push_back(pipe_fd[0]);
							table[dest].push_back(pipe_fd[1]);
						}	
					}else if(stmp[0] == '>' && isdigit(stmp[1])){
				
							// parsing id
							int toid;
							sscanf(stmp.c_str(), ">%d", &toid);
							
							char fifoname[MAX_NAME_LEN];
							sprintf(fifoname, "../fifo%d%d", id, toid);
							struct stat fileStat;

							if(!clientinfo[toid].isidexist){
									cout << "*** Error: user #" << toid << " does not exist yet. ***" << endl;
									iscommandlegal = false;
									break;
							}else if(stat(fifoname, &fileStat) == 0){
									cout << "*** Error: the pipe #" << id << "->#" << toid <<" already exists. ***" << endl;
									iscommandlegal = false;
									break;
							}else {
									// create fifo
									int fifofd = open(fifoname, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

									// stdout, stderr -> pipe_write
									dup2(fifofd, 1);
									dup2(fifofd, 2);
									close(fifofd);

									// make broadcast msg
									char msg[MAX_BUF_SIZE] = {};
									string inst = command;
									while(inst[inst.size()-1] == '\n' || inst[inst.size()-1] == '\r'){
											inst.erase(inst.end()-1);
									}
									sprintf(msg, "*** %s (#%d) just piped '%s' to %s (#%d) ***\n", clientinfo[id].name, id, inst.c_str(), 
											clientinfo[toid].name, toid);																		

									isfifoto = true;
									fifoto_msg = msg;
							}
				
					}else if(stmp[0] == '<' && isdigit(stmp[1])){
						
						int fromid;
						sscanf(stmp.c_str(), "<%d", &fromid);
						
						char fifoname[MAX_NAME_LEN];
						sprintf(fifoname, "../fifo%d%d", fromid, id);
						struct stat fileStat;

						if(stat(fifoname, &fileStat)  != 0){
							cout << "*** Error: the pipe #" << fromid << "->#" << id <<" does not exist yet. ***" << endl;
							iscommandlegal = false;
							break;
						}else{
							// stdin -> pipe_read, clear pipe table
							int fifofd = open(fifoname, O_RDWR, S_IRUSR | S_IWUSR);

							dup2(fifofd, 0);
							close(fifofd);						

							// make broadcast msg
							char msg[MAX_BUF_SIZE] = {};
							string inst = command;
							while(inst[inst.size()-1] == '\n' || inst[inst.size()-1] == '\r'){
								inst.erase(inst.end()-1);
							}
							sprintf(msg, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n", clientinfo[id].name, id, 
									clientinfo[fromid].name, fromid, inst.c_str());							

							isfifofrom = true;
							fifoid.first = fromid;
							fifoid.second = id;
							fifofrom_msg = msg;
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
						exit(1);
					}else if(!iscommandlegal){
						exit(0);
					}else {
						execvp(precommand.c_str(), arg);
					}
					
				}else {														
					// recover stdin, stdout, stderr
					dup2(newsockfd, 0);
					dup2(newsockfd, 1);
					dup2(newsockfd, 2);
				}		
			}else {
				cout << "Unknown command: [" << precommand << "]." << endl;
			}
		}

		// wait for child process exec external program, and then print '%'
		int status;
		waitpid(childpid2, &status, WUNTRACED | WCONTINUED);
		if(isredirection){
			close(filefd);
		}

		// after "a line" command parsing, put broadcast msg into shared memory in our order
		if(isfifofrom){
			// broadcast msg to everyone
			for(int i=1; i<=MAX_CLIENT_NUM; i++){
				clientinfo[i].ismsgexist = true;
				sprintf(clientinfo[i].msgbuf, "%s", fifofrom_msg.c_str());
			}

			// remove fifo
			char fifoname[MAX_NAME_LEN];
			sprintf(fifoname, "../fifo%d%d", fifoid.first, fifoid.second);
			remove(fifoname);
		}
		if(isfifoto){
			// broadcast msg to everyone
			for(int i=1; i<=MAX_CLIENT_NUM; i++){
				clientinfo[i].ismsgexist = true;
				if(isfifofrom){
					strcat(clientinfo[i].msgbuf, fifoto_msg.c_str());
				}else {
					sprintf(clientinfo[i].msgbuf, "%s", fifoto_msg.c_str());
				}
			}
		}

   	    // for syn broadcast msg
		if(clientinfo[id].ismsgexist){
			cout << clientinfo[id].msgbuf;
			fflush(stdout);
			clientinfo[id].ismsgexist = false;
		}
		cout << "% ";
		fflush(stdout);
		counter++;
	}
	
	return;
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

void reaper(int st){
	union wait status;
	while(wait3(&status, WNOHANG, (struct rusage *)0) > 0);
}

void Login(){
	// init clientinfo
	for(int i=1; i<=MAX_CLIENT_NUM; i++){
		if(!clientinfo[i].isidexist){
			id = i;
			clientinfo[i].isidexist = true;
			break;
		}
	}
	sprintf(clientinfo[id].name, "%s", "(no name)" );
	sprintf(clientinfo[id].ip, "%s", inet_ntoa(client_addr.sin_addr));
	clientinfo[id].port = (int)ntohs(client_addr.sin_port);

	// put login msg into shared memory
	for(int i=1; i<=MAX_CLIENT_NUM; i++){
		if(clientinfo[i].isidexist){
			sprintf(clientinfo[i].msgbuf, "*** User '%s' entered from %s/%d. ***\n", clientinfo[id].name, clientinfo[id].ip, clientinfo[id].port);
			clientinfo[i].ismsgexist = true;
		}
	}

	// set nonblocking i/o for clientfd
    int opt = 1;
	ioctl(newsockfd, FIONBIO, &opt);

	// stdin, stdout redirection
	dup2(newsockfd, 0);
	dup2(newsockfd, 1);
	dup2(newsockfd, 2);

	close(sockfd);

	// welcom message
	cout << "****************************************" << endl;
	cout << "** Welcome to the information server. **" << endl;
	cout << "****************************************" << endl;

	if(clientinfo[id].ismsgexist){
		cout << clientinfo[id].msgbuf;
		fflush(stdout);
		clientinfo[id].ismsgexist = false;
	}

	cout << "% ";
	fflush(stdout);

	return;
}

void Logout(){
	// put logout msg into shared memory
    for(int i=1; i<=MAX_CLIENT_NUM; i++){
		if(clientinfo[i].isidexist && i != id){
			sprintf(clientinfo[i].msgbuf, "*** User '%s' left. ***\n", clientinfo[id].name );
			clientinfo[i].ismsgexist = true;
		}
	}	
	
	cout << "*** User '" << clientinfo[id].name << "' left. ***" << endl;
	clientinfo[id].isidexist = false;
	close(newsockfd);

	// close fifo;
	for(int i=1; i<MAX_CLIENT_NUM; i++){
		char fifoname[MAX_NAME_LEN];
		sprintf(fifoname, "../fifo%d%d", id, i);
		remove(fifoname);

		sprintf(fifoname, "../fifo%d%d", i, id);
        remove(fifoname);
	}
	exit(0);
	return;
}
