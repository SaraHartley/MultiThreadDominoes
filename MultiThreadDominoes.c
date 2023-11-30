#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>

#define MAX_DOMINOES 28  // Maximum number of dominoes in the game
#define HAND_SIZE 7      // Number of dominoes each player starts with
#define NUM_PLAYERS 3    // Number of players in the game

// Define a struct for a domino, with left and right values
typedef struct {
    int left;
    int right;
} Domino;

// Define a struct for a player, including their hand of dominoes, hand size, and score
typedef struct {
    Domino hand[HAND_SIZE];
    int hand_size;
    int score;
} Player;

// Global variables
Domino dominoes[MAX_DOMINOES];
Domino drawPile[MAX_DOMINOES - NUM_PLAYERS * HAND_SIZE];
int drawPileSize = 0;
Player players[NUM_PLAYERS];
pthread_mutex_t lock;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int game_over = 0; 


// Global variables representing the open ends of the domino chain
int openEnd1 = -1;
int openEnd2 = -1;
int current_turn = 0;

// Function declarations
void initialize_dominoes();
void shuffle_dominoes();
void initialize_drawPile();
Domino draw_from_pile();
void deal_dominoes();
void print_domino(Domino d);
int can_play_domino(Domino domino);
void update_open_ends(Domino playedDomino);
void *play_dominoes(void *arg);
int determine_first_player();

// Function to determine if a domino can be played based on the open ends
int can_play_domino(Domino domino) {
    return domino.left == openEnd1 || domino.right == openEnd1 || 
           domino.left == openEnd2 || domino.right == openEnd2;
}

// Function to check if the player can play any domino from their hand
int can_play(Player *player) {
    for (int i = 0; i < player->hand_size; i++) {
        if (can_play_domino(player->hand[i])) {
            return 1; // Can play
        }
    }
    return 0; // Cannot play
}

// Function to check if the player can play any domino from their hand
void remove_domino_from_hand(Player *player, int index) {
    for (int j = index; j < player->hand_size - 1; j++) {
        player->hand[j] = player->hand[j + 1];
    }
    player->hand_size--;
}

// Function to calculate a player's score based on the dominoes in their hand
int calculate_score(Player *player) {
    int score = 0;
    for (int i = 0; i < player->hand_size; i++) {
        score += player->hand[i].left + player->hand[i].right;
    }
    return score;
}

// Function to update the open ends of the domino chain when a domino is played
void update_open_ends(Domino playedDomino) {
    if (openEnd1 == -1 && openEnd2 == -1) {
        openEnd1 = playedDomino.left;
        openEnd2 = playedDomino.right;
    } else {
        if (playedDomino.left == openEnd1 || playedDomino.right == openEnd1) {
            openEnd1 = (playedDomino.left == openEnd1) ? playedDomino.right : playedDomino.left;
        }
        if (playedDomino.left == openEnd2 || playedDomino.right == openEnd2) {
            openEnd2 = (playedDomino.left == openEnd2) ? playedDomino.right : playedDomino.left;
        }
    }
}

// Function to initialize all dominoes at the start of the game
void initialize_dominoes() {
    int count = 0;
    for (int i = 0; i <= 6; i++) {
        for (int j = i; j <= 6; j++) {
            dominoes[count].left = i;
            dominoes[count].right = j;
            count++;
        }
    }
}

// Function to shuffle the dominoes randomly
void shuffle_dominoes() {
    for (int i = 0; i < MAX_DOMINOES; i++) {
        int j = rand() % MAX_DOMINOES;
        Domino temp = dominoes[i];
        dominoes[i] = dominoes[j];
        dominoes[j] = temp;
    }
}

// Function to initialize the draw pile
void initialize_drawPile() {
    int count = 0;
    for (int i = NUM_PLAYERS * HAND_SIZE; i < MAX_DOMINOES; i++) {
        drawPile[count++] = dominoes[i];
    }
    drawPileSize = count;
}

// Function to draw a domino from the draw pile
Domino draw_from_pile() {
    if (drawPileSize > 0) {
        int index = rand() % drawPileSize;
        Domino drawn = drawPile[index];
        for (int i = index; i < drawPileSize - 1; i++) {
            drawPile[i] = drawPile[i + 1];
        }
        drawPileSize--;
        return drawn;
    } else {
        return (Domino){-1, -1};  // Return an invalid domino if draw pile is empty
    }
}

// Function to deal dominoes to each player at the start of the game
void deal_dominoes() {
    int d = 0;
    for (int i = 0; i < NUM_PLAYERS; i++) {
        for (int j = 0; j < HAND_SIZE; j++) {
            if (d < MAX_DOMINOES) {
                players[i].hand[j] = dominoes[d++];
                players[i].hand_size++;
            }
        }
    }
}

// Function to determine the first player based on the highest double or highest score
int determine_first_player() {
    int highestDouble = -1, highestScore = -1, startingPlayer = -1;

    for (int i = 0; i < NUM_PLAYERS; i++) {
        for (int j = 0; j < players[i].hand_size; j++) {
            int left = players[i].hand[j].left;
            int right = players[i].hand[j].right;

            if (left == right && left > highestDouble) {
                highestDouble = left;
                startingPlayer = i;
            } else if (highestDouble == -1 && left + right > highestScore) {
                highestScore = left + right;
                startingPlayer = i;
            }
        }
    }
    return startingPlayer;
}


