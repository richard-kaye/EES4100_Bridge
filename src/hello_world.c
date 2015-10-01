#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>

#define SERVER_ADDR "127.0.0.1"
#define SERVER_PORT 10000
#define DATA_LENGTH 256
#define NUM_LISTS   2
#define LISTEN_BACKLOG 1
#define QUIT_STRING "exit"

/* Linked list object */
typedef struct s_word_object word_object;
struct s_word_object {
    char *word;
    word_object *next;
};

/* list_head: Shared between two threads, must be accessed with list_lock */
static word_object *list_heads[NUM_LISTS];
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t list_data_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t list_data_flush = PTHREAD_COND_INITIALIZER;

/* Add object to list */
static void add_to_list(word_object **list_head, char *word) {
    word_object *last_object, *tmp_object;
    char *tmp_string;
    
    /* Do all memory allocation outside of locking - strdup() and malloc() can
     * block */
    tmp_object = malloc(sizeof(word_object));
    tmp_string = strdup(word);

    /* Set up tmp_object outside of locking */
    tmp_object->word = tmp_string;
    tmp_object->next = NULL;

    pthread_mutex_lock(&list_lock);

    if (*list_head == NULL) {
	/* The list is empty, just place our tmp_object at the head */
	*list_head = tmp_object;
    } else {
	/* Iterate through the linked list to find the last object */
	last_object = *list_head;
	while (last_object->next) {
	    last_object = last_object->next;
	}
	/* Last object is now found, link in our tmp_object at the tail */
	last_object->next = tmp_object;
    }

    pthread_mutex_unlock(&list_lock);
    pthread_cond_signal(&list_data_ready);
}

/* Retrieve the first object in the linked list. Note that this function must
 * be called with list_lock held */
static word_object *list_get_first(word_object **list_head) {
    word_object *first_object;

    first_object = *list_head;
    *list_head = (*list_head)->next;

    return first_object;
}

static void *print_func(void *arg) {
    word_object **list_head = (word_object **) arg;
    word_object *current_object;

    fprintf(stderr, "Print thread starting\n");

    while(1) {
	pthread_mutex_lock(&list_lock);

	while (*list_head == NULL) {
	    pthread_cond_wait(&list_data_ready, &list_lock);
	}

	current_object = list_get_first(list_head);

	pthread_mutex_unlock(&list_lock);

	/* printf() and free() can block, make sure that we've released
	 * list_lock first */
	printf("Print thread: %s\n", current_object->word);
	free(current_object->word);
	free(current_object);

	/* Let list_flush() know that we've done some work */
	pthread_cond_signal(&list_data_flush);
    }

    /* Silence compiler warning */
    return arg;
}

static void list_flush(word_object *list_head) {
    pthread_mutex_lock(&list_lock);

    while (list_head != NULL) {
	pthread_cond_signal(&list_data_ready);
	pthread_cond_wait(&list_data_flush, &list_lock);
    }

    pthread_mutex_unlock(&list_lock);
}

static void start_server(void) {
    int socket_fd, session_sock_fd;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    socklen_t client_address_len;
    int want_quit = 0;
    fd_set read_fds;
    int bytes;
    char data[DATA_LENGTH];
    pthread_t print_thread;

    fprintf(stderr, "Starting server\n");

    pthread_create(&print_thread, NULL, print_func, &list_heads[1]);

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(SERVER_PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;


    if (bind(socket_fd, (struct sockaddr *) &server_address, 
			    sizeof(server_address)) < 0) {
	fprintf(stderr, "Bind failed: %s\n", strerror(errno));
	exit(1);
    }

    if (listen(socket_fd, LISTEN_BACKLOG) < 0) {
	fprintf(stderr, "Listen failed\n");
	exit(1);
    }

    while (!want_quit) {
	client_address_len = sizeof(client_address);
	if ((session_sock_fd = accept(socket_fd,
					(struct sockaddr *) &client_address,
					&client_address_len)) < 0 ) {
	    fprintf(stderr, "Accept failed\n");
	    exit(1);
	}

        FD_ZERO(&read_fds);
        FD_SET(session_sock_fd, &read_fds);
        while (1) {
    	
	    /* Wait until data has arrived */
	    if (select(session_sock_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
		fprintf(stderr, "Select failed\n");
		exit(1);
	    }
	
	    if (!FD_ISSET(session_sock_fd, &read_fds)) continue;
	
	    /* Read input data */
	    bytes = recv(session_sock_fd, data, sizeof(data), 0);

	    if (bytes == 0) {
		fprintf(stderr, "Client disconnected\n");
		break;
	    }
	
	    if (bytes < 0) {
		fprintf(stderr, "Recvfrom failed\n");
		exit(1);
	    }
	
	    /* Process data */
	    if (!strcmp(data, QUIT_STRING)) want_quit = 1;
	    else add_to_list(&list_heads[1], data);
        }
    }

    close(socket_fd);
    list_flush(list_heads[1]);
}

static void start_client(int count) {
    int sock_fd;
    struct sockaddr_in addr;
    char input_word[DATA_LENGTH];

    fprintf(stderr, "Accepting %i input strings\n", count);

    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
	fprintf(stderr, "Socket failed\n");
	exit(1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);

    if (connect(sock_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
	fprintf(stderr, "Connect failed\n");
	exit(1);
    }

    while (scanf("%256s", input_word) != EOF) {
	if (send(sock_fd, input_word, strlen(input_word) + 1, 0) < 0) {
	    fprintf(stderr, "Send failed\n");
	    exit(1);
	}
	if (!--count) break;
    }
}

int main(int argc, char **argv) {
    int c;
    int option_index = 0;
    int count = -1;
    int server = 0;
    static struct option long_options[] = {
	{"count",   required_argument,	0, 'c'},
	{"server",  no_argument,	0, 's'},
	{0,         0,			0,  0 }
    };

    while (1) {
	c = getopt_long(argc, argv, "c:s", long_options, &option_index);
	if (c == -1)
	    break;

	switch (c) {
	    case 'c':
		count = atoi(optarg);
		break;
	    case 's':
		server = 1;
		break;
	}
    }

    if (server) start_server();
    else start_client(count);

    return 0;
}
