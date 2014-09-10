#include <windows.h>
#include <sys/types.h>
#include <string>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <list>
#include <vector>
#include <iostream>
using namespace std;

#include "resource.h"

#define SERVER_PORT 7799

#define WM_SOCKET_NOTIFY (WM_USER + 1)

#define MAX_BUF_SIZE 99999
#define MAX_TARGET_SERVER 5
#define F_CONNECTING 0
#define F_READING 1
#define F_WRITING 2
#define F_DONE 3

BOOL CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
int EditPrintf(HWND, TCHAR *, ...);
//=================================================================t
//	Global Variables
//=================================================================
list<SOCKET> Socks;

struct SERVER{
	string ip;
	string port;
	string filename;
	int sockfd;
	FILE* fileptr;
};
static int conn = 0;
static int max_conn = 0;
static int status[MAX_TARGET_SERVER];
static string linebuffer[MAX_TARGET_SERVER];
static int needwrite[MAX_TARGET_SERVER];
static bool isend[MAX_TARGET_SERVER];
static vector<struct SERVER> serverinfo;

int readline(int fd, string &linebuffer);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	return DialogBox(hInstance, MAKEINTRESOURCE(ID_MAIN), NULL, MainDlgProc);
}

BOOL CALLBACK MainDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	WSADATA wsaData;

	static HWND hwndEdit;
	static SOCKET msock, ssock;
	static struct sockaddr_in sa;

	if (conn == 0){
		memset(needwrite, 0, sizeof(needwrite));
		memset(isend, 0, sizeof(isend));

		for (int i = 0; i < MAX_TARGET_SERVER; i++){
			linebuffer[i].clear();
		}
	}

	int err;


	switch (Message)
	{
	case WM_INITDIALOG:
		hwndEdit = GetDlgItem(hwnd, IDC_RESULT);
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_LISTEN:

			WSAStartup(MAKEWORD(2, 0), &wsaData);

			//create master socket
			msock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

			if (msock == INVALID_SOCKET) {
				EditPrintf(hwndEdit, TEXT("=== Error: create socket error ===\r\n"));
				WSACleanup();
				return TRUE;
			}

			err = WSAAsyncSelect(msock, hwnd, WM_SOCKET_NOTIFY, FD_ACCEPT | FD_CLOSE | FD_READ | FD_WRITE);

			if (err == SOCKET_ERROR) {
				EditPrintf(hwndEdit, TEXT("=== Error: select error ===\r\n"));
				closesocket(msock);
				WSACleanup();
				return TRUE;
			}

			//fill the address info about server
			sa.sin_family = AF_INET;
			sa.sin_port = htons(SERVER_PORT);
			sa.sin_addr.s_addr = INADDR_ANY;

			//bind socket
			err = bind(msock, (LPSOCKADDR)&sa, sizeof(struct sockaddr));

			if (err == SOCKET_ERROR) {
				EditPrintf(hwndEdit, TEXT("=== Error: binding error ===\r\n"));
				WSACleanup();
				return FALSE;
			}

			err = listen(msock, 2);

			if (err == SOCKET_ERROR) {
				EditPrintf(hwndEdit, TEXT("=== Error: listen error ===\r\n"));
				WSACleanup();
				return FALSE;
			}
			else {
				EditPrintf(hwndEdit, TEXT("=== Server START ===\r\n"));
			}

			break;
		case ID_EXIT:
			EndDialog(hwnd, 0);
			break;
		};
		break;

	case WM_CLOSE:
		EndDialog(hwnd, 0);
		break;

	case WM_SOCKET_NOTIFY:
		switch (WSAGETSELECTEVENT(lParam))
		{
		case FD_ACCEPT:
			ssock = accept(msock, NULL, NULL);
			Socks.push_back(ssock);
			EditPrintf(hwndEdit, TEXT("=== Accept one new client(%d), List size:%d ===\r\n"), ssock, Socks.size());
			break;
		case FD_READ:{
			//Write your code for read event here.

			/* Start to read request from browser */
			if (max_conn == 0){
				// new http request
				char request[1001];
				int nread;
				nread = recv(ssock, request, sizeof(request), 0);
				//request[nread] = '\0';
				EditPrintf(hwndEdit, TEXT("Read browser request\r\n"));
				//EditPrintf(hwndEdit, TEXT(request));

				// parsing request, get server ip, port and batchfile name
				stringstream ss(request);
				string tmp;
				ss >> tmp; ss >> tmp;
				char filename[1001];
				char information[1001];
				struct SERVER tmp_server;

				sscanf(tmp.c_str(), "/%[^?]?%s", filename, information);
				
				ss.str(information);
				while (getline(ss, tmp, '=')){
					string ip, port, filename;
					getline(ss, ip, '&');
					
					getline(ss, tmp, '=');					
					getline(ss, port, '&');

					getline(ss, tmp, '=');					
					getline(ss, filename, '&');
					if (!ip.empty() && !port.empty() && !filename.empty()){
						tmp_server.ip = ip;
						tmp_server.port = port;
						tmp_server.filename = filename;
						tmp_server.sockfd = -1;
						tmp_server.fileptr = NULL;
						serverinfo.push_back(tmp_server);
					}
				}
				
				conn = max_conn = serverinfo.size();

				// send status to browser
				FILE *fin = fopen(filename, "r");
				string httpstatus;
				if (fin == NULL){
					httpstatus = "HTTP/1.1 404 Not Found\n";
				}else {
					httpstatus = "HTTP/1.1 200 OK\n";
				}
				send(ssock, httpstatus.c_str(), httpstatus.size(), 0);

				char name[1001];
				char extension[1001];
				sscanf(filename, "%[^.].%s", name, extension);

				if (!strcmp(extension, "htm") || !strcmp(extension, "html")){
					// htm / html
					string header = "Content-type: text/html\n\n";
					char content[1001];
					send(ssock, header.c_str(), header.size(), 0);
					int nread = 0;
					while ((nread = fread(content, 1, sizeof(content), fin)) > 0){
						if (nread < sizeof(content)){
							content[nread] = '\0';
						}
						send(ssock, content, nread, 0);
					}
					fclose(fin);
					closesocket(ssock);
				} else if (!strcmp(extension, "cgi")){
					// cgi
					struct hostent *address;
					struct sockaddr_in server_sin;

					// setup connection to server
					for (unsigned int i = 0; i < serverinfo.size(); i++){
						address = gethostbyname(serverinfo[i].ip.c_str());
						status[i] = F_CONNECTING;

						server_sin.sin_family = AF_INET;
						server_sin.sin_addr = *((struct in_addr *)address->h_addr);
						server_sin.sin_port = htons(atoi(serverinfo[i].port.c_str()));

						// socket
						int csock;
						if ((csock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
							cerr << "Can't open stream socket" << endl;
							exit(1);
						}
						serverinfo[i].sockfd = csock;

						// open batchfile and get its fd
						FILE *fin = fopen(serverinfo[i].filename.c_str(), "r");
						serverinfo[i].fileptr = fin;

						// set nonblocking socket
						//unsigned nonblocking = 1;
						//ioctlsocket(csock, FIONBIO, &nonblocking);

						// connect
						if (connect(csock, (struct sockaddr *)&server_sin, sizeof(server_sin)) < 0){
							//exit(1);
						}
						status[i] = F_READING;

						// asynchronous select
						err = WSAAsyncSelect(csock, hwnd, WM_SOCKET_NOTIFY, FD_CLOSE | FD_READ | FD_WRITE);

						if (err == SOCKET_ERROR) {
							EditPrintf(hwndEdit, TEXT("=== Error: select error ===\r\n"));
							closesocket(csock);
							WSACleanup();
							return TRUE;
						 }
					}
					
					// browser table
					string buf;
					buf += "Content-type: text/html\n\n";
					buf += "<html>\n";
					buf += "<head>\n";
					buf += "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />\n";
					buf += "<title>Network Programming Homework 3</title>\n";
					buf += "</head>\n";
					buf += "<body bgcolor=#336699>\n";
					buf += "<font face=\"Courier New\" size=2 color=#FFFF99>\n";
					buf += "<table width=\"800\" border=\"1\">\n";
					buf += "<tr>\n";

					for (unsigned int i = 0; i<serverinfo.size(); i++){
						buf = buf + "<td>" + serverinfo[i].ip + "</td>";
					}
					buf += "\n";
					
					buf += "<tr>\n";
					char tmp[MAX_BUF_SIZE] = {};
					for (unsigned int i = 0; i<serverinfo.size(); i++){
						sprintf(tmp, "%s%d%s", "<td valign=\"top\" id=\"m", i, "\"></td>");
						string stmp = tmp;
						buf = buf + stmp;
					}
					buf += "\n";
					buf += "</table>\n";

					send(ssock, buf.c_str(), buf.size(), 0);

				}							 
			} else {
				/* CGI */
				for (int i = 0; i<max_conn; i++){
					if (status[i] == F_WRITING && serverinfo[i].sockfd == (SOCKET)wParam) {
						//read input from batchfile
						if (needwrite[i] <= 0){
							linebuffer[i].clear();
							char buf[MAX_BUF_SIZE];
							int nread = 0;
							EditPrintf(hwndEdit, TEXT("\r\n"));

							if (fgets(buf, sizeof(buf), serverinfo[i].fileptr) == NULL){
								EditPrintf(hwndEdit, TEXT("File is in the end\r\n"));
								nread = 0;
							}else {
								for (unsigned int k = 0; k < strlen(buf); k++){
									linebuffer[i] += buf[k];
								}
								nread = linebuffer[i].size();
							}

							EditPrintf(hwndEdit, TEXT("Read from file over \r\n"));

							if (nread > 0){
								needwrite[i] = nread;
								EditPrintf(hwndEdit, TEXT("Write command to Browser:"));
								// write command to browser
								for (unsigned int k = 0; k < linebuffer[i].size(); k++){
									char tmp[MAX_BUF_SIZE] = {};
									if (linebuffer[i][k] == '\n'){
										sprintf(tmp, "%s%d%s", "<script>document.all['m", i, "'].innerHTML += \"<br>\";</script>");
									}
									else {
										sprintf(tmp, "%s%d%s%c%s", "<script>document.all['m", i, "'].innerHTML += \"", linebuffer[i][k], "\";</script>");
									}
									string stmp;
									for (unsigned int m = 0; m < strlen(tmp); m++){
										stmp += tmp[m];
									}
									send(ssock, stmp.c_str(), stmp.size(), 0);
									//EditPrintf(hwndEdit, TEXT("%c"), linebuffer[i][k]);
								}
								EditPrintf(hwndEdit, TEXT("Write command to Browser over:\r\n"));

								if (linebuffer[i].substr(0, 4) == "exit" || linebuffer[i].substr(0, 5) == "exit\n"){									
									isend[i] = true;									
								}
							}else {
								// batchfile is in the end
								isend[i] = true;
								status[i] = F_READING;
							}
						}
									
						// write command to server
						if (needwrite[i] > 0){
							int nwrite;
							EditPrintf(hwndEdit, TEXT("Write command to Server:\r\n"));
							nwrite = send(serverinfo[i].sockfd, linebuffer[i].c_str(), linebuffer[i].size(), 0);
													
							if (nwrite > 0){
								needwrite[i] -= nwrite;
								linebuffer[i] = linebuffer[i].substr(nwrite); // remove precommand
							}
							if (needwrite[i] <= 0){
								status[i] = F_READING;
								// sync for server react "exit" command
								
								linebuffer[i].clear();
							}
							
							EditPrintf(hwndEdit, TEXT("Write command to Server over\r\n"));
						}
						
						/* if the command is "exit", force process to enter F_READING
						due to no one trigger FD_READ to enter F_READING to close connection
						*/
						if (isend[i]){
							status[i] = F_READING;
							i--;
							// for sync server
							Sleep(1000);
							continue;
						}
						
						return true;

					}else if (status[i] == F_READING && serverinfo[i].sockfd == (SOCKET)wParam) {
						// read from server
						int nread;
						linebuffer[i].clear();
						nread = readline(serverinfo[i].sockfd, linebuffer[i]);
						if (nread <= 0) {
							if (!isend[i]){
								// do nothing
								return true;
							}
						}else {
							// send server result back to browser
							EditPrintf(hwndEdit, TEXT("Send server react back to browser:"));
							for (unsigned int k = 0; k < linebuffer[i].size(); k++){
								char tmp[MAX_BUF_SIZE] = {};
								if (linebuffer[i][k] == '\n'){
									sprintf(tmp, "%s%d%s", "<script>document.all['m", i, "'].innerHTML += \"<br>\";</script>");
								}
								else {
									sprintf(tmp, "%s%d%s%c%s", "<script>document.all['m", i, "'].innerHTML += \"", linebuffer[i][k], "\";</script>");
								}
								string stmp;
								for (unsigned int m = 0; m < strlen(tmp); m++){
									stmp += tmp[m];
								}
								send(ssock, stmp.c_str(), stmp.size(), 0);
								EditPrintf(hwndEdit, TEXT("%c"), linebuffer[i][k]);
							}
							EditPrintf(hwndEdit, TEXT("Send server react back to browser over:\r\n"));

							/* in case of receiving "% ", force process to enter F_WRITING
							   due to no one trigger FD_READ to write command	
							*/
							if (linebuffer[i].substr(0, 2) == "% "){
								status[i] = F_WRITING;
								i--;
								continue;
							}
						}
						
						if (isend[i]){
							// close a connection
							status[i] = F_DONE;
							closesocket(serverinfo[i].sockfd);
							fclose(serverinfo[i].fileptr);
							conn--;
							EditPrintf(hwndEdit, TEXT("A connection is closed\r\n"));

							// everything is done
							if (conn == 0){
								string buf = "</font>\n</body>\n</html>\n";
								send(ssock, buf.c_str(), buf.size(), 0);
								closesocket(ssock);
								EditPrintf(hwndEdit, TEXT("Everything is done!!!\r\n"));
								return true;
							}
						}
						return true;
					}
				}
			}
		}
		break;
		case FD_WRITE:
			//Write your code for write event here
			break;
		case FD_CLOSE:
			//closesocket(ssock);
			break;
		};
		break;

	default:
		return FALSE;


	};

	return TRUE;
}

int EditPrintf(HWND hwndEdit, TCHAR * szFormat, ...)
{
	
	TCHAR   szBuffer[1024];
	va_list pArgList;

	va_start(pArgList, szFormat);
	wvsprintf(szBuffer, szFormat, pArgList);
	va_end(pArgList);
	
	SendMessage(hwndEdit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
	SendMessage(hwndEdit, EM_REPLACESEL, FALSE, (LPARAM)szBuffer);
	SendMessage(hwndEdit, EM_SCROLLCARET, 0, 0);
	return SendMessage(hwndEdit, EM_GETLINECOUNT, 0, 0);
	
	return 1;
}

int readline(int fd, string &linebuffer){
	int nread = 0;
	char c;
	while (1){
		int n = recv(fd, &c, sizeof(char), 0);		
		if (n == 1){
			nread++;
			linebuffer += c;
			if (c == '\n'){
				break;
			}
		}
		else {
			if (nread == 0){
				return -1;
			}
			else {
				//linebuffer += '\n';
				//nread++;
				return nread;
			}
		}
	}
	return nread;
}
