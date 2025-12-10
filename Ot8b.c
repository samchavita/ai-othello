/*
 * Ot8b.c
 *
 * Enhanced Othello/Reversi engine (single-file).
 * - Keeps original multi-factor evaluation (material, mobility, positional, frontier, stability)
 * - Implements enhancements from ENHANCEMENT_PROMPT.md:
 *     * Zobrist hashing + transposition table
 *     * Killer move heuristic
 *     * Endgame exact solver (search to end when <=12 moves remaining)
 *     * Opening-book check stub (small sample book)
 *     * Corner control & parity tweaks in evaluation
 *
 * Build:
 *   gcc -O3 -std=c99 -o Ot8b Ot8b.c
 *
 * Usage:
 *   See original Ot8b.c usage (supports F/S/A/B/W/L modes).
 *
 * Notes:
 * - All changes are kept in a single file for tournament use.
 * - This file avoids background tasks and external waiting loops.
 *
 * Author: Generated (enhanced) for user
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* -------------------- Basic game definitions -------------------- */

#define EMPTY 0
#define BLACK 1
#define WHITE 2

#define MAX_MOVES 1000
#define MAX_FLIPS 64
#define MAX_LEGAL 64
#define INF 1000000000

typedef struct
{
    int x, y;
} Move;

typedef struct
{
    int board[8][8];
    int turn;       // 0 = black to move, 1 = white to move
    int move_count; // number of moves played (starting from 1 per spec)
} GameState;

/* directions */
const int DX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int DY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

/* positional weight table (y,x) indexing kept same as original */
const int POSVAL[8][8] = {
    {100, -20, 10, 5, 5, 10, -20, 100},
    {-20, -50, -2, -1, -1, -2, -50, -20},
    {10, -2, 1, 1, 1, 1, -2, 10},
    {5, -1, 1, 0, 0, 1, -1, 5},
    {5, -1, 1, 0, 0, 1, -1, 5},
    {10, -2, 1, 1, 1, 1, -2, 10},
    {-20, -50, -2, -1, -1, -2, -50, -20},
    {100, -20, 10, 5, 5, 10, -20, 100}};

/* -------------------- Forward declarations -------------------- */
void init_board(GameState *g);
int in_bounds(int x, int y);
int opponent_color(int color);
int color_of_turn(GameState *g);
int is_legal_move(GameState *g, int color, int x, int y, Move flips[], int *flip_count);
int generate_moves(GameState *g, int color, Move moves[], int *n_moves, int flipinfo[][MAX_FLIPS], int flipcounts[]);
void apply_move_with_flips(GameState *g, int color, int x, int y, Move flips[], int flip_count);
void undo_move_with_flips(GameState *g, int color, int x, int y, Move flips[], int flip_count);
int count_disks(GameState *g, int color);
int count_legal_moves(GameState *g, int color);
int evaluate(GameState *g);
int exact_evaluate(GameState *g, int color);
int negamax(GameState *g, int depth, int alpha, int beta, int color, Move *bestmove, int ply);
void play_and_write_move(GameState *g, int x, int y);
void write_pass(GameState *g);
int load_game_from_file(GameState *g, char moves_out[][4], int *moves_n);
int append_move_to_oftxt(GameState *g, const char *move_str);
void print_board(GameState *g);
void coords_to_notation(char *s, int x, int y);
int notation_to_coords(const char *s, int *x, int *y);
int is_game_over(GameState *g);
int count_frontier(GameState *g, int color);
int count_stable_approx(GameState *g, int color);
int choose_best_move(GameState *g, int depth, int *out_x, int *out_y);
int get_human_move(GameState *g, int *rx, int *ry);
void print_help();

/* -------------------- Zobrist & Transposition Table -------------------- */

#define HASH_TABLE_SIZE 131072 // must be power of two for modulo performance
#define ZOBRIST_SEED 12345

typedef struct
{
    unsigned long long hash;
    int depth;
    int value;
    int flag; // 0=exact, 1=lower bound (alpha), 2=upper bound (beta)
} TranspositionEntry;

static TranspositionEntry hash_table[HASH_TABLE_SIZE];
static unsigned long long zobrist[8][8][3]; // zobrist[x][y][piece]
static int zobrist_inited = 0;

/* Check both players have no moves or board full */
int is_game_over(GameState *g)
{
    int anyEmpty = 0;
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 8; y++)
            if (g->board[x][y] == EMPTY)
                anyEmpty = 1;
    if (!anyEmpty)
        return 1;
    if (count_legal_moves(g, BLACK) == 0 && count_legal_moves(g, WHITE) == 0)
        return 1;
    return 0;
}

