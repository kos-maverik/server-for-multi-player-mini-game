#include <stdio.h>	// standard input/output
#include <stdlib.h>	// general purpose functions
#include <string.h>	// string operations
#include <unistd.h>	// miscellaneous functions
#include <sys/un.h>	// for sockaddr_un structure
#include <sys/socket.h>	// socket definitions
#include <sys/types.h>	// various type definitions
#include <sys/shm.h>	// shared memory
#include <sys/wait.h>	// for the waitpid function
#include <signal.h>	// for handling signals
#include <semaphore.h>	// semaphores
#include <fcntl.h>	// file control options
#include <errno.h>	// for the errno variable

#define PATH "server"		// server hostname
#define MAX 16			// max size for small buffers
#define MAXBUF 128		// max size for large buffers
#define MAXLISTEN 50		// max queue length for listen
#define SEMNAME1 "sem_name"	// named semaphore for struct shm
#define SEMNAME2 "sem_name2"	// named semaphore for chat

int shm_id;	// shared memory id
key_t shm_key;	// shared memory key
pid_t mainpid;	// main process id (parent)
FILE *fp;		// file object
int server;		// server file descriptor

sem_t *sem_id;		// semaphore for struct shm
sem_t *sem_id2;		// semaphore for chatting
int maxplayers;		// max players per game
char inv_file[MAX];	// server inventory file
int quota;		// max resources per player

/* games are implemented using linked lists */
/* "n" node of the list contains data for the "n" game */
typedef struct game_t {		// everything for each game
	int inv[6];		// resources (inventory)
	int players[MAX];	// players' file descriptors
	char names[MAX][MAX];	// players' names
	int active;		// active players in game
	struct game_t *next;	// next game

	int temp_shm;		// used to clear shared memory segments
} *game_t;

struct shm_t {			// shared memory segment
	game_t game;		// first game
	int game_num;		// number of games

	/* chatting */
	char message[MAXBUF];	// message to send
	int client;		// message source
	int gamenum;		// which game to send the message
} *shm;

void terminate(int);		// signal handler for ctrl-c
void destroy_everything(void);	// clear memory, close server
void show_info(int);		// pretty info, handler for ctrl-z
void sig_chld(int);		// no zombie processes
void send_msg(int);		// signal for chatting
void init_server(void);		// start server
game_t get_game(int);		// get current game
void read_inventory(char *);	// read server's inventory file
int resource_id(char *);	// hashing function for resources
void action(int);		// does everything for the player
int insert_player(int, char *);	// connect a player with the server
void remove_player(int, int);	// kills player

// ./gameserver -p 3 -i inventory -q 5

int main(int argc, char *argv[]) {
	int new_fd;			// player's file descriptor
	pid_t pid;			// process id, fork return value
	socklen_t addr_size;		// size of address
	struct sockaddr_un cl_addr;	// Unix domain sockets

	/* checks if all arguments are OK */
	/* maxplayers must be < MAX, for static memory management */
	if (argc != 7 || atoi(argv[2]) > MAX) {
		printf("Run the server by writing:\n");
		printf("./gameserver –p <num_of_players> -i <game_inventory> -q <quota_per_player>\n");
		exit(1);
	}

	if (!strcmp(argv[1], "-p")) {
		maxplayers = atoi(argv[2]);	// max players
	}
	else {
		printf("Argument 1 must be -p\n"); exit(1);
	}

	if (!strcmp(argv[3], "-i")) {
		strncpy(inv_file, argv[4], strlen(argv[4]));	// inventory file
	}
	else {
		printf("Argument 3 must be -i\n"); exit(1);
	}

	if (!strcmp(argv[5], "-q")) {
		quota = atoi(argv[6]);		// quota
	}
	else {
		printf("Argument 5 must be -q\n"); exit(1);
	}

	init_server();	// start server!
	
	printf("\n~~~~~ Server Started! ~~~~~\n");
	printf("\n~~~ Press Ctrl-Z to view games and inventories! ~~~\n\n");

	addr_size = sizeof(struct sockaddr_un);
	while (1) {
		if ((new_fd = accept(server, (struct sockaddr *) &cl_addr, &addr_size)) == -1) {
			perror("accept()\nerrno"); exit(1);	// debugging
		}

		if ((pid = fork()) == -1) {
			perror("fork()\nerrno"); exit(1);	// debugging
		}

		if (pid == 0) { 		// child
			close(server);		// no longer needed
			action(new_fd);		// does everything
		}

	}

	return 0;	// unreachable
}

