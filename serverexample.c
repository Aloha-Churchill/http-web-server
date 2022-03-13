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


// todo - allow user to assign port
// correct http version
// wgets
// clean and comment code
// create header file

#define REQUEST_SIZE 4000
#define PATHNAME_SIZE 256
#define FILE_SIZE_PART 1024


int server_fd;


void error(char* message){
    perror(message);
    exit(1);
}

void shut_down_server_handler(int signal) {
    close(server_fd);
    exit(0);
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

// takes in signal
void sigchld_handler(int s)
{
	(void)s; // quiet unused variable warning
	int saved_errno = errno;
	while(waitpid(-1, NULL, WNOHANG) > 0);
	errno = saved_errno;
}

int parse_commands(char* recvbuf, char* parsed_commands[]){
	//strtok replaces delimiter with null terminator then continues to search for next string
	const char delimiters[] = " \r\n";
	char* element = strtok(recvbuf, delimiters);
	int num_input_strings = 0;

	while(element != NULL){
		if(num_input_strings < 3){
			parsed_commands[num_input_strings] = element;
		}
		printf("%d - PARSED: %s\n", num_input_strings, element);
		element = strtok(NULL, delimiters);
		num_input_strings += 1;
	}

    return num_input_strings;

}

int check_request(int fd, char* parsed_commands[], int num_input_strings){
    // malformed request

    if(num_input_strings < 3){
        //dprintf(fd, "%s ", parsed_commands[2]);
        dprintf(fd, "HTTP/1.1 400 Bad Request\r\n");
        dprintf(fd, "Content-Type: \r\n");
        dprintf(fd, "Content-Length: \r\n\r\n");
        return -1;
    }

    
    // user entered in incorrect method
    if(strcmp(parsed_commands[0], "GET") != 0){
        //dprintf(fd, "%s ", parsed_commands[2]);
        dprintf(fd, "HTTP/1.1 405 Method Not Allowed\r\n");
        dprintf(fd, "Content-Type: \r\n");
        dprintf(fd, "Content-Length: \r\n\r\n");
        return -1;
	}

	// user entered in incorrect HTTP version
	if(strncmp(parsed_commands[2], "HTTP/1.0", 8) != 0 && strncmp(parsed_commands[2], "HTTP/1.1", 8) != 0){
        //dprintf(fd, "%s ", parsed_commands[2]);
        dprintf(fd, "HTTP/1.1 505 HTTP Version Not Supported\r\n");
        dprintf(fd, "Content-Type: \r\n");
        dprintf(fd, "Content-Length: \r\n\r\n");
        return -1;		
	}
    return 0;
}

int check_file(int fd, char* pathname){
    FILE* fp = fopen(pathname, "r");

	if(fp == NULL){
		if(errno == 13){
            dprintf(fd, "HTTP/1.1 403 Forbidden\r\n");
            dprintf(fd, "Content-Type: \r\n");
            dprintf(fd, "Content-Length: \r\n\r\n");
            return -1;	
		}
		if(errno == 2){
            dprintf(fd, "HTTP/1.1 404 Not Found\r\n");
            dprintf(fd, "Content-Type: \r\n");
            dprintf(fd, "Content-Length: \r\n\r\n");
            return -1;	
		}
		else{
			error("Unexpected error while trying to open file\n");
		}	
	}

	fclose(fp);
    return 0;
}

// code from stackoverflow
char *strrev(char *str)
{
      char *p1, *p2;

      if (! str || ! *str)
            return str;
      for (p1 = str, p2 = str + strlen(str) - 1; p2 > p1; ++p1, --p2)
      {
            *p1 ^= *p2;
            *p2 ^= *p1;
            *p1 ^= *p2;
      }
      return str;
}

int send_file(int fd, char* pathname){
    // get file length
	int file_length;
	FILE* fp;
    fp = fopen(pathname, "r");
	fseek(fp, 0L, SEEK_END);
	file_length = ftell(fp);
	fseek(fp, 0L, SEEK_SET);
	//fclose(fp);

	char number_string[10];
	bzero(number_string, 10);
	sprintf(number_string, "%d", file_length);

    char content_length[PATHNAME_SIZE];
    bzero(content_length, PATHNAME_SIZE);
    strcpy(content_length, "Content-Length: ");
	strcat(content_length, number_string);
	strcat(content_length, "\r\n\r");


    char content_type[PATHNAME_SIZE];
    bzero(content_type, PATHNAME_SIZE);
    // get content type
	const char delimiters[] = ".";
	char* element = strtok(strrev(pathname), delimiters);
	element = strrev(element);
	printf("CONTENT TYPE IS: %s\n", element);

	if(element == NULL){
		error("Not a valid file format\n");
	}
	else{
		if(strcmp(element, "html") == 0){
			strcpy(content_type, "Content-Type: text/html\r");
		}
		else if(strcmp(element, "txt") == 0){
			strcpy(content_type, "Content-Type: text/plain\r");	
		}
		else if(strcmp(element, "png") == 0){
			strcpy(content_type, "Content-Type: image/png\r");
		}
		else if(strcmp(element, "gif") == 0){
			strcpy(content_type, "Content-Type: image/gif\r");
		}
		else if(strcmp(element, "jpg") == 0){
			strcpy(content_type, "Content-Type: image/jpg\r");
		}
		else if(strcmp(element, "css") == 0){
			strcpy(content_type, "Content-Type: text/css\r");
		}
		else if(strcmp(element, "js") == 0){
			strcpy(content_type, "Content-Type: application/javascript\r");
		}
        else{
            strcpy(content_type, "Content-Type: unsupported\r");
        }
	}

    printf("HTTP/1.1 200 OK\r\n");
    printf("%s\n", content_type);
    printf("%s\n", content_length);

    dprintf(fd, "HTTP/1.1 200 OK\r\n");
    dprintf(fd, "%s\n", content_type);
    dprintf(fd, "%s\n", content_length);

    if(fp == NULL){
        printf("bad fp\n");
    }
    fseek(fp, 0, SEEK_SET);
    int num_sends = file_length/FILE_SIZE_PART  + ((file_length % FILE_SIZE_PART) != 0); //taking the ceiling of this
    char file_contents[FILE_SIZE_PART];

    for(int i=0; i < num_sends; i++){
        bzero(file_contents, FILE_SIZE_PART);
        int n = fread(file_contents, FILE_SIZE_PART, 1, fp);
        if(n < 0){
            error("Error on reading file into buffer\n");
        }
        if(i == num_sends-1){
            send_all(fd, file_contents, file_length % FILE_SIZE_PART);
        }
        else{
            send_all(fd, file_contents, FILE_SIZE_PART);
        }
        
    
    }

    fclose(fp);
    return 0;
    


}


void handle_client(int fd) {
    char recvbuf[REQUEST_SIZE];
    bzero(recvbuf, REQUEST_SIZE);

    //recv returns -1 on error, 0 if closed connection, or number of bytes read into buffer
    if(recv(fd, recvbuf, REQUEST_SIZE, 0) < 0){
        error("Recieve failed\n");
    }

    printf("SERVER RECIEVED: %s\n", recvbuf);
    // need to parse commands
    char* parsed_commands[3]; // [[method],[url],[http version]]
    int num_parsed = parse_commands(recvbuf, parsed_commands);

    char pathname[PATHNAME_SIZE];
    bzero(pathname, PATHNAME_SIZE);

    if(check_request(fd, parsed_commands, num_parsed) != -1){
        strcpy(pathname, "www");
        strcat(pathname, parsed_commands[1]);

        if(check_file(fd, pathname) != -1){
            // then we can send file
            printf("NOW WE CAN SEND THE FILE\n");
            send_file(fd, pathname);

            //  // Get our response ready.
            // char *data = "Hello world.\n";
            // int size = strlen(data);
        
            // // Start the response.
            // //dprintf(fd, "HTTP/1.1 200 OK\r\n");
            // //dprintf(fd, "Content-Type: text/html\r\n");
            // //dprintf(fd, "Content-Length: %d\r\n\r\n", size);
        
            // // Loop until we're finished writing.
            // ssize_t bytes_sent;
            // while (size > 0) {
            //     bytes_sent = write(fd, data, size);
            //     if (bytes_sent < 0)
            //         return;
            //     size -= bytes_sent;
            //     data += bytes_sent;
            // }
        }  
    }

   
}

void start_server(int *server_socket) {
    // Specify the port.
    // DO NOT HARD CODE THIS IN!!!! -----------------------------------------
    int port = 8888;
 
    // Set up the socket for the server.
    *server_socket = socket(PF_INET, SOCK_STREAM, 0);
 
    // Allow reuse of local addresses.
    int socket_option = 1;
    setsockopt(*server_socket, SOL_SOCKET, SO_REUSEADDR, &socket_option, 
        sizeof(socket_option));
 
    // Set up the server address struct.
    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port);
 
    // Assign the address to the socket.
    bind(*server_socket, (struct sockaddr *) &server_address, 
        sizeof(server_address));
 
    // Start listening.
    listen(*server_socket, 1024);

    // registering the  SIGCHLD handler
    struct sigaction sa;
	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	//handle SIGCHLD, which is signal that child sends to parent when it terminates
	// child should terminate upon closed connection
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}
 
    // Initialize the relevant variables to handle client sockets.
    struct sockaddr_in client_address;
    size_t client_address_length = sizeof(client_address);
    int client_socket;
 
    // Start the server loop.
    while (1) {
        // Accept the client socket.
        client_socket = accept(*server_socket,
            (struct sockaddr *) &client_address,
            (socklen_t *) &client_address_length);
 
        if(!fork()){
            close(*server_socket);
            handle_client(client_socket);
            close(client_socket);
            exit(0);
        }
        
        close(client_socket);
    }
 
    // Shut down the server.
    shutdown(*server_socket, SHUT_RDWR);
    close(*server_socket);
}



int main(int argc, char **argv) {
    // Make sure we shut down the server gracefully.
    signal(SIGINT, shut_down_server_handler);
 
    start_server(&server_fd);
}