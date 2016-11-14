#include <string.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <fcntl.h>
#include <time.h>
#include <dlfcn.h>
#include <link.h>
#include <errno.h>

typedef void (*httprunfunc)(int ssock, const char* querystring);

const char *usage = "\nmyhttp <port>\n\n";

// c is the sorting standard of defalt by name
// o is whether ascending or not
char c = 'N';
char o = 'A';

// Keep track of modules that have been loaded
char **module_List;
int module_Size = 0;
int module_Max = 10;
void **handles;

int QueueLength = 5;

time_t i_time;

// Processes http request
void processHttpRequest( int socket );

// Used in pool of threads to spawn threads
void loopthread(int masterSocket);

// Generate html file of directories to send to client
void dir_gen(DIR *dir, int fd, char *path);

// Sort entries in a directory according to c ('N', 'M', 'S')
void entry_sort(struct dirent **entries, int ascending, int size, char *path);

// Load cgi module
void load_module(int fd, char *name, char *query);

void loopthread(int masterSocket) {
	struct sockaddr_in clientIPAddress;
	int alen = sizeof( clientIPAddress );
	while (1) {
		int slaveSocket = accept( masterSocket,
				(struct sockaddr *)&clientIPAddress,
				(socklen_t*)&alen);

		if ( slaveSocket < 0 ) {
			perror( "accept" );
			exit( -1 );
		}
		processHttpRequest(slaveSocket);
	}
}