void destroy_everything() {	// clear memory and remove files
	int i;
	char buf[MAX];	// buffer

	for (i=0; i<shm->game_num; i++) {
		shm_id = get_game(i+1)->temp_shm;
		shmctl (shm_id , IPC_RMID , 0);	// clear shared memory

		snprintf(buf, MAX, "%d", i);
		remove(buf);			// remove files
	}
	snprintf(buf, MAX, "%d", shm->game_num);
	remove(buf);			// remove last file

	sem_close(sem_id);		// close first semaphore
	sem_close(sem_id2);		// close second semaphore
	sem_unlink(SEMNAME1);	// remove named semaphore
	sem_unlink(SEMNAME2);	// remove named semaphore
	remove(PATH);			// remove server file
}

void terminate(int signo) {		// close server!
	if (getpid() == mainpid) {	// main process (parent)
		printf("\n~~~~~ Server Closing! ~~~~~\n\n");
		destroy_everything();	// destroy everything!
		usleep(100000);		// wait for child processes
	}
	_exit(0);	// kill all processes
}

void send_msg(int signo) {
	int i, cl;
	game_t g;			// game to which to send the message
	signal(SIGUSR1, send_msg);	// set signal handler

	if (getpid() == mainpid) {	// server sends the message
		g = get_game(shm->gamenum);
		cl = shm->client;

		for (i=0; i<maxplayers; i++) {
			/* sends the message to all other players of the same game */
			if (g->players[i] != 0 && g->players[i] != cl) {
				send(g->players[i], shm->message, MAXBUF, 0); // send!
			}
		}
	}
}

void show_info(int signo) {		// pretty function
	game_t g;
	int i, j;
	int empty;

	if (getpid() == mainpid) {	// main process shows the info
		for (i=0; i<shm->game_num; i++) {
			g = get_game(i+1);		// get game
			printf("\n~~~~~ GAME %d ~~~~~ \n", i+1);
			printf("\nOnline players :\n");
			empty = 1;			// flag for empty game
			for (j=0; j<maxplayers; j++) {
				if (g->players[j]) {
					empty = 0;	// game is not empty
					printf("%s\n", g->names[j]);
				}
			}
			if (empty) {			// game is empty..
				printf("No online players..\n");
			}
			/* inventory for each game */
			printf("\nInventory [ %d ] :\n", i+1);
			printf("Gold : %d\n", g->inv[0]);
			printf("Armor : %d\n", g->inv[1]);
			printf("Ammo : %d\n", g->inv[2]);
			printf("Lumber : %d\n", g->inv[3]);
			printf("Magic : %d\n", g->inv[4]);
			printf("Rock : %d\n", g->inv[5]);
		}
		printf("\n~~~ That's all! ~~~\n\n");
	}
}

void sig_chld(int signo) {
	signal(SIGCHLD, sig_chld);	// set signal handler

	pid_t pid;
	int stat;
	while( (pid = waitpid(-1, &stat, WNOHANG) ) > 0);
}

