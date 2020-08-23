#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>

struct client{
	char status; // 'x' or 'o' if player, else 'v' for viewer
	int fd;
	char buf[1000]; //line read from the client (could be partial)
	int bytes_in_buf; //how many bytes read so far
	struct in_addr ipaddr;
	struct client *next;
};
struct client *head = NULL;

static char describe_turn[] = "%c makes move %d\r\n";
static char turn[] = "It is %c's turn.\r\n";
static char player_prompt[] = "You now get to play! You are now %c.\r\n";
static char change_in_player[] = "%s is now playing '%c'.\r\n";
static char wrong_turn[] = "It is not your turn \r\n";
static char correct_turn[] = "It is your turn\r\n";
static char viewer_mssg[] = "You can't make moves; you can only watch the game\r\n";
static char invalid_move[] = "That space is taken\r\n";
static char game_over[] = "Game over!\r\n";
static char winner[] = "%c wins.\r\n";
static char draw[] = "It is a draw\r\n";
static char start_over[] = "Let's play again!\r\n";
static char role_switch[] = "You are %c\r\n";

char board[9];
static int listenfd;
// keep track of player_x and player_o's file descriptors
int player_x = -1;
int player_o = -1;
char whos_turn = 'x'; // 'x' goes first

static void broadcast(char *s, int size, struct client *speaker);
static void showboard(int fd);

int main(int argc, char **argv){
	extern void check_game_over();
	extern void newconnection(), check_activity(struct client *p);

	int status = 0, port = 3000;
	int  maxfd, c;
	struct sockaddr_in r;
	fd_set fds;

	/* Parse commandline for -p option; update port num accordingly */
        while ((c = getopt(argc, argv, "p:")) != EOF){
                switch (c){
                case 'p':
                        port = atoi(optarg);
                        break;
                default:
                        status = 1;
                }
        }
        if (status || optind < argc){
                fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
                return 1;

        }
        //initialize board
	memcpy(board, "123456789", 9);

	/* bind address to socket and listen for incoming connections */
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        	perror("socket");
                return 1;
        }
        memset(&r, '\0', sizeof r); // all members have a default value of '\0'
        r.sin_family = AF_INET;
        r.sin_addr.s_addr = INADDR_ANY;
        r.sin_port = htons(port);

        if (bind(listenfd, (struct sockaddr *)&r, sizeof r) < 0){
                perror("bind");
                return 1;
        }

        if (listen(listenfd, 5)){
                // 5 people can do a connect() before we accept() them
                perror("listen");
                return 1;
        }

	while (1){ // server will only exit if it's killed

		check_game_over();
		/* Add listening socket and connected clients to linked list
		and check who is ready to read from. Only "select" the file
		descriptors with read activity. */
		struct client *curr;
		maxfd = listenfd;
		FD_ZERO(&fds); // clear fd set
		FD_SET(listenfd, &fds);
		for (curr = head; curr; curr = curr->next){
			FD_SET(curr->fd, &fds);
			if(curr->fd > maxfd)
				maxfd = curr->fd;
		}
		if (select(maxfd + 1, &fds, NULL, NULL, NULL) < 0) {
	    		perror("select");
			return 1;
		}
		/* if any of the connected clients have read activity,
		then we should check out what that activity is*/
		for (curr = head; curr; curr = curr->next){
			if (FD_ISSET(curr->fd, &fds))
		    		break;
		}
		if (curr)
			check_activity(curr);
		/* if the listening fd has read activity, it means a client
		wants to connect */
		if (FD_ISSET(listenfd, &fds)){
			newconnection();
		}
	}
	return status;
}
/* Check if a row has been filled horizontally, vertically, or diagonally*/
int game_is_over(){

    int i, c;
    extern int allthree(int start, int offset);
    extern int isfull();

    for (i = 0; i < 3; i++)
        if ((c = allthree(i, 3)) || (c = allthree(i * 3, 1)))
            return(c);
    if ((c = allthree(0, 4)) || (c = allthree(2, 2)))
        return(c);
    if (isfull())
        return(' ');
    return(0);


}
int allthree(int start, int offset)
{
    if (board[start] > '9' && board[start] == board[start + offset]
            && board[start] == board[start + offset * 2])
        return(board[start]);
    return(0);
}