void dir_gen(DIR *dir, int fd, char *path) {
	// String buffer of html file sent to client
	char *reply = (char*) malloc(10000);

	// Header of html
	reply[0] = 0;
	strcpy(reply, "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML//EN\">\n<html>\n<head>\n<title>Index of ");
	strcat(reply, path);
	strcat(reply, "</title>\n</head>\n<body>\n<h1>Index of ");
	strcat(reply, path);
	write(fd, reply, strlen(reply));

	// First row of <Name	Last modified	Size	Description>
	// Diffenrent sorting hyper link according to current sorting method
	reply[0] = 0;
	strcpy(reply, "</h1>\n");
	strcpy(reply, "<table><tr><th><a href=\"?C=N;O=");
	if (c == 'N') {
		if (o == 'D')
			strcat(reply, "A");
		else
			strcat(reply, "D");
	}
	else
		strcat(reply, "A");
	strcat(reply, "\">Name</a></th><th><a href=\"?C=M;O=");
	if (c == 'M') {
		if (o == 'D')
			strcat(reply, "A");
		else
			strcat(reply, "D");
	}
	else
		strcat(reply, "A");
	strcat(reply, "\">Last modified</a></th><th><a href=\"?C=S;O=");
	if (c == 'S') {
		if (o == 'D')
			strcat(reply, "A");
		else
			strcat(reply, "D");
	}
	else
		strcat(reply, "A");
	strcat(reply, "\">Size</a></th><th><a href=\"?C=D;O=");
	if (c == 'D') {
		if (o == 'D')
			strcat(reply, "A");
		else
			strcat(reply, "D");
	}
	else
		strcat(reply, "A");
	strcat(reply, "\">Description</a></th></tr><tr><th colspan=\"5\"><hr></th></tr>");
	write(fd, reply, strlen(reply));


	// Sort the entries in the directory
	struct dirent *entries[100];
	int entry_number = 0;
	struct dirent *entry = readdir(dir);
	while (entry != NULL) {
		entries[entry_number++] = entry;
		entry = readdir(dir);
	}

	if (o == 'A')
		entry_sort(entries, 1, entry_number, path);
	else
		entry_sort(entries, 0, entry_number, path);

	// Hyper link for <Parent Directory>
	reply[0] = 0;
	strcpy(reply, "<tr><td><a href=\"");
	strcat(reply, "..");
	strcat(reply, "\">Parent Directory</a></td><td>&nbsp;</td><td align=\"right\">  - </td><td>&nbsp;</td></tr>\n");
	write(fd, reply, strlen(reply));

	// Hyper link for each entry
	int i;
	for (i = 0; i < entry_number; i++) 
		if (entries[i]->d_name[0] != '.') {

			// Get the stat of the entry
			char entry_path[100];
			entry_path[0] = 0;
			strcat(entry_path, path);
			strcat(entry_path, entries[i]->d_name);
			struct stat buffer;
			int status = stat(entry_path, &buffer);
			if (status < 0) {
				perror("stat");
				exit(1);
			}

			// Name field
			reply[0] = 0;
			strcpy(reply, "<tr><td><a href=\"");
			strcat(reply, entries[i]->d_name);

			// Adding '/' to the end of directory entry
			if (S_ISDIR(buffer.st_mode)) {
				strcat(reply, "/");
			}
			strcat(reply, "\">");
			strcat(reply, entries[i]->d_name);

			if (S_ISDIR(buffer.st_mode)) {
				strcat(reply, "/");
			}

			// Time field
			strcat(reply, "</a> </td><td align=\"right\">");
			char time[20];
			strftime(time, 20, "%Y-%m-%d %H:%M:%S", localtime(&buffer.st_mtime));
			strcat(reply, time);

			// Size field
			strcat(reply, "  </td><td align=\"right\">");
			char bytes[20];
			if (buffer.st_size / 1024 == 0)
				sprintf(bytes, "%luB", buffer.st_size);
			else if (buffer.st_size / 1024 > 0 && buffer.st_size / 1024 / 1024 == 0) {
				double size = (double)buffer.st_size / (double)1024;
				sprintf(bytes, "%.1lfKB", size);
			}
			else if (buffer.st_size / 1024 / 1024 > 0 && buffer.st_size / 1024 / 1024 / 1024== 0) {
				double size = (double)buffer.st_size / (double)1024 / (double)1024;
				sprintf(bytes, "%.1lfMB", size);
			}
			strcat(reply, bytes);

			// Description field
			strcat(reply, "</td><td>");
			char suf[10];
			int suf_len = 0;
			int index = strlen(entries[i]->d_name) - 1;
			while (entries[i]->d_name[index] != '.' && index >= 0)
				index--;
			if (index < 0)
				strcat(reply, "&nbsp;");
			else {
				index++;
				while (index < strlen(entries[i]->d_name))
					suf[suf_len++] = entries[i]->d_name[index++];
				suf[suf_len] = 0;
				strcat(reply, suf);
				strcat(reply, " file");
			}

			strcat(reply, "</td></tr>\n");
			write(fd, reply, strlen(reply));
		}

	// End of html file
	strcpy(reply, "<tr><th colspan=\"5\"><hr></th></tr>\n</table>\n</body></html>");
	write(fd, reply, strlen(reply));

	// Free the html buffer
	free(reply);
}

