#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>

/* Linked list object */
typedef struct s_word_object word_object;
struct s_word_object {
    char *word;
    word_object *next;
};

/* list_head: Shared between two threads, must be accessed with list_lock */
static word_object *list_head;
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t list_data_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t list_data_flush = PTHREAD_COND_INITIALIZER;

/* Add object to list */
static void add_to_list(char *word) {
    word_object *last_object, *tmp_object;

    char *tmp_string = strdup(word);
    tmp_object = malloc(sizeof(word_object));

    pthread_mutex_lock(&list_lock);

    if (list_head == NULL) {
	last_object = tmp_object;	
	list_head = last_object;
    } else {
	last_object = list_head;
	while (last_object->next) {
	    last_object = last_object->next;
	}
	last_object->next = tmp_object;
	last_object = last_object->next;
    }
    last_object->word = tmp_string;
    last_object->next = NULL;

    pthread_mutex_unlock(&list_lock);
    pthread_cond_signal(&list_data_ready);
}

static word_object *list_get_first(void) {
    word_object *first_object;

    first_object = list_head;
    list_head = list_head->next;

    return first_object;
}

static void *print_func(void *arg) {
    word_object *current_object;

    fprintf(stderr, "Print thread starting\n");

    while(1) {
	pthread_mutex_lock(&list_lock);

	while (list_head == NULL) {
	    pthread_cond_wait(&list_data_ready, &list_lock);
	}

	current_object = list_get_first();

	pthread_mutex_unlock(&list_lock);

	printf("Print thread: %s\n", current_object->word);
	free(current_object->word);
	free(current_object);

	pthread_cond_signal(&list_data_flush);
    }

    /* Silence compiler warning */
    return arg;
}

static void list_flush(void) {
    pthread_mutex_lock(&list_lock);

    while (list_head != NULL) {
	pthread_cond_signal(&list_data_ready);
	pthread_cond_wait(&list_data_flush, &list_lock);
    }

    pthread_mutex_unlock(&list_lock);
}

int main(int argc, char **argv) {
    char input_word[256];
    int c;
    int option_index = 0;
    int count = -1;
    pthread_t print_thread;
    static struct option long_options[] = {
	{"count",   required_argument, 0, 'c'},
	{0,         0,                 0,  0 }
    };

    while (1) {
	c = getopt_long(argc, argv, "c:", long_options, &option_index);
	if (c == -1)
	    break;

	switch (c) {
	    case 'c':
		count = atoi(optarg);
		break;
	}
    }

    /* Start new thread for printing */
    pthread_create(&print_thread, NULL, print_func, NULL);

    fprintf(stderr, "Accepting %i input strings\n", count);

    while (scanf("%256s", input_word) != EOF) {
	add_to_list(input_word);
	if (!--count) break;
    }

    list_flush();

    return 0;
}