extern void check_game_over(){
	extern void swap_players();
	int winnr = game_is_over();
 	if (winnr){
        	printf("Game over!\n");
                fflush(stdout);
                if (winnr != ' '){
                	printf("%c wins.\n", winnr);
			fflush(stdout);
                }else{
                	printf("It is a draw.\n");
                	fflush(stdout);
		}
                broadcast(game_over, strlen(game_over), NULL);

                for(struct client *a = head; a; a = a->next)
                	showboard(a->fd);

		if (winnr  == ' '){
			broadcast(draw, strlen(draw), NULL);
		}else{
			char who_won[sizeof winner + 1];
			snprintf(who_won, sizeof winner , winner, winnr);
			broadcast(who_won, strlen(who_won), NULL);
		}
		broadcast(start_over, strlen(start_over), NULL);
		swap_players();
		// clear the board
		memcpy(board, "123456789", 9);
		whos_turn = 'x'; // new game begins with player x
	}
}
/* checks if the board is full of players */
int isfull()
{
    int i;
    for (i = 0; i < 9; i++)
        if (board[i] < 'a')
            return(0);
    return(1);
}
/* Assuming that the game is finished, swap the player roles.
That is, player_x becomes player_o, and vice versa.*/

extern void swap_players(){

	char swap[sizeof role_switch + 1];
	int temp = player_x;
	player_x = player_o; // change file descriptor for player x
 	snprintf(swap, sizeof swap,role_switch, 'x');
	// inform player_x of the switch
	if (write(player_x, swap, strlen(swap)) != strlen(swap)){
		perror("write");
		exit(1);
	}
	player_o = temp; // change file descriptor for player o
	// inform player_o of the switch
	snprintf(swap, sizeof swap,role_switch, 'o');
	if(write(player_o,swap, strlen(swap)) != strlen(swap)){
		perror("write");
 		exit(1);
	}
	for (struct client *y = head; y; y = y->next){
		if (y->status == 'x')
			y->status = 'o';
		else if(y->status == 'o')
			y->status = 'x';
	}
}
/*The given client had read activity that needs to be checked.
A 0 read means EOF (client disconnected), and negative digit is error.
A single positive digit read is a move; anything else is a chat message. */

void check_activity(struct client *p){

	int len;
	extern void removeclient(int fd);
	extern void process(char *mssg, int size, struct client *k);
	extern char *extractline(char *p, int size);

	if( (len = read(p->fd, p->buf + p->bytes_in_buf,
		sizeof(p->buf) - p->bytes_in_buf - 1) ) < 0 ){
			perror("read");
			exit(1);

	}else if(len == 0){ //client disconnects
		removeclient(p->fd);
	}else{
		char *nextpos;
		while ((nextpos = extractline(p->buf, len))
		      || len == sizeof(p->buf) - 1){

                	if (len == sizeof(p->buf) - 1){
                        	p->buf[len] = '\0';
                        	process(p->buf,strlen(p->buf), p);
                        	p->bytes_in_buf = 0;
				return;
			}else{
				process(p->buf, strlen(p->buf), p);
				len -= nextpos - p->buf;
				// move data to the beginning of buf
				memmove(p->buf, nextpos, len);
				p->bytes_in_buf = len;
			}
		}
		p->bytes_in_buf = len;
	}
}

char *extractline(char *p, int size){
    /* returns pointer to string after, or NULL if there isn't an entire
     * line here.  If non-NULL, original p is now a valid string. */
    int nl;
    for (nl = 0; nl < size && p[nl] != '\r' && p[nl] != '\n'; nl++)
        ;
    if (nl == size)
        return(NULL);

    /*
     * There are three cases: either this is a lone \r, a lone \n, or a CRLF.
     */
    if (p[nl] == '\r' && nl + 1 < size && p[nl+1] == '\n') {
        /* CRLF */
        p[nl] = '\0';
        return(p + nl + 2);
    } else {
        /* lone \n or \r */
        p[nl] = '\0';
        return(p + nl + 1);
    }
}

/* Given a message, decide whether it is a move
or whether it is a chat message, and act appropriately.*/