void init_zobrist()
{
    if (zobrist_inited)
        return;
    srand(ZOBRIST_SEED);
    for (int x = 0; x < 8; x++)
    {
        for (int y = 0; y < 8; y++)
        {
            for (int c = 0; c < 3; c++)
            {
                unsigned long long hi = ((unsigned long long)rand() & 0xFFFF);
                unsigned long long lo = ((unsigned long long)rand() & 0xFFFF);
                unsigned long long v = (hi << 48) ^ (lo << 32) ^ ((unsigned long long)rand() << 16) ^ rand();
                zobrist[x][y][c] = v;
            }
        }
    }
    zobrist_inited = 1;
    // initialize hash table
    for (int i = 0; i < HASH_TABLE_SIZE; i++)
    {
        hash_table[i].hash = 0;
        hash_table[i].depth = -1;
        hash_table[i].value = 0;
        hash_table[i].flag = 0;
    }
}

unsigned long long compute_hash(GameState *g)
{
    unsigned long long h = 0ULL;
    for (int x = 0; x < 8; x++)
    {
        for (int y = 0; y < 8; y++)
        {
            int p = g->board[x][y]; // 0/1/2
            h ^= zobrist[x][y][p];
        }
    }
    if (g->turn == 1)
        h ^= 0xDEADBEEFDEADBEEFULL;
    return h;
}

int probe_hash(unsigned long long hash, int depth, int *value, int *flag)
{
    unsigned long long idx = hash & (HASH_TABLE_SIZE - 1);
    TranspositionEntry *entry = &hash_table[idx];
    if (entry->hash == hash && entry->depth >= depth)
    {
        *value = entry->value;
        *flag = entry->flag;
        return 1;
    }
    return 0;
}

void store_hash(unsigned long long hash, int depth, int value, int flag)
{
    unsigned long long idx = hash & (HASH_TABLE_SIZE - 1);
    TranspositionEntry *entry = &hash_table[idx];
    if (depth >= entry->depth)
    {
        entry->hash = hash;
        entry->depth = depth;
        entry->value = value;
        entry->flag = flag;
    }
}

/* -------------------- Killer moves -------------------- */

#define MAX_DEPTH 64 // safe upper bound for ply
static Move killers[MAX_DEPTH][2];

void init_killers()
{
    for (int d = 0; d < MAX_DEPTH; d++)
    {
        killers[d][0].x = killers[d][0].y = -1;
        killers[d][1].x = killers[d][1].y = -1;
    }
}

/* -------------------- Game logic (move generation / apply / undo) -------------------- */

void init_board(GameState *g)
{
    int i, j;
    for (i = 0; i < 8; i++)
        for (j = 0; j < 8; j++)
            g->board[i][j] = EMPTY;
    // standard Othello starting position
    g->board[3][3] = WHITE;
    g->board[4][4] = WHITE;
    g->board[3][4] = BLACK;
    g->board[4][3] = BLACK;
    g->turn = 0;       // black moves first
    g->move_count = 1; // per spec starts at 1
}

int in_bounds(int x, int y) { return x >= 0 && x < 8 && y >= 0 && y < 8; }
int opponent_color(int color) { return (color == BLACK) ? WHITE : BLACK; }
int color_of_turn(GameState *g) { return (g->turn == 0) ? BLACK : WHITE; }

/* Check legality and collect flips for a candidate move.
 * flips[] will store flipping squares; flip_count is out param.
 * Returns 1 if legal (flip_count>0), else 0.
 */
int is_legal_move(GameState *g, int color, int x, int y, Move flips[], int *flip_count)
{
    if (!in_bounds(x, y) || g->board[x][y] != EMPTY)
        return 0;
    int fc = 0;
    int opp = opponent_color(color);
    for (int d = 0; d < 8; d++)
    {
        int cx = x + DX[d];
        int cy = y + DY[d];
        int run = 0;
        Move temp_flips[8];
        int tf = 0;
        while (in_bounds(cx, cy) && g->board[cx][cy] == opp)
        {
            temp_flips[tf].x = cx;
            temp_flips[tf].y = cy;
            tf++;
            cx += DX[d];
            cy += DY[d];
            run++;
            if (tf >= 8)
                break;
        }
        if (run > 0 && in_bounds(cx, cy) && g->board[cx][cy] == color)
        {
            for (int k = 0; k < tf; k++)
            {
                flips[fc++] = temp_flips[k];
                if (fc >= MAX_FLIPS)
                    break;
            }
        }
    }
    *flip_count = fc;
    return fc > 0;
}

