#include <stdio.h>	// standard input/output
#include <stdlib.h>	// general purpose functions
#include <string.h>	// string operations
#include <unistd.h>	// miscellaneous functions
#include <pthread.h>	// for the POSIX threads
#include <sys/un.h>	// for sockaddr_un structure
#include <sys/socket.h>	// socket definitions
#include <sys/types.h>	// various type definitions
#include <signal.h>	// for handling signals

#define PATH "server"	// server hostname
#define MAX 16		// max size for small buffers
#define MAXBUF 128	// max size for large buffers
#define MAXLISTEN 50	// max queue length for listen

/* games are implemented using linked lists */
/* "n" node of the list contains data for the "n" game */
typedef struct game_t {	// everything for each game
	int *inv;	// resources (inventory)
	int *players;	// players' file descriptors
	char **names;	// players' names
	int active;	// active players in game
	struct game_t *next;	// next game
} *game_t;

game_t game;		// first game
pthread_mutex_t mutex;	// mutex for inserting players
int maxplayers;		// max players per game
char inv_file[MAX];	// server inventory file
int quota;		// max resources per player

int ret;		// for pthread_exit
int server;		// server file descriptor
int game_num;		// number of games

void terminate(int);		// signal handler for ctrl-c
void destroy_everything(void);	// clear memory, close server
void show_info(int);		// pretty info, handler for ctrl-z
void init_server(void);		// start server
game_t get_game(int);		// get current game
void read_inventory(char *);	// read server's inventory file
int resource_id(char *);	// hashing function for resources
void* action(void *);		// does everything for the player
int insert_player(int, char *);	// connect a player with the server
void remove_player(int, int);	// kills player

// ./gameserver -p 5 -i inventory -q 5

int main(int argc, char *argv[]) {
	int new_fd;			// player's file descriptor
	socklen_t addr_size;		// size of address
	struct sockaddr_un cl_addr;	// Unix domain sockets
	pthread_t thr; // thread

	/* checks if all arguments are OK */
	if (argc != 7) {
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
		quota = atoi(argv[6]);	// quota
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
		/* after accepting a player, create a thread calling action */
		/* action takes player's file descriptor and does everything */
		pthread_create(&thr, NULL, action, &new_fd);
		pthread_detach(thr);	// don't wait for thread
	}
	return 0;	// unreachable
}

void destroy_everything() {	// free memory
	game_t g = game;	// get first game
	game_t temp;		// used to get next game
	int i, j;

	for (i=0; i<game_num; i++) { 	// for each game
		for (j=0; j<maxplayers; j++) {
			if (g->names[j]) {
				free(g->names[j]);	// free names
			}
		}
		free(g->names);		// free array of names
		free(g->inv);		// free inventory
		free(g->players);	// free array of players' file descriptors
		temp = g;		// temporary
		g = g->next;		// get next game
		free(temp);		// free game
	}
	remove(PATH);			// remove server file
	pthread_mutex_destroy(&mutex);	// destroy mutex
}


void terminate(int signo) {		// close server!
	printf("\n~~~~~ Server Closing! ~~~~~\n\n");
	destroy_everything();	// destroy everything!

	exit(0);	// terminate server!
}

void show_info(int signo) {	// pretty function
	game_t g;
	int i, j;
	int empty;

	g = game;	// get first game
	for (i=0, g=game; i<game_num; i++, g=g->next) {
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
	}	// get next game
	printf("\n~~~ That's all! ~~~\n\n");
}


void init_server() {
	struct sockaddr_un srv_addr;	// Unix domain sockets

	pthread_mutex_init(&mutex, 0);	// initialize mutex
	if ( signal(SIGINT, terminate) == SIG_ERR ) {
		perror("signal()\nerrno"); exit(1);	// debugging
	}
	if ( signal(SIGTSTP, show_info) == SIG_ERR ) {
		perror("signal()\nerrno"); exit(1);	// debugging
	}

	/* set initial values to game */
	/* dynamic memory allocation for games (nodes of linked list) */
	game = (game_t) malloc(sizeof(*game));
	/* set inventory resources to 0 */
	game->inv = (int *) calloc (6, sizeof(int));
	/* set players' file descriptors to 0 */
	game->players = (int *) calloc (maxplayers, sizeof(int));	
	/* set array of players' names to NULL */
	game->names = (char **) calloc(maxplayers, sizeof(char));
	game->next = NULL;	// no next game
	game->active = 0;	// no active players
	game_num = 1;	// first game

	read_inventory(inv_file);	// read inventory file

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
	int i;
	game_t current_game = game;	// current game, return value
	for (i=0; i<number-1; i++) {
		current_game = current_game->next;	// get next game
	}
	return current_game;	// return "number" game
}