void process(char *mssg, int size, struct client *k){
	extern void move(struct client *b, int square);
	char x[strlen(mssg) + 3];
	if(size == 1 && isdigit(mssg[0]) && mssg[0]!= '0'){
		move(k, mssg[0] - '0'); //make a move on the board
	}else{
		printf("chat message: %s\n", mssg);//add message to stdout log
		fflush(stdout);
		//add network newline before broadcasting
		strncpy(x,mssg,sizeof x);
		strncat(x,"\r\n", sizeof x -strlen(x));

		broadcast(x, strlen(x), k); //send the message to everyone
	}
}
/* The given client tried to make a move on a square in the board.
If it's their turn, display the move on the board*/
void move(struct client *b, int square){

	if(b->status == 'v'){ //a viewer can't make a move

		if (write(b->fd, viewer_mssg, strlen(viewer_mssg))
		    != strlen(viewer_mssg)){
			perror("write");
			exit(1);
		}
		printf("%s tries to make move %d, but they aren't playing\n",
			inet_ntoa(b->ipaddr),square);
		fflush(stdout);

	}else if(b->status != whos_turn){ //ensure right player makes a move

		if(write(b->fd, wrong_turn, strlen(wrong_turn))
		   != strlen(wrong_turn)){
			perror("write");
			exit(1);
		}
		printf("%s tries to make a move %d, but it's not their turn\n",
			inet_ntoa(b->ipaddr),square);
		fflush(stdout);

	}else{ //the correct player must be making a move
		if(isdigit(board[square - 1])){
			board[square - 1] = b->status;
			printf("%s (%c) makes move %d\r\n",
				inet_ntoa(b->ipaddr), b->status, square);
			fflush(stdout);
			char a[sizeof describe_turn + 1];
			snprintf(a, sizeof a, describe_turn,
				b->status, square);

			broadcast(a, strlen(a), NULL);
			//display board to everyone
			for (struct client *h = head; h; h = h->next)
				showboard(h->fd);
			/*Check who's turn it is, and inform that player
			that they need to make a move.  */

			char cur_player[sizeof turn + 1];
			if (whos_turn == 'x'){
				whos_turn = 'o';
				snprintf(cur_player, sizeof cur_player, turn, whos_turn);
				// indicate who's turn it is next ("It is your turn")
				if(player_o != -1 && write(player_o, correct_turn,
				 strlen(correct_turn)) != strlen(correct_turn)){
					perror("write");
					exit(1);
				}
				// dont broadcast to player o
				struct client *o;
				for(o = head; o && o->status != 'o'; o = o->next)
					;
                                broadcast(cur_player, strlen(cur_player), o);
			}else{
				whos_turn = 'x';
				snprintf(cur_player, sizeof cur_player, turn, whos_turn);
				//dont broadcast to player x
				struct client *x;
                                for(x = head; x && x->status != 'x'; x = x->next)
                                        ;
                                broadcast(cur_player, strlen(cur_player), x);

				if(player_x != -1 && write(player_x, correct_turn,
                                 strlen(correct_turn)) != strlen(correct_turn)){
                                        perror("write");
                                        exit(1);
				}
			}
		}else{
			printf("%s tries to make move %d, but that space is taken\n",
				inet_ntoa(b->ipaddr), square);
			fflush(stdout);
			if(write(b->fd, invalid_move, strlen(invalid_move)) !=
			   strlen(invalid_move)){
				perror("write");
				exit(1);
			}
		}
	}
}
/*Remove client from file descriptor. If this client is a player,
their role needs to be reassigned to someone else .*/
void removeclient(int fd){
	extern void reassign(char status);
	struct client **cur;
	for(cur = &head; *cur && (*cur)->fd != fd; cur = &(*cur)->next)
		;
	if(*cur){
		struct client *t = (*cur)->next;
		printf("disconnecting client %s\n",inet_ntoa((*cur)->ipaddr));
		fflush(stdout);
		if ((*cur)->status == 'x' || (*cur)->status == 'o')
			reassign((*cur)->status);
		free(*cur);
		*cur = t;
	}else{
		fprintf(stderr, "ticsvr: unable to remove fd %d\n", fd);
		fflush(stderr);
		exit(1);
	}
	close(fd);
}
/* give a player's role to someone else when they leave the game */
void reassign(char status){

	struct client *q;
	char prompt[sizeof player_prompt + 1];
	for (q = head; q && q->status != 'v';q = q->next)
		;
	if (q){
		q->status = status;
		// inform client that their role has changed
		snprintf(prompt, sizeof prompt, player_prompt, status);
		if(write(q->fd, prompt, strlen(prompt)) != strlen(prompt)){
			perror("write");
			exit(1);
		}
		// log to stdout that a client's role has changed
		printf("client from %s is now %c\n",
			inet_ntoa(q->ipaddr), q->status);
		fflush(stdout);

		// broadcast this player's role to everyone
		char *addr = inet_ntoa(q->ipaddr);
		int length = sizeof change_in_player + strlen(addr) +1;
		char change_role[length];
		snprintf(change_role, sizeof change_role, change_in_player,
			 addr, q->status);

		broadcast(change_role, strlen(change_role), q);
		if (status == 'x')
			player_x = q->fd;
		else
			player_o = q->fd;

	}else{ // we were unable to find replacement players
		if(status == 'x')
			player_x = -1;
		else if(status == 'o')
			player_o = -1;
	}
}

