#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <ctype.h>

extern int total_guesses;
extern int total_wins;
extern int total_losses;
extern char **words;

int wordsInDict;

pthread_mutex_t lock;

void *handle_client(void *arg);
void sigusr1_handler(int signum);
void load_words(const char *filename, int num_words);
int is_valid_guess(const char *guess);
void update_global_variables(int guesses_left, int game_result);
char* str_to_lower(const char *str);
char* str_to_upper(const char *str);
void send_response(int client_fd, char valid_guess, int guesses_left, const char *result);
char *evaluate_guess(const char *hidden_word, const char *guess);

int wordle_server(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "ERROR: Invalid argument(s)\nUSAGE: %s <listener-port> <seed> <dictionary-filename> <num-words>\n", *argv);
        return EXIT_FAILURE;
    }

    int port = atoi(*(argv + 1));
    int seed = atoi(*(argv + 2));
    const char *dictionary_filename = *(argv + 3);
    int num_words = atoi(*(argv + 4));

    if (port <= 0 || seed < 0 || num_words <= 0) {
        fprintf(stderr, "ERROR: Invalid argument(s)\nUSAGE: %s <listener-port> <seed> <dictionary-filename> <num-words>\n", *argv);
        return EXIT_FAILURE;
    }

    int server_fd, client_fd;
    struct sockaddr_in address;
    socklen_t addr_len = sizeof(address);

    // Initialize the mutex
    pthread_mutex_init(&lock, NULL);

    // Load dictionary
    load_words(dictionary_filename, num_words);

    printf("MAIN: opened %s (%d words)\n", dictionary_filename, num_words);
    fflush(NULL);

    // Set up signal handler for SIGUSR1
    struct sigaction sa;
    sa.sa_handler = sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    // Seed the random number generator
    srand(seed);
    printf("MAIN: seeded pseudo-random number generator with %d\n", seed);
    fflush(NULL);

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Bind socket
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("MAIN: Wordle server listening on port\n");
    fflush(NULL);

    // Accept connections
    while (1) {
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, &addr_len)) < 0) {
            perror("accept failed");
            continue;
        }
        printf("MAIN: rcvd incoming connection request\n");
        fflush(NULL);

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void *)(intptr_t)client_fd) != 0) {
            perror("pthread_create failed");
            close(client_fd);
        }
        pthread_detach(thread_id);
    }

    // Clean up
    close(server_fd);
    pthread_mutex_destroy(&lock);
    return EXIT_SUCCESS;
}

void *handle_client(void *arg) {
    int client_fd = (intptr_t)arg;
    char *buffer = calloc(1024, sizeof(char));
    int guesses_left = 6;
    int game_result = -1; // -1 for ongoing, 0 for loss, 1 for win

    printf("THREAD %lu: waiting for guess\n", pthread_self());
    fflush(NULL);

    // Select a random word
    pthread_mutex_lock(&lock);
    char *hidden_word = strdup(*(words + (rand() % wordsInDict)));
    pthread_mutex_unlock(&lock);

    if (hidden_word == NULL) {
        fprintf(stderr, "THREAD %lu: no valid words available; closing TCP connection...\n", pthread_self());
        close(client_fd);
        free(buffer);  // Free the allocated buffer
        pthread_exit(NULL);
    }

    // Game play loop
    while (guesses_left > 0 && game_result == -1) {
        // Clear buffer to avoid carrying over any previous data
        memset(buffer, 0, 1024 * sizeof(char));

        int read_bytes = read(client_fd, buffer, 1023 * sizeof(char));
        if (read_bytes <= 0) {
            printf("THREAD %lu: client gave up; closing TCP connection...\n", pthread_self());
            fflush(NULL);
            break;
        }

        *(buffer + read_bytes) = '\0'; // Ensure null-termination
        //printf("THREAD: rcvd data: %s\n", buffer);  // Debugging print
        fflush(NULL);
        char *guess = str_to_lower(buffer);
        printf("THREAD %lu: rcvd guess: %s\n", pthread_self(), guess);
        fflush(NULL);

        if (!is_valid_guess(guess)) {
            send_response(client_fd, 'N', guesses_left, "?????");
            printf("THREAD %lu: invalid guess; sending reply: ????? (%d guesses left)\n", pthread_self(), guesses_left);
            fflush(NULL);
        } else {
            char *result = evaluate_guess(hidden_word, guess);
            guesses_left--;

            if (strcmp(str_to_lower(result), str_to_lower(hidden_word)) == 0) {
                game_result = 1;
                send_response(client_fd, 'Y', guesses_left, result);
                printf("THREAD %lu: sending reply: %s (%d guesses left)\n", pthread_self(), result, guesses_left);
                printf("THREAD %lu: game over; word was %s!\n", pthread_self(), str_to_upper(hidden_word));
                fflush(NULL);
                free(result);
                break;  // End the game if the word is guessed correctly
            } else {
                send_response(client_fd, 'Y', guesses_left, result);
                printf("THREAD %lu: sending reply: %s (%d guesses left)\n", pthread_self(), result, guesses_left);
                fflush(NULL);
            }
            free(result);
        }
        free(guess);
        printf("THREAD %lu: waiting for guess\n", pthread_self());
        fflush(NULL);
    }

    if (game_result == -1 && guesses_left == 0) {
        printf("THREAD %lu: game over; word was %s!\n", pthread_self(), str_to_upper(hidden_word));
        fflush(NULL);
        game_result = 0;
    }

    update_global_variables(guesses_left, game_result);
    free(hidden_word); // Free the allocated hidden word
    free(buffer);      // Free the allocated buffer
    close(client_fd);
    pthread_exit(NULL);
}