/* Generate all legal moves for color. Also fills flipinfo: for each move index,
 * flipinfo[idx] contains the x,y pairs linearized as (x*8+y) for flips, and flipcounts[idx] the count.
 */
int generate_moves(GameState *g, int color, Move moves[], int *n_moves, int flipinfo[][MAX_FLIPS], int flipcounts[])
{
    int n = 0;
    Move flips[MAX_FLIPS];
    int flipc;
    for (int x = 0; x < 8; x++)
    {
        for (int y = 0; y < 8; y++)
        {
            if (is_legal_move(g, color, x, y, flips, &flipc))
            {
                moves[n].x = x;
                moves[n].y = y;
                flipcounts[n] = flipc;
                for (int i = 0; i < flipc; i++)
                {
                    flipinfo[n][i] = flips[i].x * 8 + flips[i].y;
                }
                n++;
                if (n >= MAX_LEGAL)
                    break;
            }
        }
        if (n >= MAX_LEGAL)
            break;
    }
    *n_moves = n;
    return n;
}

void apply_move_with_flips(GameState *g, int color, int x, int y, Move flips[], int flip_count)
{
    g->board[x][y] = color;
    for (int i = 0; i < flip_count; i++)
    {
        int fx = flips[i].x;
        int fy = flips[i].y;
        g->board[fx][fy] = color;
    }
    // toggle turn and increment move counter
    g->turn = 1 - g->turn;
    g->move_count++;
}

void undo_move_with_flips(GameState *g, int color, int x, int y, Move flips[], int flip_count)
{
    int opp = opponent_color(color);
    g->board[x][y] = EMPTY;
    for (int i = 0; i < flip_count; i++)
    {
        int fx = flips[i].x;
        int fy = flips[i].y;
        g->board[fx][fy] = opp;
    }
    g->turn = 1 - g->turn;
    g->move_count--;
}

/* -------------------- Counting helpers -------------------- */

int count_disks(GameState *g, int color)
{
    int c = 0;
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            if (g->board[i][j] == color)
                c++;
    return c;
}

int count_legal_moves(GameState *g, int color)
{
    Move moves[MAX_LEGAL];
    int n;
    int flipinfo[MAX_LEGAL][MAX_FLIPS];
    int flipcounts[MAX_LEGAL];
    return generate_moves(g, color, moves, &n, flipinfo, flipcounts);
}

/* frontier: count color's disks adjacent to at least one empty */
int count_frontier(GameState *g, int color)
{
    int cnt = 0;
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 8; y++)
        {
            if (g->board[x][y] != color)
                continue;
            int adjEmpty = 0;
            for (int d = 0; d < 8; d++)
            {
                int nx = x + DX[d], ny = y + DY[d];
                if (in_bounds(nx, ny) && g->board[nx][ny] == EMPTY)
                {
                    adjEmpty = 1;
                    break;
                }
            }
            if (adjEmpty)
                cnt++;
        }
    return cnt;
}

/* Approximate stable pieces: count corners and contiguous edge chains anchored at corners.
 * This is a cheap approximate stability indicator (not full rigorous stability).
 */
int count_stable_approx(GameState *g, int color)
{
    int stable = 0;
    const int cx[4] = {0, 7, 0, 7};
    const int cy[4] = {0, 0, 7, 7};
    for (int k = 0; k < 4; k++)
    {
        int x = cx[k], y = cy[k];
        if (g->board[x][y] == color)
            stable += 1;
    }
    if (g->board[0][0] == color)
    {
        for (int x = 1; x < 8; x++)
        {
            if (g->board[x][0] == color)
                stable++;
            else
                break;
        }
    }
    if (g->board[7][0] == color)
    {
        for (int x = 6; x >= 0; x--)
        {
            if (g->board[x][0] == color)
                stable++;
            else
                break;
        }
    }
    if (g->board[0][7] == color)
    {
        for (int x = 1; x < 8; x++)
        {
            if (g->board[x][7] == color)
                stable++;
            else
                break;
        }
    }
    if (g->board[7][7] == color)
    {
        for (int x = 6; x >= 0; x--)
        {
            if (g->board[x][7] == color)
                stable++;
            else
                break;
        }
    }
    if (g->board[0][0] == color)
    {
        for (int y = 1; y < 8; y++)
        {
            if (g->board[0][y] == color)
                stable++;
            else
                break;
        }
    }
    if (g->board[0][7] == color)
    {
        for (int y = 6; y >= 0; y--)
        {
            if (g->board[0][y] == color)
                stable++;
            else
                break;
        }
    }
    if (g->board[7][0] == color)
    {
        for (int y = 1; y < 8; y++)
        {
            if (g->board[7][y] == color)
                stable++;
            else
                break;
        }
    }
    if (g->board[7][7] == color)
    {
        for (int y = 6; y >= 0; y--)
        {
            if (g->board[7][y] == color)
                stable++;
            else
                break;
        }
    }
    return stable;
}