/* Broadcast an announcement to all connected clients
except the speaker. If the message is not from a client, then
the speaker is NULL.*/

void broadcast(char *s, int size, struct client *speaker){
	struct client *m;
	for (m = head; m; m = m->next){
		if( m != speaker && write(m->fd, s, size) != size){
			perror("write");
			exit(1);
		}
	}
}
/* Accept the connection from a client, update log on stdout,
add them to linked list*/
void newconnection(){

	extern void addclient(int fd, struct in_addr ipaddr, char status);
	int fd;
	char display[sizeof turn + 1];
	char display2[sizeof player_prompt + 1];
	struct sockaddr_in r;
	socklen_t socklen = sizeof r;

	if ((fd = accept(listenfd, (struct sockaddr *)&r, &socklen)) < 0) {
		perror("accept");
		exit(1);
	}
	printf("new connection from %s\n", inet_ntoa(r.sin_addr));
	fflush(stdout);

	/* draw board for the client and indicate who's turn it is */
	showboard(fd);
	snprintf(display, sizeof display,turn, whos_turn);

	if (write(fd, display, strlen(display)) != strlen(display)){
		perror("write");
		exit(1);
	}

	if(player_x == -1){

                //Broadcast to everyone that there is a new player

		int len = sizeof change_in_player + strlen(inet_ntoa(r.sin_addr)) + 1;
                char newplayer[len];
                snprintf(newplayer, sizeof newplayer, change_in_player,
		inet_ntoa(r.sin_addr), 'x');
                broadcast(newplayer, strlen(newplayer), NULL);

		/* Assign the connected client to player x */
		printf("client from %s is now x\n", inet_ntoa(r.sin_addr));
		fflush(stdout);
		player_x = fd;
		addclient(fd, r.sin_addr, 'x');

		snprintf(display2, sizeof display2, player_prompt, 'x');
		if (write(fd, display2, strlen(display2)) != strlen(display2)){
			perror("write");
			exit(1);
		}

	}else if(player_o == -1){

		//Broadcast to everyone that there is a new player
		int len = sizeof change_in_player + strlen(inet_ntoa(r.sin_addr)) + 1;
		char newplayer[len];
		snprintf(newplayer, sizeof newplayer, change_in_player,
		 inet_ntoa(r.sin_addr), 'o');
		broadcast(newplayer, strlen(newplayer), NULL);

		/* Assign the connected client to player o*/
                printf("client from %s is now o\n", inet_ntoa(r.sin_addr));
                fflush(stdout);
                player_o = fd;
                addclient(fd, r.sin_addr, 'o');

		snprintf(display2, sizeof display2, player_prompt, 'o');
                if (write(fd, display2, strlen(display2)) != strlen(display2)){
                        perror("write");
                        exit(1);
                }


	}else{
		addclient(fd, r.sin_addr, 'v');
		/* the client only views the game
		 if there are already x & o players */
	}
}

/* Add a connected client to the linked list*/
void addclient(int fd, struct in_addr ipaddr, char status){
	struct client *curr = malloc(sizeof(struct client));
	if (curr == NULL){
		perror("out of memory");
		exit(1);
	}
	curr->fd = fd;
	curr->ipaddr = ipaddr;
	curr->status = status;
	curr->bytes_in_buf = 0;
	curr->next = head;
	head = curr;

}
void showboard(int fd)
{
    char buf[100], *bufp, *boardp;
    int col, row;

    for (bufp = buf, col = 0, boardp = board; col < 3; col++) {
        for (row = 0; row < 3; row++, bufp += 4)
            snprintf(bufp, sizeof buf - strlen(buf), " %c |", *boardp++);
        bufp -= 2;  // kill last " |"
        strncpy(bufp, "\r\n---+---+---\r\n", sizeof buf - strlen(buf));
        bufp = strchr(bufp, '\0');
    }
    if (write(fd, buf, bufp - buf) != bufp-buf){
        perror("write");
	exit(1);
    }
}