int main(int argc, char **argv) {
	i_time = time(NULL);
	// Prevent zombie process
	struct sigaction sa;
	sa.sa_handler = SIG_DFL;
	sa.sa_flags = SA_NOCLDWAIT;
	sigaction(SIGCHLD, &sa, NULL);

	// Print usage if not enough arguments
	if (argc < 2) {
		fprintf(stderr, "%s", usage);
		exit(-1);
	} 
	// Get the port from the arguments
	int port = argc == 2? atoi(argv[1]) : atoi(argv[2]);
	if (!(port > 1024 && port < 65536)) {
		fprintf(stderr, "incorrect port number.\n");
		exit(1);
	}

	// Initialize module list
	module_List = (char**)malloc(module_Max * 4);
	handles = (void**)malloc(module_Max * 4);

	// Set the IP address and port for this server
	struct sockaddr_in serverIPAddress; 
	memset(&serverIPAddress, 0, sizeof(serverIPAddress));
	serverIPAddress.sin_family = AF_INET;
	serverIPAddress.sin_addr.s_addr = INADDR_ANY;
	serverIPAddress.sin_port = htons((u_short)port);

	// Allocate a socket
	int masterSocket =  socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (masterSocket < 0) {
		perror("socket");
		exit(-1);
	}

	// Set socket options to reuse port. Otherwise we will
	// have to wait about 2 minutes before reusing the same port number
	int optval = 1; 
	int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, 
			(char*)&optval, sizeof( int ) );

	// Bind the socket to the IP address and port
	int error = bind(masterSocket,
			(struct sockaddr *)&serverIPAddress,
			sizeof(serverIPAddress));
	if (error) {
		perror("bind");
		exit(-1);
	}

	// Put socket in listening mode and set the 
	// size of the queue of unprocessed connections
	error = listen(masterSocket, QueueLength);
	if (error) {
		perror("listen");
		exit(-1);
	}

	if (argc > 2) {
		if (strcmp(argv[1], "-p")) {
			while (1) {
				// Accept incoming connections
				struct sockaddr_in clientIPAddress;
				int alen = sizeof(clientIPAddress);
				int slaveSocket = accept(masterSocket,
						(struct sockaddr*)&clientIPAddress,
						(socklen_t*)&alen);

				if (slaveSocket < 0) {
					perror("accept");
					exit(-1);
				}
				if (!strcmp(argv[1], "-f")) {
					int f = fork();
					if (f < 0) {
						perror("Error creating process.\n");
						_exit(1);
					}
					if (f == 0) {
						processHttpRequest(slaveSocket);
						exit(0);
					}
					close(slaveSocket);
				}
				else if (!strcmp(argv[1], "-t")) {
					pthread_t thr;
					pthread_attr_t attr;
					pthread_attr_init(&attr);
					pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
					pthread_create(&thr, &attr, (void *(*)(void *))processHttpRequest, (void *)slaveSocket);
					continue;
				}
			}
		}
		else {
			pthread_t threads[5];
			for (int i = 0; i < 4; i++) {
				pthread_create(&threads[i], NULL, (void * (*)(void *))loopthread, (void *)masterSocket);
			}
			loopthread(masterSocket);
		}
	}
	else {
		while (1) {
			// Accept incoming connections
			struct sockaddr_in clientIPAddress;
			int alen = sizeof(clientIPAddress);
			int slaveSocket = accept(masterSocket,
					(struct sockaddr*)&clientIPAddress,
					(socklen_t*)&alen);

			if (slaveSocket < 0) {
				perror("accept");
				exit(-1);
			}

			// Process request.
			processHttpRequest(slaveSocket);
		}
	}

}

