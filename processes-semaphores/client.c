#include <stdio.h>	// standard input/output
#include <stdlib.h>	// general purpose functions
#include <string.h>	// string operations
#include <unistd.h>	// miscellaneous functions
#include <sys/un.h>	// for sockaddr_un structure
#include <sys/socket.h>	// socket definitions
#include <sys/types.h>	// various type definitions
#include <sys/wait.h>	// for the waitpid function
#include <signal.h>	// for handling signals

#define MAX 16		// max size for small buffers
#define MAXBUF 128	// max size for large buffers

int server;		// server file descriptor
char name[MAX];		// player's name
char inv_file[MAX];	// inventory file
char server_name[MAX];	// server hostname

void read_inventory(char *, char *);	// reads inventory file
void init_player(void);		// connects player with server
void terminate(void);		// kills player
void sig_chld(int);		// no zombie processes
void send_request(void);	// sends player's request to server
void cl_write(void);		// waits for input and sends
void cl_read(void);		// waits for recv() and prints

// ./player -n kos_n -i inventory_n server

int main(int argc, char *argv[]) {
	pid_t pid;	// fork() return value

	/* checks if all arguments are OK */
	if (argc != 6) {
		printf("Start playing by writing:\n");
		printf("./player –n <name> -i <inventory> <server_host>\n");
		exit(1);
	}

	if (!strcmp(argv[1], "-n")) {
		strncpy(name, argv[2], strlen(argv[2]));	// player's name
	}
	else {
		printf("Argument 1 must be -n\n"); exit(1);
	}

	if (!strcmp(argv[3], "-i")) {
		strncpy(inv_file, argv[4], strlen(argv[4]));	// inventory
	}
	else {
		printf("Argument 3 must be -i\n"); exit(1);
	}
	strncpy(server_name, argv[5], strlen(argv[5]));		// server hostname


	init_player();	// connect to server
	send_request();	// send request to server

	/* if connected, start asynchronous I/O */
	pid = fork();	// create child process

	if (pid == -1) {
		perror("fork()\nerrno"); exit(1);	// debugging
	}
	if (pid != 0) { // parent process
		cl_read();	// parent reads
	}
	else {			// child process
		cl_write();	// child writes
	}

	return 0;	// unreachable
}

void init_player() {
	struct sockaddr_un srv_addr;	// Unix domain sockets

	signal(SIGCHLD, sig_chld);	// no zombies allowed

	/* set all bytes to 0 */
	memset(&srv_addr, 0, sizeof(struct sockaddr_un));
	srv_addr.sun_family = AF_UNIX; // Local
	strncpy(srv_addr.sun_path, server_name,
			sizeof(srv_addr.sun_path) - 1);	// server hostname

	if ((server = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket()\nerrno"); exit(1);	// debugging
	}

	if (connect(server, (struct sockaddr *) &srv_addr, sizeof(struct sockaddr)) == -1)  
	{  
		perror("connect()\nerrno"); exit(1);	// debugging
	}

	/******* player connected to server! *******/
	printf("%s connected to server\n", name);
}

void sig_chld(int signo) {
	signal(SIGCHLD, sig_chld);	// set signal handler again

	pid_t pid;
	int stat;
	while( (pid = waitpid(-1, &stat, WNOHANG) ) > 0);
}

void terminate(void) {	// server ctr-c or crash
	printf("\n\nServer closed..\n\n");
	_exit(1);	// kill player
}

void send_request() {
	char mes[MAXBUF];	// player's request
	int ready=0;		// flag for START

	memset(mes, 0, MAXBUF);			// set string to 0 (\0)
	read_inventory(inv_file, mes);		// reads player's request
	send(server, mes, strlen(mes), 0);	// send request

	if (recv(server, mes, MAXBUF, 0) == 0) {
		terminate();		// server crashes
	}
	if (strcmp(mes, "OK\n")) {
		printf("%s", mes);
		exit(1);		// server does not approve
	}
	else printf("%s", mes);	// OK

	while (!ready) {		// wait for START signal
		memset(mes, 0, MAXBUF);	// set string to \0
		if (recv(server, mes, MAXBUF, 0) == 0) {
			terminate();	// server crashes
		}
		if (!strcmp(mes, "START\n")) {
			ready = 1;	// game starts!
		}
		printf("%s", mes);	// print server's message
	}
}	// player is OK, game starts!

void read_inventory(char *fname, char *mes) {
	FILE *fp;
	int len;		// file's length (bytes)
	char inv_name[MAX];	// player's name (first line in inventory)

	if ((fp = fopen(fname, "r")) == NULL) {			// open file
		perror("File does not exist\nerrno"); exit(1);	// debugging
	}
	fseek(fp, 0, SEEK_END);	// set position indicator to EOF
	len = ftell(fp);	// total chars
	rewind(fp);		// set position indicator to beginning

	if ( fscanf(fp, "%s\n", inv_name) == EOF || strcmp(name, inv_name)) {
		/* argv[2] must be the same with inventory's first line */
		perror("Wrong inventory\nerrno"); exit(1);
	}

	rewind(fp);				// set indicator to beginning			
	fread(mes, len, sizeof(char), fp);	// read message!
	fclose(fp);				// close file
}	// reading inventory file complete!

void cl_write() {	// constant writing
	char mes[MAXBUF];		// player's message
	while (1) {			// always waits for input
		memset(mes, 0, MAXBUF);		// set buffer to \0
		fgets(mes, MAX-1, stdin);	// read message from stdin
		send(server, mes, strlen(mes), 0);	// send to server!
	}
}

void cl_read() {	// constant reading
	char mes[MAXBUF];	// server's message
	while(1) {		// always waits for message from server
		memset(mes, 0, MAXBUF);	// set bytes to 0
		if (recv(server, mes, MAXBUF, 0) == 0) {
			terminate();	// server crashes
		}
		printf("%s", mes);	// print server's message!
	}
}