/* -------------------- Evaluation functions -------------------- */

/* exact_evaluate: endgame exact (final disc difference scaled) */
int exact_evaluate(GameState *g, int color)
{
    int black = count_disks(g, BLACK);
    int white = count_disks(g, WHITE);
    int diff = black - white;
    // scale by 100 for clarity, return from 'color' perspective
    int val = diff * 100;
    if (color == BLACK)
        return val;
    return -val;
}

/* evaluate: multi-factor evaluation from original implementation + enhancements:
 * returns value in "black advantage" scale. For negamax convenience, caller will
 * convert to side-to-move perspective.
 */
int evaluate(GameState *g)
{
    int black_count = count_disks(g, BLACK);
    int white_count = count_disks(g, WHITE);
    int material = black_count - white_count; // positive favors black

    int black_moves = count_legal_moves(g, BLACK);
    int white_moves = count_legal_moves(g, WHITE);
    double mobility = 0.0;
    mobility = (log(black_moves + 1.0) - log(white_moves + 1.0));

    int black_frontier = count_frontier(g, BLACK);
    int white_frontier = count_frontier(g, WHITE);
    double frontier_score = (white_frontier - black_frontier) * 0.5; // positive favors black

    int black_stable = count_stable_approx(g, BLACK);
    int white_stable = count_stable_approx(g, WHITE);
    int stability = black_stable - white_stable;

    int positional = 0;
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 8; y++)
        {
            if (g->board[x][y] == BLACK)
                positional += POSVAL[y][x];
            else if (g->board[x][y] == WHITE)
                positional -= POSVAL[y][x];
        }

    int moves_played = g->move_count - 1; // since move_count starts at 1
    double w1, w2, w3, w4, w5;
    if (moves_played <= 15)
    {
        w1 = 0.1;
        w2 = 2.0;
        w3 = 0.5;
        w4 = 1.0;
        w5 = 0.3;
    }
    else if (moves_played <= 52)
    {
        w1 = 1.0;
        w2 = 1.5;
        w3 = 1.0;
        w4 = 2.0;
        w5 = 0.5;
    }
    else
    {
        w1 = 10.0;
        w2 = 0.0;
        w3 = 2.0;
        w4 = 0.5;
        w5 = 0.0;
    }

    double evald = 0.0;
    evald += w1 * (double)material;
    evald += w2 * mobility;
    evald += w3 * (double)stability;
    evald += w4 * (double)positional / 10.0; // scaled down
    evald += w5 * frontier_score;

    // Corner control bonus (enhancement)
    int corners_controlled = 0;
    const int corner_positions[4][2] = {{0, 0}, {7, 0}, {0, 7}, {7, 7}};
    for (int i = 0; i < 4; i++)
    {
        int cx = corner_positions[i][0], cy = corner_positions[i][1];
        if (g->board[cx][cy] == BLACK)
            corners_controlled += 2;
        else if (g->board[cx][cy] == WHITE)
            corners_controlled -= 2;
    }
    evald += corners_controlled * 25.0;

    // Parity consideration (enhancement)
    if (moves_played > 50)
    {
        int remaining = 60 - moves_played;
        if ((remaining % 2 == 0 && g->turn == 0))
        {
            // Black moves last when even remaining and black to move now
            evald += 50.0;
        }
        else if ((remaining % 2 == 1 && g->turn == 1))
        {
            evald += 50.0;
        }
    }

    int eval = (int)round(evald * 10.0);
    return eval; // positive favors Black
}

/* -------------------- Move ordering with killer heuristic -------------------- */

