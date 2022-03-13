# Simple HTTP Server

This is a simple HTTP file server. The contents that it serves are under the www/ folder. 


## Run Server
First, start the server using

```make all```
and then 

```make run``` or ```./server [PORTNO]```

#### Browser
Once the server has started, go to a broswer and type http://localhost:[PORTNO]/index.html
Then, click on any of the links.
#### Netcat
Enter
```nc localhost PORTNO```
and then type your command.

Command should be format: [GET] [FILEPATH] [HTTP_VERSION]

Command example: GET /index.html HTTP/1.0
#### Telnet
Enter
```telnet localhost PORTNO```
and then type your command.

Command should be format: [GET] [FILEPATH] [HTTP_VERSION]

Command example: GET /index.html HTTP/1.0

Finally, run
```make clean```

To remove the executable.