void processHttpRequest(int fd) {
	int length = 0;
	int MaxLength = 1024;
	char request[MaxLength + 1];
	int n;

	// Currently character read
	unsigned char newChar;

	// Last character read
	unsigned char lastChar = 0;

	//
	// The client should send <name><cr><lf>
	// Read the name of the client character by character until a
	// <CR><LF> is found.
	//


	while (length < MaxLength && (n = read(fd, &newChar, sizeof(newChar))) > 0) {
		if (lastChar == '\015' && newChar == '\012') {
			if (length > 1 && request[length - 3] == '\015' && request[length - 2] == '\012') {
				break;
			}
		}
		request[length] = newChar;
		length++;
		lastChar = newChar;
	}

	// Get the requested file name
	n = 0;
	int file_length = 0;
	char file_name[MaxLength];

	while (request[n] != ' ')
		n++;
	n++;

	while (request[n] != ' ')
		file_name[file_length++] = request[n++];
	file_name[file_length] = 0;

	// Root directory
	char file_path[100];
	file_path[0] = 0;
	strcat(file_path, "http-root-dir/htdocs");

	// If file contains "/?" then it's a sorting request
	if (strstr(file_name, "/?") != NULL) {
		char *str = strstr(file_name, "/?");
		if (c == *(str + 4)) {
			if (o == 'A')
				o = 'D';
			else
				o = 'A';
		}
		else
			c = *(str + 4);
		o = *(str + 8);
		strcat(file_path, file_name);
		int i = strlen(file_path);
		while (file_path[i] != '?')
			i--;
		file_path[i] = 0;
		char reply[] = "HTTP/1.1 200 Document follows\nServer\nContent-type: text/html\n\n";
		write(fd, reply, strlen(reply));
		DIR *dir = opendir(file_path);
		if (dir == NULL) {
			perror("Direcory");
			exit(1);
		}
		dir_gen(dir, fd, file_path);
		closedir(dir);
	}
	// Request log
	else if (!strcmp(file_name, "/stats")) {
		char auth[] = "Author: Yifan Wang.\n";
		write(fd, auth, strlen(auth));
		char time_log[] = "Server has been up for ";
		write(fd, time_log, strlen(time_log));
		time_t now = time(NULL);
		char seconds[100];
		sprintf(seconds, "%d seconds.\n", (int) (now - i_time));
		write(fd, seconds, strlen(seconds));
	}
	// Request is default
	else if (!strcmp(file_name, "/")) {
		char reply[] = "HTTP/1.1 200 Document follows\nServer\nContent-type: text/html\n\n";
		write(fd, reply, strlen(reply));
		strcat(file_path, "/index.html");
		FILE *file = fopen(file_path, "rb");
		if (file == NULL) {
			perror("Error opening index.html\n");
			exit(1);
		}
		fseek(file, 0, SEEK_END);
		int file_size = ftell(file);
		fseek(file, 0, SEEK_SET);
		off_t offset = 0;
		int err = sendfile(fd, fileno(file), &offset, file_size);
		if (err < 0) {
			perror("Error sending file.\n");
			exit(1);
		}
	}
	// Request is cgi
	else if (file_name[1] == 'c' && file_name[2] == 'g' 
			&& file_name[3] == 'i' && file_name[4] == '-' && file_name[5] == 'b'
			&& file_name[6] == 'i' && file_name[7] == 'n' && file_name[8] == '/') {
		int index = strlen(file_name) - 1;
		while (file_name[index] != '?' && index >= 0)
			index--;
		// If no query exists
		if (index < 0) {

			// Loadable Modules
			int len = strlen(file_name);
			if (file_name[len - 3] == '.' && file_name[len - 2] == 's' && file_name[len - 1] == 'o') {
				load_module(fd, file_name, NULL);
			}
			else {
				char pwd[100];
				pwd[0] = 0;
				strcat(pwd, getenv("PWD"));
				int pid = fork();
				if (pid == 0){
					file_path[0] = 0;
					strcat(file_path, pwd);
					strcat(file_path, "/http-root-dir");
					setenv("REQUEST_METHOD", "GET", 1);
					dup2(fd, 1);
					char reply[] = "HTTP/1.1 200 Document follows\nServer: Server-Type\n";
					write(fd, reply, strlen(reply));
					close(fd);
					char *args[2];
					strcat(file_path, file_name);
					args[0] = file_path;
					args[1] = NULL;
					execvp(file_path, args);
					perror("child process");
					_exit(1);
				}
				else
					wait();
			}
		}
		// If there exists a query
		else {
			int pid = fork();
			char script[100];
			int s_len = 0;
			int i;
			for (i = 0; i < index; i++)
				script[s_len++] = file_name[i];
			script[s_len] = 0;

			char query[100];
			int query_len = 0;
			for (i = index + 1; i < strlen(file_name); i++)
				query[query_len++] = file_name[i];
			query[query_len] = 0;

			int len = strlen(script);
			// Loadable Modules
			if (script[len - 3] == '.' && script[len - 2] == 's' && script[len - 1] == 'o') {
				load_module(fd, script, query);
			}
			else {
				char pwd[100];
				pwd[0] = 0;
				strcat(pwd, getenv("PWD"));
				if (pid == 0) {
					file_path[0] = 0;
					strcat(file_path, pwd);
					strcat(file_path, "/http-root-dir");
					strcat(file_path, script);
					setenv("REQUEST_METHOD", "GET", 1);
					setenv("QUERY_STRING", query, 1);
					dup2(fd, 1);
					char reply[] = "HTTP/1.1 200 Document follows\nServer: Server-Type\n";
					write(fd, reply, strlen(reply));
					close(fd);
					char *args[2];
					args[0] = file_path;
					args[1] = NULL;
					execvp(file_path, args);
					perror("child process");
					_exit(1);
				}
				else
					wait();
			}
		}
	}
	// Request regular file
	else {
		strcat(file_path, file_name);
		struct stat buffer;
		int status = stat(file_path, &buffer);
		// If no such file exists
		if (status < 0) {
			char reply[] = "HTTP/1.1 404 File Not Found\nServer\nContent-type: text\n\nPage Not Found.\n";
			write(fd, reply, strlen(reply));
		}
		// If requested file is a directory
		else if (S_ISDIR(buffer.st_mode)) {
			if (file_path[strlen(file_path) - 1] != '/') {
				int len = strlen(file_path);
				file_path[len] = '/';
				file_path[len + 1] = 0;
			}
			char reply[] = "HTTP/1.1 200 Document follows\nServer\nContent-type: text/html\n\n";
			write(fd, reply, strlen(reply));
			DIR *dir = opendir(file_path);
			if (dir == NULL) {
				perror("Direcory");
				exit(1);
			}
			dir_gen(dir, fd, file_path);
			closedir(dir);
		}
		// If requested file is not a directory
		else {
			FILE *file = fopen(file_path, "r");
			if (file == NULL) {
				char reply[] = "HTTP/1.1 404 File Not Found\nServer\nContent-type: text\n\nPage Not Found.\n";
				write(fd, reply, strlen(reply));
			}
			else {
				int dot = file_length - 1;
				while (file_name[dot] != '.')
					dot--;
				char *reply;

				// Decide content type based on the suffix of the file
				if (file_name[dot + 1] == 'g' && file_name[dot + 2] == 'i' && file_name[dot + 3] == 'f')
					reply = "HTTP/1.1 200 Document follows\nServer\nContent-type: image/gif\n\n";
				else if (file_name[dot + 1] == 's' && file_name[dot + 2] == 'v' && file_name[dot + 3] == 'g') 
					reply = "HTTP/1.1 200 Document follows\nServer\nContent-type: image/svg+xml\n\n";
				else if (file_name[dot + 1] == 'h' && file_name[dot + 2] == 't' && file_name[dot + 3] == 'm' && file_name[dot + 4] == 'l')
					reply = "HTTP/1.1 200 Document follows\nServer\nContent-type: text/html\n\n";
				else
					reply = "HTTP/1.1 200 Document follows\nServer\nContent-type: text/plain\n\n";
				write(fd, reply, strlen(reply));
				fseek(file, 0, SEEK_END);
				int file_size = ftell(file);
				fseek(file, 0, SEEK_SET);
				off_t offset = 0;
				int err = sendfile(fd, fileno(file), &offset, file_size);
				if (err < 0 || err != file_size) {
					perror("Error sending file.\n");
					exit(1);
				}
			}
		}
	}

	close(fd);
}

