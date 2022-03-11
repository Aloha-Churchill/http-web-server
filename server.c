/*
** server.c -- a stream socket server demo
** code inspired from Beej's guide to socket programming


QUESTION: does client need to exit after
*/

#include "helper_functions.h"


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

	signal(SIGINT, exit_signal_handler);

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
					

					FILE* fp = fopen(pathname, "r");
					for(int i=0; i < num_sends; i++){
						bzero(file_contents, FILE_SIZE_PART);
						int n = fread(file_contents, FILE_SIZE_PART, 1, fp);
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
