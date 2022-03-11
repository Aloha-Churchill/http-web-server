/*
** server.c -- a stream socket server demo
** code from Beej's guide to socket programming
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

// for reaping terminated child processes
#include <sys/wait.h>
#include <signal.h>

#define PORT "8888"  			// the port users will be connecting to, it is alternative for HTTP port 80
#define BACKLOG 10	 			// how many pending connections queue will hold
#define REQUEST_SIZE 1024
#define HEADER_SIZE 4000
#define STATUS_SIZE 50
#define COMMAND_LINE_SIZE 100
#define FILE_SIZE_PART 1024


void error(char* message){
    perror(message);
    exit(1);
}

// takes in signal
void sigchld_handler(int s)
{
	(void)s; // quiet unused variable warning

	// waitpid() might overwrite errno, so we save and restore it:
	int saved_errno = errno;

	// -1 means wait for ANY child process
	// WNOHANG = return immediately if no child has exited --> this is why we use waitpid instead of wait
	// placed in loop so that multiple child processes can terminate

	while(waitpid(-1, NULL, WNOHANG) > 0);

	errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


int parse_commands(char* recvbuf, char* parsed_commands[], char* status){
	//strtok replaces delimiter with null terminator then continues to search for next string
	char* element = strtok(recvbuf, " ");

	if(element == NULL){
		error("Could not parse entered command\n");
	}

	int num_input_strings = 0;

	while(element != NULL){
		parsed_commands[num_input_strings] = element;
		element = strtok(NULL, " ");
		num_input_strings += 1;
	}

	if(num_input_strings == 3){
		parsed_commands[2][strcspn(parsed_commands[2], "\n")] = 0;
	}

	// user did not enter in 3 distinct commands
	if(num_input_strings != 3){
		bzero(status, STATUS_SIZE);
		strcpy(status, "400 Bad Request\r\n");
		return -1;
	}

	// user used method other than GET
	if(strcmp(parsed_commands[0], "GET") != 0){
		bzero(status, STATUS_SIZE);
		strcpy(status, "405 Method Not Allowed\r\n");
		return -1;
	}

	// user entered in incorrect HTTP version
	if(strcmp(parsed_commands[2], "HTTP/1.0") != 0 && strcmp(parsed_commands[2], "HTTP/1.1") != 0){
		bzero(status, STATUS_SIZE);
		strcpy(status, "505 HTTP Version Not Supported\r\n");
		return -1;		
	}

	return 0;
}

// wrapper function for send
int send_all(int fd, char* send_buf, int size){
	int n;

	n = send(fd, send_buf, size, 0);
	if(n < 0){
		error("send\n");
	}

	if(n != size){
		while(n < size){
			n += send(fd, send_buf + n, size - n, 0);
		}
	}

	return n;	
	
}


int get_error_status_file(char* pathname, char* status){
	// if everything parsed ok, then we can look for file, return -1 if file not found , 0 otherwise

	FILE* fp = fopen(pathname, "r");

	if(fp == NULL){
		if(errno == 13){
			bzero(status, STATUS_SIZE);
			strcpy(status, "403 Forbidden\r\n");
			return -1;
		}
		if(errno == 2){
			bzero(status, STATUS_SIZE);
			strcpy(status, "404 Not Found\r\n");
			return -1;
		}
		else{
			error("Unexpected error while trying to open file\n");
		}	
	}

	fclose(fp);
	return 0;
}

int get_file_header_info(char* pathname, char* content_length, char* content_type){
	// get file length
	int file_length;
	FILE* fp = fopen(pathname, "r");
	fseek(fp, 0L, SEEK_END);
	file_length = ftell(fp);
	fseek(fp, 0L, SEEK_SET);
	fclose(fp);

	char number_string[10];
	bzero(number_string, 10);
	sprintf(number_string, "%d", file_length);

	strcat(content_length, number_string);
	strcat(content_length, "\r\n");

	// get file type
	char command[COMMAND_LINE_SIZE];
	bzero(command, COMMAND_LINE_SIZE);

	strcpy(command, "file -b ");
	strcat(command, pathname);

	FILE* content_fp = popen(command, "r");

	if(content_fp == NULL){
		error("Could not get file content type\n");
	}

	char content_buf[COMMAND_LINE_SIZE];
	bzero(content_buf, COMMAND_LINE_SIZE);

	if(fgets(content_buf, STATUS_SIZE, content_fp) == NULL){
		error("Could not read file content type\n");
	}

	strcat(content_type, content_buf);
	strcat(content_type, "\r\n\r\n");

	if(pclose(content_fp) == -1){
		error("pclose failed\n");
	}
	
	return file_length;
}



int main(void)
{
	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; //can either be IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; //TCP
	hints.ai_flags = AI_PASSIVE; // use my IP

	// this sets up structures like servinfo for later use
	//servinfo now points to linked list of addrinfos which contain port number,IP address about the host
	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
		error("Could not getaddrinfo\n");
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		// get socket file descriptor using information that we gained in getaddrinfo
		// 
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		// set sockopt -- NEED to read more about what this function does
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		// once you have socket, need to associate it with port on machine
		// ai_addr contains info about port numbers
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	// done with using servinfo to connect
	freeaddrinfo(servinfo); // all done with this structure

	if (p == NULL)  {
		error("Server failed to bind \n");
	}

	//listen: incoming connections go into queue until you accept them
	if (listen(sockfd, BACKLOG) == -1) {
		error("Server failed to listen\n");
	}

	// registering the  SIGCHLD handler
	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	//handle SIGCHLD, which is signal that child sends to parent when it terminates
	// child should terminate upon closed connection
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");

	while(1) {  // main accept() loop

		//accept command - client calls connect() to your machine on port you are listening on
		// their connection goes into queue and waits for acceptance
		// accept gets pending connection and returns new file descriptor to use for this connection
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
		printf("server: got connection from %s\n", s);

		// in child process, fork returns zero
		// we fork process
		//fork command: after fork syscall, both parent and child will execute code immediately following fork

		if (!fork()) { // this is the child process
		//use recv inside here
			close(sockfd); // child doesn't need the listener

			// buffer to store user input in
			char recvbuf[REQUEST_SIZE];
			bzero(recvbuf, REQUEST_SIZE);

			//recv returns -1 on error, 0 if closed connection, or number of bytes read into buffer
			if(recv(new_fd, recvbuf, REQUEST_SIZE, 0) < 0){
				error("Recieve failed\n");
			}

			printf("SERVER RECIEVED: %s\n", recvbuf);

			// default content for header
			char status[STATUS_SIZE];
			bzero(status, STATUS_SIZE);
			strcpy(status, "200 OK\r\n");

			char content_length[STATUS_SIZE];
			bzero(content_length, STATUS_SIZE);
			strcpy(content_length, "Content-Length: ");

			char content_type[STATUS_SIZE];
			bzero(content_type, STATUS_SIZE);
			strcpy(content_type, "Content-Type: ");

			char* parsed_commands[3];

			int input_valid = parse_commands(recvbuf, parsed_commands, status);

			if(input_valid == -1){
				// send http stuff immediately and exit
				strcat(content_length, "\r\n");
				strcat(content_type, "\r\n\r\n");
				send_all(new_fd, status, STATUS_SIZE);
				send_all(new_fd, content_length, STATUS_SIZE);
				send_all(new_fd, content_type, STATUS_SIZE);
			}
			else{
				char pathname[COMMAND_LINE_SIZE];
				bzero(pathname, COMMAND_LINE_SIZE);
				strcpy(pathname, "www");
				strcat(pathname, parsed_commands[1]);

				int file_valid = get_error_status_file(pathname, status);

				if(file_valid == -1){
					// just send header content
					strcat(content_length, "\r\n");
					strcat(content_type, "\r\n\r\n");
					send_all(new_fd, status, STATUS_SIZE);
					send_all(new_fd, content_length, STATUS_SIZE);
					send_all(new_fd, content_type, STATUS_SIZE);

				}
				else{
					// sending file header
					int file_length = get_file_header_info(pathname, content_length, content_type);
					send_all(new_fd, status, STATUS_SIZE);
					send_all(new_fd, content_length, STATUS_SIZE);
					send_all(new_fd, content_type, STATUS_SIZE);

					// send file content
					int num_sends = file_length/FILE_SIZE_PART  + ((file_length % FILE_SIZE_PART) != 0); //taking the ceiling of this
					char file_contents[FILE_SIZE_PART];
					int n, bytes_sent;

					FILE* fp = fopen(pathname, "r");
					for(int i=0; i < num_sends; i++){
						bzero(file_contents, FILE_SIZE_PART);
						n = fread(file_contents, FILE_SIZE_PART, 1, fp);
						if(n < 0){
							error("Error on reading file into buffer\n");
						}
						send_all(new_fd, file_contents, FILE_SIZE_PART);
					
					}
					fclose(fp);

				}
			}
				
			close(new_fd);
			exit(0);

		}
		close(new_fd);  // parent doesn't need this because it is listening for new connections
		//in parent continue listening to new connections
	}

	return 0;
}
