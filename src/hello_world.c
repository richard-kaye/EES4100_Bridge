#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

/* Linked list object */
typedef struct s_word_object word_object;
struct s_word_object {
    char *word;
    word_object *next;
};
static word_object *list_head;

/* Add object to list */
static void add_to_list(char *word) {
    word_object *last_object;

    if (list_head == NULL) {
	last_object = malloc(sizeof(word_object));	
	list_head = last_object;
    } else {
	last_object = list_head;
	while (last_object->next) {
	    last_object = last_object->next;
	}
	last_object->next = malloc(sizeof(word_object));
	last_object = last_object->next;
    }
    last_object->word = strdup(word);
    last_object->next = NULL;
}

/* Print and free objects */
void print_and_free(void) {
    word_object *current_object;
    word_object *old_object;

    current_object = list_head;
    while (1) {
	printf("%s\n", current_object->word);
	free(current_object->word);
	old_object = current_object;
	if (current_object->next) {
	    current_object = current_object->next;
	    free(old_object);
	} else {
	    free(old_object);
	    break;
	}
    }
    
}

int main(int argc, char **argv) {
    char input_word[256];
    int c;
    int option_index = 0;
    int count = -1;
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

    fprintf(stderr, "Accepting %i input strings\n", count);

    while (scanf("%256s", input_word) != EOF) {
	add_to_list(input_word);
	if (!--count) break;
    }

    /* print and free objects */
    print_and_free();

    return 0;
}