void init_server() {
	struct sockaddr_un srv_addr;			// Unix domain sockets
	sem_id=sem_open(SEMNAME1, O_CREAT, 0600, 1);	// open named semaphore
	sem_id2=sem_open(SEMNAME2, O_CREAT, 0600, 1);	// open named semaphore
	int i;
	mainpid = getpid();		// main process id (parent)

	signal(SIGCHLD, sig_chld);	// set signal handler for zombies
	signal(SIGUSR1, send_msg);	// set signal handler for chatting

	if ( signal(SIGINT, terminate) == SIG_ERR ) {
		perror("signal()\nerrno"); exit(1);	// debugging
	}
	if ( signal(SIGTSTP, show_info) == SIG_ERR ) {
		perror("signal()\nerrno"); exit(1);	// debugging
	}
	/* unique shm keys are associated with files in current directory */
	/* file "0" is for the shm struct */
	/* file "n" is for the "n" game */
	fp = fopen("0", "a+");		// create file
	fclose(fp);			// close file
	shm_key = ftok("0", 'x');	// key for shm file

	/* get shared memory id for the shm struct */
	if ((shm_id = shmget(shm_key, sizeof(struct shm_t), IPC_CREAT | 0666)) == -1) {
		perror("shmget()\nerrno"); exit(1);	// debugging
	}

	/* attach shared memory segment to shm */
	if ((long) (shm = (struct shm_t *) shmat(shm_id, (void *) 0, 0)) == -1) {
		perror("shmat()\nerrno"); exit(1);	// debugging
	}

	/* set shared memory segment for destruction */
	if (shmctl (shm_id , IPC_RMID , 0) == -1) {
		perror("schctl()\nerrno"); exit(1);	// debugging
	}
	fp = fopen("1", "a+");		// shared memory for first game
	fclose(fp);			// close file
	shm_key = ftok("1", 'x');	// create unique key

	/* get shared memory id for the first game */
	if ((shm_id = shmget(shm_key, sizeof(*(shm->game)), IPC_CREAT | 0666)) == -1) {
		perror("shmget()\nerrno"); exit(1);	// debugging
	}

	/* attach shared memory segment to shm->game */
	if ((long)(shm->game = (game_t) (shmat(shm_id, (void *) 0, 0))) == -1) {
		perror("shmat()\nerrno"); exit(1);	// debugging
	}

	/* set shared memory segment for destruction */
	if (shmctl (shm_id , IPC_RMID , 0) == -1) {
		perror("schctl()\nerrno"); exit(1);	// debugging
	}

	/* set initial values to game */
	for (i=0; i<maxplayers; i++) {
		shm->game->players[i] = 0;	// set file descriptors to 0
	}
	shm->game->next = NULL;			// no next game
	shm->game->active = 0;			// no active players
	shm->game_num = 1;			// first game

	read_inventory(inv_file);		// read inventory file

	/****** start server ******/
	memset(&srv_addr, 0, sizeof(struct sockaddr_un));
	srv_addr.sun_family = AF_UNIX;
	strncpy(srv_addr.sun_path, PATH,
			sizeof(srv_addr.sun_path) - 1);	// server hostname

	if ((server = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket()\nerrno"); exit(1);	// debugging
	}

	remove(PATH);	// remove server file if already exists
	if (bind(server, (struct sockaddr *) &srv_addr,
            sizeof(struct sockaddr_un)) == -1) {
        perror("bind()\nerrno"); exit(1);		// debugging
	}

	if (listen(server, MAXLISTEN) == -1) {
		perror("listen()\nerrno"); exit(1);	// debugging
	}
}

/* returns node "number" of the linked list */
game_t get_game(int number) {
	game_t current_game;	// current game, return value
	char buf[MAX];		// buffer

	if (number == 1) return shm->game;	// return first game

	snprintf(buf, MAX, "%d", number);	// copy number to buffer
	shm_key = ftok(buf, 'x');		// get unique key

	/* get shared memory id for the "number" game */
	if ((shm_id = shmget(shm_key, sizeof(*current_game), IPC_CREAT | 0666)) == -1) {
		perror("shmget()\nerrno"); exit(1);
	}

	/* attach shared memory segment to current_game */
	if ((long)(current_game = (game_t) (shmat(shm_id, (void *) 0, 0))) == -1) {
		perror("shmat()\nerrno"); exit(1);
	}

	/* if shmctl is executed, the shared memory will get destroyed */
	/* immediately as the function returns -- temp_shm holds the id */
	/* so it can be destroyed when the server closes */
	current_game->temp_shm = shm_id;

	return current_game;	// return pointer to shared memory segment
}

void read_inventory(char * fname) {
	int i, num;
	char word[MAX];		// holds each line

	if ((fp = fopen(fname, "r")) == NULL) {
		perror("File does not exist\nerrno"); _exit(1);	// debugging
	}
	fseek(fp, 0, SEEK_END);	// set position indicator to EOF
	rewind(fp);		// set position indicator to beginning

	while( fscanf(fp, "%s\t%d\n", word, &num) != EOF ) {
		i = resource_id(word);	// hashing function

		if (i == -1) {	// item does not exist
			perror("Wrong inventory\nerrno"); _exit(1);
		}
		get_game(shm->game_num)->inv[i] = num;	// set inventory values
	}
	fclose(fp);		// close file
}

int resource_id(char * res) {	// hashing function
	return (!strcmp(res, "gold") ? 0 :
			!strcmp(res, "armor") ? 1 :
			!strcmp(res, "ammo") ? 2 :
			!strcmp(res, "lumber") ? 3 :
			!strcmp(res, "magic") ? 4 :
			!strcmp(res, "rock") ? 5 : -1);
}

/* this is the game */
void action(int cl) {
	int game_number;	// current game number
	int timer = 0;		// for the 5 seconds timer
	char buf[MAXBUF], message[MAXBUF];	// buffers
	char name[MAX];		// player's name
	game_t g;		// current game struct

	signal(SIGUSR1, send_msg);	// set signal handler

	memset(name, 0, MAX);					// set buffer to \0
	/* try to insert player to server */
	/* if successful, return player's game number and name */
	game_number = insert_player(cl, name);

	g = get_game(game_number);				// get current game

	while(g->active < maxplayers) {	// till game is full
		usleep(100000);				// every 0.1 sec checks
		timer += 1;				// till it reaches 5 seconds
		if (timer == 50) {			// 5 seconds
			timer = 0;				// reset timer
			send(cl, "Please wait...\n",16,0);	// waiting..
		}
	}
	usleep(100000);				// solves some bugs..
	send(cl, "START\n", 7, 0);		// send start message to players
	printf("%s is ready!\n", name);	// players are ready!

	while (1) {	// chatting
		memset(message, 0, MAXBUF);			// set message to \0
		/* customize the message, so it shows who sent it */
		strncat(message, name, strlen(name));
		strncat(message, " : ", 4);

		memset(buf, 0, MAXBUF);				// set buffer to 0
		if(!recv(cl, buf, MAXBUF, 0)) {			// player crashed
			remove_player(cl, game_number);		// kill player
			printf("Player %s left..\n", name);	// inform the others
			g->active--;			// decrease active players of game
			if(g->active == 0) {	// empty game
				printf("All players left.\nGame Over\n\n");
			}
			_exit(1);	// kill player's process
		}

		strncat(message, buf, strlen(buf));	// for pretty code

		/* each player's process only has the open file descriptors */
		/* of the *previously* accepted players, so players */
		/* cannot send messages directly to other players */
		/* instead, they send the message to the server */
		/* by sending a custom signal to the main process (parent) */
		/* and the parent (server) then sends the message */
		/* to the other players of the same game */
		sem_wait(sem_id2);	// one message at a time
		memset(shm->message, 0, MAXBUF);
		strncpy(shm->message, message, strlen(message));
		shm->client = cl;	// player that sends the message
		shm->gamenum = game_number;	// the player's game
		kill(getppid(), SIGUSR1);	// send signal!
		usleep(100000);			// wait for others to receive
		sem_post(sem_id2);	// all is good
	}
}

int insert_player(int cl, char *name) {
	int i;
	int ok=1, num, temp[6] = {0}, sum = 0;	// various flags and variables
	char res[MAX], buf[MAXBUF], *line;	// buffers
	game_t g;		// player's game
	int game_number;	// game's number

	memset(buf, 0, MAXBUF);	// set buf to \0

	if (recv(cl, buf, MAXBUF, 0) == 0) {	// player crashes
		printf("Could not add player..\n");
		_exit(1);	// kill player's process
	}

	line = strtok (buf,"\n");		// get player's name
	if (sscanf(line, "%s", name) != 1) {	// no name in first line
		ok = 0;		// flag, player is not ok
	}
	else {
		line = strtok (NULL, "\n");	// next line
	}

	while (line && ok) {	// till it reaches EOF or player is not ok
		if ((sscanf(line, "%s\t%d", res, &num)) == 1) {
			ok = 0;			// bad file
		}
		else {				// good file
			if (((i = resource_id(res)) < 0) || (num <= 0)) {
				ok = 0;		// invalid resource or invalid number
			}
			else {
				temp[i] += num;		// player's request
				sum += num;		// total resources
			}

			line = strtok (NULL, "\n");	// next line
		}
	}	// EOF

	sem_wait(sem_id);	// one insert at a time

	game_number = shm->game_num;	// current game number
	g = get_game(game_number);	// get current game

	for (i=0; i<6; i++) {
		if (g->inv[i] - temp[i] < 0) {	// checks if player is greedy
			ok = 0;
		}
	}

	if( sum > quota ) {	// checks if player is too greedy
		ok = 0;
	}

	if ( ok ) {		// player is approved by the server!
		for (i=0; i<6; i++) {
			g->inv[i] -= temp[i];		// decrease server's inventory
		}
		send(cl, "OK\n", 4, 0);			// send ok message to player
		/* save player's name for the pretty "show info" function */
		memset(g->names[g->active], 0, MAX);
		strncpy(g->names[g->active], name, strlen(name));
		g->players[g->active++] = cl;	// save player's file descriptor

		if (g->active >= maxplayers) {	// game is full!
			shm->game_num++;		// next game
			snprintf(buf, MAX, "%d", shm->game_num);
			fp = fopen(buf, "a+");		// create next game's unique file
			fclose(fp);			// close file
			shm_key = ftok(buf, 'x');	// get unique key

			/* get shared memory id for the next game */
			if ((shm_id = shmget(shm_key, sizeof(*g), IPC_CREAT | 0666)) == -1) {
				perror("shmget()\nerrno"); _exit(1);	// debugging
			}

			/* attach shared memory segment to next game struct */
			if ((long)(g->next = (game_t) (shmat(shm_id, (void *) 0, 0))) == -1) {
				perror("shmat()\nerrno"); _exit(1);	// debugging
			}

			/* set shared memory segment for destruction */
			if (shmctl(shm_id , IPC_RMID , 0) == -1) {
				perror("schctl()\nerrno"); _exit(1);	// debugging
			}

			/* set initial values for next game */
			g = g->next;
			for (i=0; i<maxplayers; i++) {
				g->players[i] = 0;	// file decriptors are 0
			}
			g->next = NULL;			// no next game
			g->active = 0;			// no active player
			/* each game has its own inventory */
			read_inventory(inv_file);
		}
	}
	else {	// server disapproves of the player
		send(cl, "Try next time..\n", 17, 0);	// send message..
		printf("Could not add %s\n", name);	// sorry
		sem_post(sem_id); 	// increase semaphore
		_exit(1);		// kill player's process
	}

	/****** player inserted to game ******/
	sem_post(sem_id);		// next

	return game_number;		// return player's game number
}

void remove_player(int cl, int game_number) {
	int i;
	game_t g = get_game(game_number);	// get player's game

	for (i=0; i<maxplayers; i++) {
		if (g->players[i] == cl) {		// find player
			g->players[i] = 0;		// and remove his file descriptor
		}
	}
}
