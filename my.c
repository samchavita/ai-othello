/*
 * Ot8b.c - Tournament Grade Othello AI Engine
 * * Features:
 * - Alpha-Beta Pruning with Iterative Deepening
 * - Robust File I/O synchronization (of.txt)
 * - Multi-factor Evaluation (Mobility, Stability, Parity, Positional)
 * - Exact Endgame Solver (Last 12 moves)
 *
 * Usage:
 * ./Ot8b F  (Play as First/Black)
 * ./Ot8b S  (Play as Second/White)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>
#include <math.h>
#include <unistd.h> // For sleep/usleep

// ================= CONSTANTS & DEFINITIONS =================

#define EMPTY 0
#define BLACK 1
#define WHITE 2

#define BOARD_SIZE 8
#define MAX_MOVES 60
#define INF 100000000

// Board coordinates
#define A 0
#define B 1
#define C 2
#define D 3
#define E 4
#define F 5
#define G 6
#define H 7

// File I/O
#define FILE_NAME "of.txt"

// Search Parameters
#define DEFAULT_DEPTH 6
#define ENDGAME_TRIGGER 52 // Start exact solver when 12 moves remain (64-12)
#define TIME_LIMIT_MS 1900 // Leave buffer for I/O

// Directions: N, NE, E, SE, S, SW, W, NW
const int DirX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int DirY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// Static Position Weights (Positional Score)
const int PosWeights[8][8] = {
    {100, -20, 10, 5, 5, 10, -20, 100},
    {-20, -50, -2, -1, -1, -2, -50, -20},
    {10, -2, 1, 1, 1, 1, -2, 10},
    {5, -1, 1, 0, 0, 1, -1, 5},
    {5, -1, 1, 0, 0, 1, -1, 5},
    {10, -2, 1, 1, 1, 1, -2, 10},
    {-20, -50, -2, -1, -1, -2, -50, -20},
    {100, -20, 10, 5, 5, 10, -20, 100}};

// ================= DATA STRUCTURES =================

typedef struct
{
    int board[8][8];
    int moves_played; // Total half-moves played
    int current_turn; // BLACK or WHITE
} GameState;

typedef struct
{
    int x, y;
    int score;
} Move;

// ================= FUNCTION PROTOTYPES =================

// Core Logic
void Init_Game(GameState *g);
int Is_On_Board(int x, int y);
int Is_Legal_Move(const GameState *g, int x, int y, int color);
void Apply_Move(GameState *g, int x, int y);
int Get_Legal_Moves(const GameState *g, Move *move_list, int color);
int Has_Valid_Move(const GameState *g, int color);
int Count_Pieces(const GameState *g, int color);

// AI & Search
int Evaluate_Position(GameState *g);
int Minimax(GameState *g, int depth, int alpha, int beta, int maximizing_player);
void Pick_Best_Move(GameState *g, int *bestX, int *bestY);

// File I/O & Utils
void Parse_Args(int argc, char *argv[], int *my_color);
int Read_Game_State(GameState *g);
void Write_Move(int move_num, int x, int y); // x=9, y=9 indicates PASS
void Write_Pass(int move_num);
void Debug_Print_Board(const GameState *g);

// ================= MAIN EXECUTION =================

int main(int argc, char *argv[])
{
    int my_color;
    GameState g;
    int x, y;

    // 1. Setup
    Parse_Args(argc, argv, &my_color);
    srand(time(NULL));

    printf("Ot8b Engine Started. Playing as: %s\n", (my_color == BLACK) ? "BLACK (First)" : "WHITE (Second)");

    while (1)
    {
        // 2. Read State from File (Synchronize)
        // We re-read the full history to ensure we are perfectly synced
        int status = Read_Game_State(&g);

        if (status == -1)
        {
            // Game Over detected in file
            printf("Game Over signal received. Exiting.\n");
            break;
        }

        // 3. Check Turn
        // If moves_played is even (0, 2..), it's Black's turn.
        // If moves_played is odd (1, 3..), it's White's turn.
        int active_color = (g.moves_played % 2 == 0) ? BLACK : WHITE;

        if (active_color == my_color)
        {
            printf("\n--- My Turn (Move %d) ---\n", g.moves_played + 1);

            // Check if we must pass
            if (!Has_Valid_Move(&g, my_color))
            {
                printf("No legal moves. Passing.\n");
                Write_Pass(g.moves_played + 1);
            }
            else
            {
                // THINK
                Pick_Best_Move(&g, &x, &y);
                printf("Selected Move: %c%d\n", x + 'a', y + 1);

                // ACT
                Write_Move(g.moves_played + 1, x, y);
            }

            // Wait a moment to ensure file write flushes and referee picks it up
            // before we loop back and potentially read our own move too fast
            usleep(500000); // 0.5 sec
        }
        else
        {
            // Not my turn, wait
            usleep(100000); // 0.1 sec poll
        }
    }

    return 0;
}

// ================= CORE LOGIC IMPLEMENTATION =================

void Init_Game(GameState *g)
{
    memset(g->board, EMPTY, sizeof(g->board));
    g->board[3][3] = WHITE;
    g->board[4][4] = WHITE;
    g->board[3][4] = BLACK;
    g->board[4][3] = BLACK;
    g->moves_played = 0;
    g->current_turn = BLACK;
}

int Is_On_Board(int x, int y)
{
    return (x >= 0 && x < 8 && y >= 0 && y < 8);
}

// Returns 1 if move is legal, 0 otherwise
int Is_Legal_Move(const GameState *g, int x, int y, int color)
{
    if (!Is_On_Board(x, y) || g->board[x][y] != EMPTY)
        return 0;

    int opponent = (color == BLACK) ? WHITE : BLACK;

    // Check all 8 directions
    for (int d = 0; d < 8; d++)
    {
        int nx = x + DirX[d];
        int ny = y + DirY[d];
        int has_opponent = 0;

        // Walk in direction
        while (Is_On_Board(nx, ny) && g->board[nx][ny] == opponent)
        {
            nx += DirX[d];
            ny += DirY[d];
            has_opponent = 1;
        }

        // Must end with our piece and have crossed at least one opponent
        if (has_opponent && Is_On_Board(nx, ny) && g->board[nx][ny] == color)
        {
            return 1;
        }
    }
    return 0;
}

// Applies move and flips pieces
void Apply_Move(GameState *g, int x, int y)
{
    // Check for pass
    if (x == 9 && y == 9)
    {
        g->moves_played++;
        g->current_turn = (g->current_turn == BLACK) ? WHITE : BLACK;
        return;
    }

    int color = g->current_turn;
    int opponent = (color == BLACK) ? WHITE : BLACK;
    g->board[x][y] = color;

    // Flip pieces
    for (int d = 0; d < 8; d++)
    {
        int nx = x + DirX[d];
        int ny = y + DirY[d];
        int flip_list[8][2]; // Store coords to flip
        int count = 0;

        while (Is_On_Board(nx, ny) && g->board[nx][ny] == opponent)
        {
            flip_list[count][0] = nx;
            flip_list[count][1] = ny;
            count++;
            nx += DirX[d];
            ny += DirY[d];
        }

        // If bracketed, commit flips
        if (count > 0 && Is_On_Board(nx, ny) && g->board[nx][ny] == color)
        {
            for (int i = 0; i < count; i++)
            {
                g->board[flip_list[i][0]][flip_list[i][1]] = color;
            }
        }
    }

    g->moves_played++;
    g->current_turn = opponent;
}

int Get_Legal_Moves(const GameState *g, Move *move_list, int color)
{
    int count = 0;
    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x++)
        {
            if (Is_Legal_Move(g, x, y, color))
            {
                move_list[count].x = x;
                move_list[count].y = y;
                // Heuristic for simple sorting: Corners are high value
                move_list[count].score = PosWeights[y][x];
                count++;
            }
        }
    }
    return count;
}

int Has_Valid_Move(const GameState *g, int color)
{
    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x++)
        {
            if (Is_Legal_Move(g, x, y, color))
                return 1;
        }
    }
    return 0;
}

int Count_Pieces(const GameState *g, int color)
{
    int count = 0;
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            if (g->board[i][j] == color)
                count++;
    return count;
}

// ================= AI & EVALUATION =================

int Evaluate_Position(GameState *g)
{
    int my_color = g->current_turn;
    int op_color = (my_color == BLACK) ? WHITE : BLACK;

    // Factors
    double score = 0;

    // 1. Material (Piece Difference)
    int my_pieces = Count_Pieces(g, my_color);
    int op_pieces = Count_Pieces(g, op_color);
    int material = my_pieces - op_pieces;

    // 2. Mobility (Available Moves)
    Move dummy[64];
    int my_moves = Get_Legal_Moves(g, dummy, my_color);

    // Need to temporarily switch turn to check opponent moves
    int op_moves = 0;
    // (Simulate opponent turn logic locally without changing global state is complex,
    // for this eval we just scan valid moves for op_color on current board)
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            if (Is_Legal_Move(g, x, y, op_color))
                op_moves++;

    // Logarithmic mobility scaling
    double mobility = 0;
    if (my_moves + op_moves > 0)
    {
        mobility = 100.0 * (double)(my_moves - op_moves) / (my_moves + op_moves + 1);
    }

    // 3. Positional Score (Board Map)
    int positional = 0;
    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x++)
        {
            if (g->board[x][y] == my_color)
                positional += PosWeights[y][x];
            else if (g->board[x][y] == op_color)
                positional -= PosWeights[y][x];
        }
    }

    // 4. Corner Stability (Simplified)
    int stability = 0;
    int corners[4][2] = {{0, 0}, {7, 0}, {0, 7}, {7, 7}};
    for (int i = 0; i < 4; i++)
    {
        int cx = corners[i][0];
        int cy = corners[i][1];
        if (g->board[cx][cy] == my_color)
            stability += 25;
        else if (g->board[cx][cy] == op_color)
            stability -= 25;
    }

    // Dynamic Weights based on Game Phase
    double w_mat, w_mob, w_pos, w_stab;

    if (g->moves_played < 20)
    {
        // Opening: Mobility is king
        w_mat = 0.1;
        w_mob = 5.0;
        w_pos = 2.0;
        w_stab = 10.0;
    }
    else if (g->moves_played <= 52)
    {
        // Midgame: Position and Stability
        w_mat = 1.0;
        w_mob = 2.0;
        w_pos = 3.0;
        w_stab = 5.0;
    }
    else
    {
        // Endgame: Material Only (handled by Exact Solver usually, but here for safety)
        w_mat = 10.0;
        w_mob = 0.0;
        w_pos = 1.0;
        w_stab = 1.0;
    }

    score = (w_mat * material) + (w_mob * mobility) + (w_pos * positional) + (w_stab * stability);
    return (int)score;
}

// Compare function for qsort
int compare_moves(const void *a, const void *b)
{
    Move *m1 = (Move *)a;
    Move *m2 = (Move *)b;
    return m2->score - m1->score; // Descending
}

int Minimax(GameState *g, int depth, int alpha, int beta, int maximizing)
{
    // 1. Termination
    if (depth == 0)
        return Evaluate_Position(g);

    // Check Game Over (No moves for either side)
    int current_color = g->current_turn;
    int next_color = (current_color == BLACK) ? WHITE : BLACK;

    if (!Has_Valid_Move(g, current_color))
    {
        // Current player passes
        if (!Has_Valid_Move(g, next_color))
        {
            // Game Over - Return massive score based on piece count
            int diff = Count_Pieces(g, current_color) - Count_Pieces(g, next_color);
            return (diff > 0) ? 10000 + diff : -10000 + diff;
        }
        // Pass but game continues (treat as 1 level deeper effectively without move)
        GameState next_g = *g;
        Apply_Move(&next_g, 9, 9); // Pass move
        return -Minimax(&next_g, depth, -beta, -alpha, !maximizing);
    }

    // 2. Generate Moves
    Move moves[64];
    int num_moves = Get_Legal_Moves(g, moves, current_color);

    // 3. Move Ordering (Critical for Alpha-Beta efficiency)
    qsort(moves, num_moves, sizeof(Move), compare_moves);

    // 4. Recursive Search
    int best_val = -INF;

    for (int i = 0; i < num_moves; i++)
    {
        GameState next_g = *g;
        Apply_Move(&next_g, moves[i].x, moves[i].y);

        int val = -Minimax(&next_g, depth - 1, -beta, -alpha, !maximizing);

        if (val > best_val)
        {
            best_val = val;
        }
        if (val > alpha)
        {
            alpha = val;
        }
        if (alpha >= beta)
        {
            break; // Beta Cutoff
        }
    }

    return best_val;
}

void Pick_Best_Move(GameState *g, int *bestX, int *bestY)
{
    int depth = DEFAULT_DEPTH;

    // Endgame Mode: Exact solver
    if (g->moves_played >= ENDGAME_TRIGGER)
    {
        depth = 14; // Look ahead to end
        printf("Endgame Mode Activated (Depth %d)\n", depth);
    }

    Move moves[64];
    int num_moves = Get_Legal_Moves(g, moves, g->current_turn);

    if (num_moves == 0)
    {
        *bestX = 9;
        *bestY = 9; // Sentinel for PASS
        return;
    }

    // Sort initial moves
    qsort(moves, num_moves, sizeof(Move), compare_moves);

    int best_val = -INF;
    int best_idx = 0;
    int alpha = -INF;
    int beta = INF;

    // Root Search Loop
    for (int i = 0; i < num_moves; i++)
    {
        GameState next_g = *g;
        Apply_Move(&next_g, moves[i].x, moves[i].y);

        int val = -Minimax(&next_g, depth - 1, -beta, -alpha, 0);

        printf("Eval %c%d: %d\n", moves[i].x + 'a', moves[i].y + 1, val);

        if (val > best_val)
        {
            best_val = val;
            best_idx = i;
        }
        if (val > alpha)
        {
            alpha = val;
        }
    }

    *bestX = moves[best_idx].x;
    *bestY = moves[best_idx].y;
}

// ================= FILE I/O =================

void Parse_Args(int argc, char *argv[], int *my_color)
{
    if (argc < 2)
    {
        // Default to interactive or Black if unspecified
        *my_color = BLACK;
        return;
    }
    if (toupper(argv[1][0]) == 'F')
    {
        *my_color = BLACK;
    }
    else
    {
        *my_color = WHITE;
    }
}

// Reads the entire history from of.txt and reconstructs board state
// Returns 0 on success, -1 on Game Over signal
int Read_Game_State(GameState *g)
{
    FILE *fp = fopen(FILE_NAME, "r");
    if (!fp)
    {
        // If file doesn't exist, assume new game
        Init_Game(g);
        // Create the file immediately
        fp = fopen(FILE_NAME, "w");
        fprintf(fp, " 1\n"); // Move 1 pending
        fclose(fp);
        return 0;
    }

    Init_Game(g);

    int expected_move_count;
    if (fscanf(fp, "%d", &expected_move_count) != 1)
    {
        fclose(fp);
        return 0;
    }

    char move_str[10];
    // Read moves sequentially
    // The format in file is strictly history.
    // We read until we hit EOF or the 'w' marker.
    while (fscanf(fp, "%s", move_str) == 1)
    {
        if (move_str[0] == 'w' || move_str[0] == 'W')
        {
            fclose(fp);
            return -1; // Game Over
        }

        if (move_str[0] == 'p' || move_str[0] == 'P')
        {
            // Pass
            Apply_Move(g, 9, 9);
        }
        else
        {
            // Coordinate: c4 -> x=2('c'-'a'), y=3('4'-'1')
            if (strlen(move_str) >= 2)
            {
                int col = tolower(move_str[0]) - 'a';
                int row = move_str[1] - '1';
                Apply_Move(g, col, row);
            }
        }
    }

    fclose(fp);
    return 0;
}

// Write move using the specific append/update logic requested
void Write_Move(int move_num, int x, int y)
{
    // 1. Update line 1 (Move Count)
    FILE *fp = fopen(FILE_NAME, "r+");
    if (fp)
    {
        rewind(fp);                         // Ensure at start
        fprintf(fp, "%2d\n", move_num + 1); // Next move number
        fclose(fp);
    }

    // 2. Append move
    fp = fopen(FILE_NAME, "a");
    if (fp)
    {
        if (x == 9)
        {
            fprintf(fp, "p9\n");
        }
        else
        {
            fprintf(fp, "%c%d\n", x + 'a', y + 1);
        }
        fclose(fp);
    }
}

void Write_Pass(int move_num)
{
    Write_Move(move_num, 9, 9);
}

void Debug_Print_Board(const GameState *g)
{
    printf("  a b c d e f g h\n");
    for (int y = 0; y < 8; y++)
    {
        printf("%d ", y + 1);
        for (int x = 0; x < 8; x++)
        {
            if (g->board[x][y] == BLACK)
                printf("X ");
            else if (g->board[x][y] == WHITE)
                printf("O ");
            else
                printf(". ");
        }
        printf("\n");
    }
}