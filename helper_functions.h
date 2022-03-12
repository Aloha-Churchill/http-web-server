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

    // gracefully handles Ctrl+C
void exit_signal_handler(int sig){
    printf("Gracefully exiting server, recieved signal number %d...\n", sig);
    exit(0);
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
	const char delimiters[] = " \r\n";
	char* element = strtok(recvbuf, delimiters);

	if(element == NULL){
		// THIS SHOULD NOT TACTUALLY BE ERROR, SHOULD JUST BE CANNOT PARSE
		bzero(status, STATUS_SIZE);
		strcpy(status, "\n400 Bad Request\r\n");
		return -1;
	}

	int num_input_strings = 0;

	while(element != NULL){
		if(num_input_strings < 3){
			parsed_commands[num_input_strings] = element;
		}
		printf("pc: %s\n", element);
		element = strtok(NULL, delimiters);
		num_input_strings += 1;
	}



	char http_version_buf[9];
	bzero(http_version_buf, 9);

	printf("METHOD: %s\n", parsed_commands[0]);
	printf("URL: %s\n", parsed_commands[1]);
	printf("VERSION: %s\n", parsed_commands[2]);

	if(num_input_strings >= 3){
		// test using valgrind
		if(strlen(parsed_commands[2]) < 8){
			bzero(status, STATUS_SIZE);
			strcpy(status, "\n400 Bad Request\r\n");
			return -1;
		}

		strncpy(http_version_buf, parsed_commands[2], 8);

	}

	// user did not enter in 3 distinct commands
	if(num_input_strings <= 3){
		bzero(status, STATUS_SIZE);
		strcpy(status, "\n400 Bad Request\r\n");
		return -1;
	}

	// user used method other than GET
	if(strcmp(parsed_commands[0], "GET") != 0){
		bzero(status, STATUS_SIZE);
		strcpy(status, "\n405 Method Not Allowed\r\n");
		return -1;
	}

	// user entered in incorrect HTTP version
	if(strncmp(http_version_buf, "HTTP/1.0", 8) != 0 && strncmp(http_version_buf, "HTTP/1.1", 8) != 0){ //strcmp(parsed_commands[2], "HTTP/1.0\t") != 0 && strcmp(parsed_commands[2], "HTTP/1.1\t") != 0
		bzero(status, STATUS_SIZE);
		strcpy(status, "\n505 HTTP Version Not Supported\r\n");
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
			strcpy(status, "\n403 Forbidden\r\n");
			return -1;
		}
		if(errno == 2){
			bzero(status, STATUS_SIZE);
			strcpy(status, "\n404 Not Found\r\n");
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