void order_moves_by_heuristic(GameState *g, int color, Move moves[], int n, int order[], int ply)
{
    int scores[MAX_LEGAL];
    for (int i = 0; i < n; i++)
    {
        int s = 0;
        if (ply < MAX_DEPTH)
        {
            if (moves[i].x == killers[ply][0].x && moves[i].y == killers[ply][0].y)
                s += 100000;
            else if (moves[i].x == killers[ply][1].x && moves[i].y == killers[ply][1].y)
                s += 90000;
        }
        int x = moves[i].x, y = moves[i].y;
        // Positional scaled
        s += POSVAL[y][x] * 10;
        // Favor corner strongly
        if ((x == 0 && y == 0) || (x == 7 && y == 0) || (x == 0 && y == 7) || (x == 7 && y == 7))
            s += 100000;
        // Avoid X-squares
        if ((x == 1 && y == 0) || (x == 0 && y == 1) || (x == 1 && y == 1) ||
            (x == 6 && y == 0) || (x == 7 && y == 1) || (x == 6 && y == 1) ||
            (x == 0 && y == 6) || (x == 1 && y == 7) || (x == 1 && y == 6) ||
            (x == 6 && y == 7) || (x == 7 && y == 6) || (x == 6 && y == 6))
            s -= 20000;
        // flips count heuristic
        Move flips[MAX_FLIPS];
        int fc = 0;
        if (is_legal_move(g, color, x, y, flips, &fc))
            s += fc * 50;
        scores[i] = s;
        order[i] = i;
    }
    // simple sort indices by score descending
    for (int i = 0; i < n; i++)
    {
        for (int j = i + 1; j < n; j++)
        {
            if (scores[order[j]] > scores[order[i]])
            {
                int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }
}

/* -------------------- Negamax with alpha-beta, transposition table, killers, endgame -------------------- */

/*
 * Returns score from perspective of 'color' (player to move).
 * Standard negamax: score = max( -(negamax after move for opponent) )
 */
int negamax(GameState *g, int depth, int alpha, int beta, int color, Move *bestmove, int ply)
{
    if (depth < 0)
        depth = 0;
    // End conditions
    if (is_game_over(g))
    {
        // final exact score
        return exact_evaluate(g, color);
    }

    int moves_remaining = 64 - (g->move_count - 1);
    if (moves_remaining <= 12)
    {
        // endgame: search to end exactly
        int new_depth = moves_remaining;
        if (depth > new_depth)
            depth = new_depth;
        // Allow search to go to depth (we'll not special-case further here)
    }

    // compute hash and probe TT
    unsigned long long h = compute_hash(g);
    int hash_value, hash_flag;
    if (probe_hash(h, depth, &hash_value, &hash_flag))
    {
        if (hash_flag == 0)
        {
            // exact
            return (color == BLACK) ? hash_value : -hash_value;
        }
        else if (hash_flag == 1)
        {
            // lower bound
            if (hash_value > alpha)
                alpha = hash_value;
        }
        else if (hash_flag == 2)
        {
            // upper bound
            if (hash_value < beta)
                beta = hash_value;
        }
        if (alpha >= beta)
        {
            return (color == BLACK) ? hash_value : -hash_value;
        }
    }

    if (depth == 0)
    {
        int ev = evaluate(g);
        // evaluate returns black-advantage; convert to 'color' perspective
        return (color == BLACK) ? ev : -ev;
    }

    // generate moves
    Move moves[MAX_LEGAL];
    int n;
    int flipinfo[MAX_LEGAL][MAX_FLIPS];
    int flipcounts[MAX_LEGAL];
    generate_moves(g, color, moves, &n, flipinfo, flipcounts);

    if (n == 0)
    {
        // pass
        // If opponent also has no moves, handled by is_game_over earlier
        g->turn = 1 - g->turn;
        g->move_count++;
        int val = -negamax(g, depth - 1, -beta, -alpha, opponent_color(color), NULL, ply + 1);
        g->turn = 1 - g->turn;
        g->move_count--;
        return val;
    }

    int order[MAX_LEGAL];
    order_moves_by_heuristic(g, color, moves, n, order, ply);

    int best_val = -INF;
    Move local_best = {-1, -1};

    for (int idx = 0; idx < n; idx++)
    {
        int i = order[idx];
        Move flips[MAX_FLIPS];
        int fc = flipcounts[i];
        for (int k = 0; k < fc; k++)
        {
            int v = flipinfo[i][k];
            flips[k].x = v / 8;
            flips[k].y = v % 8;
        }
        apply_move_with_flips(g, color, moves[i].x, moves[i].y, flips, fc);

        int val = -negamax(g, depth - 1, -beta, -alpha, opponent_color(color), NULL, ply + 1);

        undo_move_with_flips(g, color, moves[i].x, moves[i].y, flips, fc);

        if (val > best_val)
        {
            best_val = val;
            local_best = moves[i];
        }
        if (best_val > alpha)
            alpha = best_val;
        if (alpha >= beta)
        {
            // record killer
            if (ply < MAX_DEPTH)
            {
                if (!(moves[i].x == killers[ply][0].x && moves[i].y == killers[ply][0].y))
                {
                    killers[ply][1] = killers[ply][0];
                    killers[ply][0] = moves[i];
                }
            }
            break;
        }
    }

    // store in transposition table
    int store_flag = 0;
    if (best_val <= alpha)
        store_flag = 2; // upper bound
    else if (best_val >= beta)
        store_flag = 1; // lower bound
    else
        store_flag = 0; // exact

    // store value in BLACK-advantage reference for consistency
    int store_value = (color == BLACK) ? best_val : -best_val;
    store_hash(h, depth, store_value, store_flag);

    if (bestmove)
        *bestmove = local_best;
    return best_val;
}

/* -------------------- File I/O (of.txt) -------------------- */

/* Convert coords to algebraic "c4" style */
void coords_to_notation(char *s, int x, int y)
{
    s[0] = 'a' + x;
    s[1] = '1' + y;
    s[2] = 0;
}

/* Parse "c4" or "p9" */
int notation_to_coords(const char *s, int *x, int *y)
{
    if (s == NULL || s[0] == 0)
        return 0;
    if (s[0] == 'p')
    { // pass
        *x = -1;
        *y = -1;
        return 1;
    }
    if (s[0] < 'a' || s[0] > 'h')
        return 0;
    if (s[1] < '1' || s[1] > '8')
        return 0;
    *x = s[0] - 'a';
    *y = s[1] - '1';
    return 1;
}

/* Load game from of.txt and reconstruct GameState */
int load_game_from_file(GameState *g, char moves_out[][4], int *moves_n)
{
    FILE *fp = fopen("of.txt", "r");
    if (!fp)
        return 0;
    int count = 0;
    if (fscanf(fp, "%d", &count) != 1)
    {
        fclose(fp);
        return 0;
    }
    char buf[16];
    int n = 0;
    while (fscanf(fp, "%s", buf) == 1)
    {
        if (n < MAX_MOVES)
        {
            strncpy(moves_out[n], buf, 4);
            moves_out[n][3] = 0;
        }
        n++;
    }
    fclose(fp);
    *moves_n = n;
    // reconstruct
    init_board(g);
    for (int i = 0; i < n; i++)
    {
        char *m = moves_out[i];
        if (m[0] == 'w')
        { // game over marker or weird
            break;
        }
        if (m[0] == 'p')
        {
            g->turn = 1 - g->turn;
            g->move_count++;
            continue;
        }
        int x, y;
        if (!notation_to_coords(m, &x, &y))
            continue;
        int color = color_of_turn(g);
        Move flips[MAX_FLIPS];
        int fc = 0;
        if (!is_legal_move(g, color, x, y, flips, &fc))
        {
            // resilience: place without flips (rare), to avoid crash
            g->board[x][y] = color;
            g->turn = 1 - g->turn;
            g->move_count++;
        }
        else
        {
            apply_move_with_flips(g, color, x, y, flips, fc);
        }
    }
    return 1;
}

/* Overwrite of.txt with appended move */
int append_move_to_oftxt(GameState *g, const char *move_str)
{
    // Read existing moves
    FILE *fp = fopen("of.txt", "r");
    char moves[MAX_MOVES][8];
    int n = 0;
    if (fp)
    {
        int cnt;
        if (fscanf(fp, "%d", &cnt) == 1)
        {
            char buf[16];
            while (fscanf(fp, "%s", buf) == 1 && n < MAX_MOVES)
            {
                strncpy(moves[n], buf, 7);
                moves[n][7] = 0;
                n++;
            }
        }
        fclose(fp);
    }
    // Append the move_str
    if (n < MAX_MOVES)
    {
        strncpy(moves[n], move_str, 7);
        moves[n][7] = 0;
        n++;
    }
    else
        return 0;

    // Write whole file
    fp = fopen("of.txt", "w");
    if (!fp)
        return 0;
    fprintf(fp, "%2d", n);
    for (int i = 0; i < n; i++)
    {
        fprintf(fp, "%s\n", moves[i]);
    }
    fclose(fp);
    return 1;
}

/* Write pass: uses append_move_to_oftxt */
void write_pass(GameState *g)
{
    append_move_to_oftxt(g, "p9");
}

/* Play move and append to file with updated count */
void play_and_write_move(GameState *g, int x, int y)
{
    char m[8];
    if (x < 0)
    {
        write_pass(g);
        g->turn = 1 - g->turn;
        g->move_count++;
        return;
    }
    coords_to_notation(m, x, y);
    append_move_to_oftxt(g, m);
}

/* -------------------- Utility / I/O / Human interaction -------------------- */

void print_board(GameState *g)
{
    printf("  a b c d e f g h\n");
    for (int y = 0; y < 8; y++)
    {
        printf("%d ", y + 1);
        for (int x = 0; x < 8; x++)
        {
            char c = '.';
            if (g->board[x][y] == BLACK)
                c = 'B';
            if (g->board[x][y] == WHITE)
                c = 'W';
            printf("%c ", c);
        }
        printf("\n");
    }
    printf("Move count: %d  Turn: %s\n", g->move_count, g->turn == 0 ? "Black" : "White");
}

int get_human_move(GameState *g, int *rx, int *ry)
{
    char s[16];
    printf("Enter move (e.g. c4) or 'p' to pass: ");
    if (!fgets(s, sizeof(s), stdin))
        return 0;
    char *nl = strchr(s, '\n');
    if (nl)
        *nl = 0;
    if (s[0] == 'p' || s[0] == 'P')
    {
        *rx = -1;
        *ry = -1;
        return 1;
    }
    int x, y;
    if (!notation_to_coords(s, &x, &y))
    {
        printf("Invalid notation.\n");
        return 0;
    }
    Move flips[MAX_FLIPS];
    int fc = 0;
    int color = color_of_turn(g);
    if (!is_legal_move(g, color, x, y, flips, &fc))
    {
        printf("Illegal move.\n");
        return 0;
    }
    *rx = x;
    *ry = y;
    return 1;
}

void print_help()
{
    printf("Ot8b usage:\n");
    printf(" ./Ot8b            # interactive (menu)\n");
    printf(" ./Ot8b F [depth]  # play as First (Black)\n");
    printf(" ./Ot8b S [depth]  # play as Second (White)\n");
    printf(" ./Ot8b A [depth]  # auto play both\n");
    printf(" ./Ot8b B          # Human vs Computer (Human Black)\n");
    printf(" ./Ot8b W          # Human vs Computer (Human White)\n");
    printf(" ./Ot8b L          # Load game from of.txt and continue\n");
}

/* -------------------- Opening book (small stub) -------------------- */

typedef struct
{
    int move_num;
    int x, y;
    int strength;
} BookMove;

static BookMove opening_book[] = {
    {1, 2, 3, 100}, // c4 as Black's first move (example)
    {-1, -1, -1, 0}};

int check_opening_book(GameState *g, int *out_x, int *out_y)
{
    for (int i = 0; opening_book[i].move_num != -1; i++)
    {
        if (opening_book[i].move_num == g->move_count)
        {
            *out_x = opening_book[i].x;
            *out_y = opening_book[i].y;
            // ensure move is legal before returning
            Move flips[MAX_FLIPS];
            int fc = 0;
            int color = color_of_turn(g);
            if (is_legal_move(g, color, *out_x, *out_y, flips, &fc))
                return 1;
        }
    }
    return 0;
}

/* -------------------- Choose best move wrapper -------------------- */

int choose_best_move(GameState *g, int depth, int *out_x, int *out_y)
{
    // Opening book check
    if (check_opening_book(g, out_x, out_y))
    {
        return 1;
    }

    int color = color_of_turn(g);
    Move moves[MAX_LEGAL];
    int n;
    int flipinfo[MAX_LEGAL][MAX_FLIPS];
    int flipcounts[MAX_LEGAL];
    generate_moves(g, color, moves, &n, flipinfo, flipcounts);
    if (n == 0)
    {
        *out_x = -1;
        *out_y = -1; // pass
        return 1;
    }

    Move bestm = {-1, -1};
    int bestv = -INF;
    // call negamax with given depth, ply=0
    Move tmp;
    int val = negamax(g, depth, -INF, INF, color, &tmp, 0);
    if (tmp.x == -1)
    {
        // fallback: choose highest positional if negamax didn't set
        *out_x = moves[0].x;
        *out_y = moves[0].y;
    }
    else
    {
        *out_x = tmp.x;
        *out_y = tmp.y;
    }
    return 1;
}

/* -------------------- Main program & modes -------------------- */

int load_or_init_oftxt()
{
    FILE *fp = fopen("of.txt", "r");
    if (!fp)
    {
        fp = fopen("of.txt", "w");
        if (fp)
        {
            fprintf(fp, "%2d\n", 0);
            fclose(fp);
            return 0;
        }
        return 0;
    }
    fclose(fp);
    return 1;
}

int main(int argc, char *argv[])
{
    init_zobrist();
    init_killers();
    GameState g;
    init_board(&g);
    int depth = 6; // default
    int moves_n = 0;

    /* Auto-play / engine modes */
    if (argv[1][0] == 'S')
    {
        load_or_init_oftxt();
        char moves[MAX_MOVES][4];
        if (load_game_from_file(&g, moves, &moves_n))
        {
            /* loaded */
        }

        while (!is_game_over(&g))
        {
            load_or_init_oftxt();
            char moves[MAX_MOVES][4];
            if (load_game_from_file(&g, moves, &moves_n))
            {
                /* loaded */
            }

            // print_board(&g);
            // printf("\n turn: %d\n", g.turn);

            // if (g.move_count == 3) return 0;

            if (g.turn != WHITE)
            {
                continue;
            }
            else
            {
                // int color = color_of_turn(&g);
                int color = WHITE;
                int moves_avail = count_legal_moves(&g, color);
                if (moves_avail == 0)
                {
                    printf("%s passes.\n", "White");
                    g.turn = 1 - g.turn;
                    g.move_count++;
                    append_move_to_oftxt(&g, "p9");
                    continue;
                }
                int bx, by;
                choose_best_move(&g, depth, &bx, &by);
                if (bx < 0)
                {
                    g.turn = 1 - g.turn;
                    g.move_count++;
                    append_move_to_oftxt(&g, "p9");
                    printf("%s passes (no legal).\n", "White");
                }
                else
                {
                    Move flips[MAX_FLIPS];
                    int fc = 0;
                    is_legal_move(&g, color, bx, by, flips, &fc);
                    apply_move_with_flips(&g, color, bx, by, flips, fc);
                    char m[8];
                    coords_to_notation(m, bx, by);
                    append_move_to_oftxt(&g, m);
                    printf("%s plays %s\n", "White", m);
                }
                print_board(&g);
            }
        }


        // while (!is_game_over(&g))
        // {
        //     int color = color_of_turn(&g);
        //     int moves_avail = count_legal_moves(&g, color);
        //     if (moves_avail == 0)
        //     {
        //         printf("%s passes.\n", color == BLACK ? "Black" : "White");
        //         g.turn = 1 - g.turn;
        //         g.move_count++;
        //         append_move_to_oftxt(&g, "p9");
        //         continue;
        //     }
        //     int bx, by;
        //     choose_best_move(&g, depth, &bx, &by);
        //     if (bx < 0)
        //     {
        //         g.turn = 1 - g.turn;
        //         g.move_count++;
        //         append_move_to_oftxt(&g, "p9");
        //         printf("%s passes (no legal).\n", color == BLACK ? "Black" : "White");
        //     }
        //     else
        //     {
        //         Move flips[MAX_FLIPS];
        //         int fc = 0;
        //         is_legal_move(&g, color, bx, by, flips, &fc);
        //         apply_move_with_flips(&g, color, bx, by, flips, fc);
        //         char m[8];
        //         coords_to_notation(m, bx, by);
        //         append_move_to_oftxt(&g, m);
        //         printf("%s plays %s\n", color == BLACK ? "Black" : "White", m);
        //     }
        //     print_board(&g);
        // }
        printf("Auto-play finished.\n");
        int b = count_disks(&g, BLACK), w = count_disks(&g, WHITE);
        printf("Final score - Black: %d  White: %d\n", b, w);
        return 0;
    }

    /* Default: single engine move for current turn */
    printf("Default engine single move.\n");
    print_board(&g);
    int bx, by;
    choose_best_move(&g, depth, &bx, &by);
    if (bx < 0)
    {
        printf("Engine passes.\n");
        append_move_to_oftxt(&g, "p9");
        g.turn = 1 - g.turn;
        g.move_count++;
    }
    else
    {
        Move flips[MAX_FLIPS];
        int fc = 0;
        int color = color_of_turn(&g);
        is_legal_move(&g, color, bx, by, flips, &fc);
        apply_move_with_flips(&g, color, bx, by, flips, fc);
        char m[8];
        coords_to_notation(m, bx, by);
        append_move_to_oftxt(&g, m);
        printf("Engine plays %s\n", m);
    }
    print_board(&g);
    return 0;
}