// Function executed by each thread representing a player's turn
void *play_dominoes(void *arg) {
    Player *player = (Player *)arg;
    int player_number = ((int)(player - players)) + 1;

    while (1) {
        pthread_mutex_lock(&lock);

        while (current_turn != player_number && !game_over) {
            pthread_cond_wait(&cond, &lock);
        }

        if (game_over) {
            pthread_mutex_unlock(&lock);
            break;
        }
        int played = 0;
        Domino drawn;

        // Check if player has no dominos left, which indicates game over
        if (player->hand_size == 0) { 
            game_over = 1;
            pthread_cond_broadcast(&cond); 
            pthread_mutex_unlock(&lock);
            break;
        }
        
        // Iterate through player's hand to find a playable domino
        for (int i = 0; i < player->hand_size; i++) {
            if (can_play_domino(player->hand[i])) {
                printf("Player %d plays: [%d|%d]\n", player_number, player->hand[i].left, player->hand[i].right);
                update_open_ends(player->hand[i]);
                remove_domino_from_hand(player, i);
                played = 1;
                break;
            }
        }

        // Draw a domino from the pile if the player hasn't played and the pile is not empty
        if (!played && drawPileSize > 0) {
            drawn = draw_from_pile();
            printf("Player %d draws from the pile: [%d|%d]\n", player_number, drawn.left, drawn.right);

            // Check if the drawn domino can be played immediately
            if (!can_play_domino(drawn)) {
                player->hand[player->hand_size] = drawn;
                player->hand_size++;
                printf("Player %d adds drawn domino to hand\n", player_number);
            } else {
                printf("Player %d plays drawn domino: [%d|%d]\n", player_number, drawn.left, drawn.right);
                update_open_ends(drawn);
                played = 1;
            }
        }

        // Handle the case where the player cannot play and the draw pile is empty
        if (!played && drawPileSize == 0 && !can_play(player)) {
            printf("Player %d cannot play and the draw pile is empty. Passing turn.\n", player_number);
            current_turn = (current_turn % NUM_PLAYERS) + 1;
            pthread_cond_broadcast(&cond);
            
            // Check if all players have passed in a round, which indicates game over
            if (player_number == NUM_PLAYERS) {  
                game_over = 1;
            }
        } else if (played || !can_play(player)) {
            current_turn = (current_turn % NUM_PLAYERS) + 1;
            pthread_cond_broadcast(&cond);
        }
        
        pthread_mutex_unlock(&lock);
    }

    player->score = calculate_score(player);
    return NULL;
}




int main() {
    srand(time(NULL));
    pthread_mutex_init(&lock, NULL);

    // Initialize the game elements
    initialize_dominoes();
    shuffle_dominoes();
    deal_dominoes();
    initialize_drawPile();
    game_over = 0;

    pthread_mutex_lock(&lock);

    int firstPlayer = determine_first_player();
    printf("Player %d starts the game.\n", firstPlayer + 1);
    current_turn = firstPlayer + 1;

    Domino firstDomino = players[firstPlayer].hand[0];
    printf("Player %d plays first domino: [%d|%d]\n", firstPlayer + 1, firstDomino.left, firstDomino.right);
    update_open_ends(firstDomino);
    remove_domino_from_hand(&players[firstPlayer], 0);
    current_turn = (current_turn % NUM_PLAYERS) + 1;
    
    if (players[firstPlayer].hand_size == 0) {
        game_over = 1;
    }

    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);

    pthread_t threads[NUM_PLAYERS];
    for (int i = 0; i < NUM_PLAYERS; i++) {
        pthread_create(&threads[i], NULL, play_dominoes, (void *)&players[i]);
    }

    for (int i = 0; i < NUM_PLAYERS; i++) {
        pthread_join(threads[i], NULL);
    }
        // Calculate and print scores for each player
    for (int i = 0; i < NUM_PLAYERS; i++) {
        players[i].score = calculate_score(&players[i]);
        printf("Player %d scores %d\n", i + 1, players[i].score);
    }

    // Determine the winner based on the lowest score
    int lowest_score = INT_MAX;
    int winners[NUM_PLAYERS] = {0};
    int num_winners = 0;
    for (int i = 0; i < NUM_PLAYERS; i++) {
        if (players[i].score < lowest_score) {
            lowest_score = players[i].score;
            memset(winners, 0, sizeof(winners));
            winners[i] = 1;
            num_winners = 1;
        } else if (players[i].score == lowest_score) {
            winners[i] = 1;
            num_winners++;
        }
    }

    // Announce the winner or a tie
    if (num_winners == 1) {
        // Loop through the players to find and announce the single winner
        for (int i = 0; i < NUM_PLAYERS; i++) {
            if (winners[i]) {
                printf("Player %d wins!\n", i + 1);
                break; // Exit loop once winner is found
            }
        }
    } else if (num_winners > 1) {
        // Handle the case of a tie between multiple players
        printf("It's a tie between ");
        for (int i = 0, count = 0; i < NUM_PLAYERS; i++) {
            if (winners[i]) {
                printf("Player %d", i + 1);
                count++;
                if (count < num_winners) {
                    printf(", ");
                } else {
                    printf(".\n");
                }
            }
        }
    }
    
    // Broadcast a final signal to ensure all threads are synchronized before destruction
    pthread_cond_broadcast(&cond);
    
    // Clean up resources by destroying mutex and condition variable
    pthread_mutex_destroy(&lock);
    pthread_cond_destroy(&cond);
    
    return 0;
}