void entry_sort(struct dirent **entries, int ascending, int size, char *path) {
	int i,j;
	for (i = 1; i < size; i++) {
		for (j = i; j > 0; j--) {
			switch (c) {
				case 'N': {
					if (ascending) {
						if (strcmp(entries[j]->d_name, entries[j - 1]->d_name) < 0) {
							struct dirent *tmp = entries[j];
							entries[j] = entries[j - 1];
							entries[j - 1] = tmp;
						}
					}
					else {
						if (strcmp(entries[j]->d_name, entries[j - 1]->d_name) > 0) {
							struct dirent *tmp = entries[j];
							entries[j] = entries[j - 1];
							entries[j - 1] = tmp;
						}
					}
					break;
				}
				case 'M': { 
					struct stat buffer1;
					struct stat buffer2;
					char entry_path1[1000] = "";
					char entry_path2[1000] = "";
					strcat(entry_path1, path);
					strcat(entry_path2, path);
					strcat(entry_path1, entries[j]->d_name);
					strcat(entry_path2, entries[j - 1]->d_name);
					int status = stat(entry_path1, &buffer1);
					if (status < 0) {
						perror("directory entry");
						exit(1);
					}
					status = stat(entry_path2, &buffer2);
					if (status < 0) {
						perror("directory entry");
						exit(1);
					}
					if (ascending) {
						if (difftime(buffer1.st_mtime, buffer2.st_mtime) < 0) {
							struct dirent *tmp = entries[j];
							entries[j] = entries[j - 1];
							entries[j - 1] = tmp;
							}
					}
					else {
						if (difftime(buffer1.st_mtime, buffer2.st_mtime) > 0) {
							struct dirent *tmp = entries[j];
							entries[j] = entries[j - 1];
							entries[j - 1] = tmp;
						}
					}
					break;
				}
				case 'S': {
					struct stat buffer1;
					struct stat buffer2;
					char entry_path1[1000] = "";
					char entry_path2[1000] = "";
					strcat(entry_path1, path);
					strcat(entry_path2, path);
					strcat(entry_path1, entries[j]->d_name);
					strcat(entry_path2, entries[j - 1]->d_name);
					int status = stat(entry_path1, &buffer1);
					if (status < 0) {
						perror("directory entry");
						exit(1);
					}
					status = stat(entry_path2, &buffer2);
					if (status < 0) {
						perror("directory entry");
						exit(1);
					}
					if (ascending) {
						if (buffer1.st_size < buffer2.st_size) {
							struct dirent *tmp = entries[j];
							entries[j] = entries[j - 1];
							entries[j - 1] = tmp;
						}
					}
					else {
						if (buffer1.st_size > buffer2.st_size) {
							struct dirent *tmp = entries[j];
							entries[j] = entries[j - 1];
							entries[j - 1] = tmp;
						}
					}
					break;
				}
			}
		}
	}
}