void sigusr1_handler(int signum) {
    printf("MAIN: SIGUSR1 rcvd; Wordle server shutting down...\n");
    fflush(NULL);
    exit(EXIT_SUCCESS);
}

void load_words(const char *filename, int num_words) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("ERROR: opening dictionary file");
        exit(EXIT_FAILURE);
    }

    words = calloc(num_words + 1, sizeof(char *));
    if (!words) {
        perror("ERROR: allocating memory for words");
        exit(EXIT_FAILURE);
    }

    char* line = calloc(256, sizeof(char));
    int i = 0;
    while (fgets(line, 256, file) && i < num_words) {
        // Strip leading and trailing whitespace
        char *start = line;
        while (isspace((unsigned char)*start)) start++;
        char *end = start + strlen(start) - 1;
        while (end > start && isspace((unsigned char)*end)) end--;
        *(end + 1) = '\0';  // Null-terminate the string

        // Duplicate the cleaned word
        *(words+i) = strdup(start);
        i++;
    }
    wordsInDict = i;

    free(line);
    fclose(file);
}

int is_valid_guess(const char *guess) {
    // Ensure guess is a 5-letter word
    if (strlen(guess) != 5) {
        return 0;
    }

    // Check if the guess is in the list of valid words
    for (char **ptr = words; *ptr != NULL; ptr++) {
        if (strcmp(*ptr, guess) == 0) {
            return 1;
        }
    }
    total_guesses++;
    return 0;
}

void update_global_variables(int guesses_left, int game_result) {
    pthread_mutex_lock(&lock);
    if (game_result == 1) {
        total_wins++;
    } else if (guesses_left == 0) {
        total_losses++;
    }
    pthread_mutex_unlock(&lock);
}

char* str_to_lower(const char *str) {
    // Strip leading and trailing whitespace first
    const char *start = str;
    while (isspace((unsigned char)*start)) start++;
    const char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    
    // Calculate the length of the cleaned string
    size_t len = end - start + 1;

    // Allocate memory for the lowercase string
    char *lower_str = calloc(len + 1, sizeof(char));
    
    // Convert to lowercase
    for (size_t i = 0; i < len; i++) {
        *(lower_str+i) = tolower(*(start+i));
    }

    return lower_str;
}

char* str_to_upper(const char *str) {
    // Strip leading and trailing whitespace first
    const char *start = str;
    while (isspace((unsigned char)*start)) start++;
    const char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    
    // Calculate the length of the cleaned string
    size_t len = end - start + 1;

    // Allocate memory for the uppercase string
    char *upper_str = calloc(len + 1, sizeof(char));
    
    // Convert to uppercase
    for (size_t i = 0; i < len; i++) {
        *(upper_str+i) = toupper(*(start+i));
    }

    return upper_str;
}

void send_response(int client_fd, char valid_guess, int guesses_left, const char *result) {
    // Allocate 8 bytes of memory for the response
    char *response = (char *)calloc(9, sizeof(char));
    if (!response) {
        perror("calloc failed");
        return;
    }

    // Fill the response buffer according to the protocol
    *response = valid_guess;  // 1-byte valid guess ('Y' or 'N')

    // Store the guesses remaining (2 bytes, short) in network byte order
    short numGuesses = htons((short)guesses_left);
    memcpy(response + 1, &numGuesses, sizeof(short));
    *(response+9) = '\0';

    // Copy the result (5 bytes)
    memcpy(response + 3, result, 6);

    // Send the 8-byte response to the client
    send(client_fd, response, 9, 0);
    
    // Free the allocated memory
    free(response);
}

char *evaluate_guess(const char *hidden_word, const char *guess) {
    char *result = calloc(6, sizeof(char));
    char *hidden_word_copy = strdup(hidden_word);

    // First pass: find exact matches
    for (int i = 0; i < 5; i++) {
        if (*(guess+i) == *(hidden_word_copy+i)) {
            *(result+i) = toupper(*(guess+i));
            *(hidden_word_copy+i) = '-';
        }
    }

    // Second pass: find misplaced matches
    for (int i = 0; i < 5; i++) {
        if (*(result+i) == '\0') {
            for (int j = 0; j < 5; j++) {
                if (*(guess+i) == *(hidden_word_copy+j) && *(hidden_word_copy+j) != '-') {
                    *(result+i) = tolower(*(guess+i));
                    *(hidden_word_copy+j) = '-';
                    break;
                }
            }
        }
    }

    // Remaining letters are incorrect
    for (int i = 0; i < 5; i++) {
        if (*(result+i) == '\0') {
            *(result+i) = '-';
        }
    }

    free(hidden_word_copy);
    return result;
}