void read_inventory(char * fname) {
	FILE *fp;
	int i, num;
	char word[MAX];		// holds each line

	if ((fp = fopen(fname, "r")) == NULL) {
		perror("File does not exist\nerrno"); exit(1);	// debugging
	}
	fseek(fp, 0, SEEK_END);	// set position indicator to EOF
	rewind(fp);		// set position indicator to beginning

	while( fscanf(fp, "%s\t%d\n", word, &num) != EOF ) {
		i = resource_id(word);	// hashing function

		if (i == -1) {	// item does not exist
			perror("Wrong inventory\nerrno"); exit(1);
		}
		get_game(game_num)->inv[i] = num;	// set inventory values
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
void* action(void *fd) {
	int cl = *(int *)fd;	// player's file descriptor
	int game_number;	// current game number
	int i;
	int timer = 0;		// for the 5 seconds timer
	char buf[MAXBUF], message[MAXBUF];	// buffers
	char name[MAX];		// player's name
	game_t g;		// current game struct

	memset(name, 0, MAX);		// set buffer to \0
	/* try to insert player to server */
	/* if successful, return player's game number and name */
	game_number = insert_player(cl, name);

	g = get_game(game_number);	// get current game

	while(g->active < maxplayers) {	// till game is full
		usleep(100000);		// every 0.1 sec checks
		timer += 1;		// till it reaches 5 seconds
		if (timer == 50) {	// 5 seconds
			timer = 0;	// reset timer
			send(cl, "Please wait...\n",16,0);	// waiting..
		}
	}
	usleep(100000);			// solves some bugs..
	send(cl, "START\n", 7, 0);	// send start message to players
	printf("%s is ready!\n", name);	// players are ready!

	while (1) {	// chatting
		memset(message, 0, MAXBUF);	// set message to \0
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
			pthread_exit(&ret);	// terminate player's thread
		}

		strncat(message, buf, strlen(buf));	// for pretty code

		/* threads share everything, including open file descriptors */
		/* so any player can send a message to another player */
		for (i=0; i<maxplayers; i++) {
			/* sends the message to all other players of the same game */
			if (g->players[i] != 0 && g->players[i] != cl) {
				send(g->players[i], message, MAXBUF, 0);
			}
		}
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
		pthread_exit(&ret);	// terminate player's thread
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
				temp[i] += num;	// player's request
				sum += num;	// total resources
			}

			line = strtok (NULL, "\n");	// next line
		}
	}	// EOF

	pthread_mutex_lock(&mutex);	// one insert at a time

	game_number = game_num;	// current game number
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
			g->inv[i] -= temp[i];	// decrease server's inventory
		}
		send(cl, "OK\n", 4, 0);		// send ok message to player
		/* save player's name for the pretty "show info" function */
		g->names[g->active] = calloc(MAX, sizeof(char));
		strncpy(g->names[g->active], name, strlen(name));
		g->players[g->active++] = cl;	// save player's file descriptor

		if (g->active >= maxplayers) {	// game is full!
			/* set initial values to game */
			/* dynamic memory allocation for games (nodes of linked list) */
			g->next = (game_t) malloc(sizeof(*(g->next)));
			g = g->next;	// get next game (next node)
			/* set players' file descriptors to 0 */
			g->players = (int *) calloc (maxplayers, sizeof(int));
			/* set array of players' names to NULL */
			g->names = (char **) calloc(maxplayers, sizeof(char));
			/* set inventory resources to 0 */
			g->inv = (int *) calloc (6, sizeof(int));
			g->next = NULL;	// no next game
			g->active = 0;	// no active players
			game_num++;	// next game
			/* each game has its own inventory */
			read_inventory(inv_file);
		}
	}
	else {	// server disapproves of the player
		send(cl, "Try next time..\n", 17, 0);	// send message..
		printf("Could not add %s\n", name);	// sorry
		pthread_mutex_unlock(&mutex);	// unlock mutex
		pthread_exit(&ret);		// terminate player's thread
	}

	/****** player inserted to game ******/
	pthread_mutex_unlock(&mutex);	// next

	return game_number;		// return player's game number
}

void remove_player(int cl, int game_number) {
	int i;
	game_t g = get_game(game_number);	// get player's game

	for (i=0; i<maxplayers; i++) {
		if (g->players[i] == cl) {	// find player
			g->players[i] = 0;	// and remove his file descriptor
		}
	}
}