void load_module(int fd, char *name, char *query) {
	int i; 
	int found = 0;
	int index = 0;
	char reply[] = "HTTP/1.1 200 Document follows\nServer\n";
	write(fd, reply, strlen(reply));
	for (i = 0; i < module_Size; i++)
		if (!strcmp(module_List[i], name)) {
			found = 1;
			index = i;
			break;
		}
	if (found) {
		httprunfunc httprun;
		httprun = (httprunfunc)dlsym(handles[index], "httprun");
		if (httprun == NULL) {
			perror("httprun");
			exit(1);
		}
		httprun(fd, query);
	}
	else {
		if (module_Size + 1 == module_Max) {
			module_Max *= 2;
			module_List = (char**) realloc(module_List, module_Max);
			handles = (void**) realloc(handles, module_Max);
		}
		module_List[module_Size] = strdup(name);
		char path[100];
		path[0] = 0;
		strcat(path, "http-root-dir/");
		strcat(path, name);
		void *handle = dlopen(path, RTLD_LAZY);
		if (handle == NULL) {
			perror(name);
			exit(1);
		}
		handles[module_Size++] = handle;
		httprunfunc httprun;
		httprun = (httprunfunc)dlsym(handle, "httprun");
		if (httprun == NULL) {
			perror("httprun");
			exit(1);
		}
		httprun(fd, query);
	}
}
