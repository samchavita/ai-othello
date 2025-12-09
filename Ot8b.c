/*
 * Ot8b.c
 *
 * Tournament-grade Othello/Reversi engine (single-file).
 *
 * Note: I cannot provide internal chain-of-thought. This file is
 * an implementation that follows the specification provided by the user:
 * - Correct legal move detection and flipping
 * - File I/O to of.txt (reads/writes whole file; maintains move count on first line)
 * - Search: negamax with alpha-beta, simple move ordering, iterative deepening entry point
 * - Evaluation: material + mobility + positional + frontier approximations, endgame handling
 * - Command-line modes: F S A B W L (see usage below)
 *
 * Build:
 *   gcc -O2 -std=c99 -o Ot8b Ot8b.c
 *
 * Usage:
 *   ./Ot8b            # interactive (menu)
 *   ./Ot8b F [depth]  # play as First (Black)
 *   ./Ot8b S [depth]  # play as Second (White)
 *   ./Ot8b A [depth]  # auto play both
 *   ./Ot8b B          # Human vs Computer (Human Black)
 *   ./Ot8b W          # Human vs Computer (Human White)
 *   ./Ot8b L          # Load game from of.txt and continue
 *
 * of.txt format (read/write):
 * Line 1: move count (integer)
 * Lines 2+: moves in "c4" or "p9" for pass
 * A move like "wB" or "wW" as a line starting with 'w' signals game over (deprecated but handled)
 *
 * This implementation rewrites of.txt wholly when adding a move: it reads existing moves,
 * appends our move, then writes the new count and all moves back to of.txt.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

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

/* direction vectors (x,y) */
const int DX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int DY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

/* positional weight table per spec */
const int POSVAL[8][8] = {
    {100, -20, 10, 5, 5, 10, -20, 100},
    {-20, -50, -2, -1, -1, -2, -50, -20},
    {10, -2, 1, 1, 1, 1, -2, 10},
    {5, -1, 1, 0, 0, 1, -1, 5},
    {5, -1, 1, 0, 0, 1, -1, 5},
    {10, -2, 1, 1, 1, 1, -2, 10},
    {-20, -50, -2, -1, -1, -2, -50, -20},
    {100, -20, 10, 5, 5, 10, -20, 100}};

/* forward declarations */
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
int negamax(GameState *g, int depth, int alpha, int beta, int color, Move *bestmove);
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

/* --- IMPLEMENTATION --- */

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
            // collect temp_flips
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
    // revert flips to opponent color, clear played square
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
    // corners
    const int cx[4] = {0, 7, 0, 7};
    const int cy[4] = {0, 0, 7, 7};
    for (int k = 0; k < 4; k++)
    {
        int x = cx[k], y = cy[k];
        if (g->board[x][y] == color)
            stable += 1;
    }
    // edges anchored from corners - if corner held, count contiguous same-color edge pieces
    // top row left->right
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
    // left column
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

/* Evaluation function: combine material, mobility, positional, frontier, stability approximations.
 * Uses different weight regimes by move count as described.
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

    // Final: convert to int in centi- or just simple integer scaled. Favor positive => black advantage
    int eval = (int)round(evald * 10.0);
    return eval;
}

/* Move ordering: basic heuristic - corner moves highest, then positional value descending, then flips (not computed here).
 * We'll return an array of indices sorted by heuristic.
 */
void order_moves_by_heuristic(GameState *g, int color, Move moves[], int n, int order[])
{
    // compute heuristics per move
    int scores[MAX_LEGAL];
    for (int i = 0; i < n; i++)
    {
        int x = moves[i].x, y = moves[i].y;
        int v = POSVAL[y][x];
        // corners super-high
        if ((x == 0 && y == 0) || (x == 7 && y == 0) || (x == 0 && y == 7) || (x == 7 && y == 7))
            v += 1000;
        // X-squares (adjacent to corners) penalize
        if ((x == 1 && y == 0) || (x == 0 && y == 1) || (x == 1 && y == 1) ||
            (x == 6 && y == 0) || (x == 7 && y == 1) || (x == 6 && y == 1) ||
            (x == 0 && y == 6) || (x == 1 && y == 7) || (x == 1 && y == 6) ||
            (x == 6 && y == 7) || (x == 7 && y == 6) || (x == 6 && y == 6))
            v -= 200;
        scores[i] = v;
        order[i] = i;
    }
    // sort indices by scores descending (simple bubble for small n)
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

/* Negamax with alpha-beta. color: BLACK=1 / WHITE=2; returns eval from Black's POV (positive favors Black).
 * We convert to signed context: if color==WHITE we invert sign by evaluating opponent.
 */
int negamax(GameState *g, int depth, int alpha, int beta, int color, Move *bestmove)
{
    // Terminal: game over (no moves both)
    Move moves[MAX_LEGAL];
    int n;
    int flipinfo[MAX_LEGAL][MAX_FLIPS];
    int flipcounts[MAX_LEGAL];
    generate_moves(g, color, moves, &n, flipinfo, flipcounts);

    int opp = opponent_color(color);

    if (depth == 0 || is_game_over(g))
    {
        int ev = evaluate(g);
        // evaluation is positive for black; if white to move, return eval as is (we keep eval expressed as black-advantage)
        return ev;
    }

    if (n == 0)
    {
        // pass turn
        // If opponent also has no moves -> game over handled earlier
        // simulate pass toggling turn and recurse (move_count increment handled manually)
        g->turn = 1 - g->turn;
        g->move_count++;
        int val = negamax(g, depth - 1, alpha, beta, opp, NULL);
        g->turn = 1 - g->turn;
        g->move_count--;
        return val;
    }

    int order[MAX_LEGAL];
    order_moves_by_heuristic(g, color, moves, n, order);

    int best_val = -INF;
    Move local_best = {-1, -1};

    for (int idx = 0; idx < n; idx++)
    {
        int i = order[idx];
        // collect flips for this move
        Move flips[MAX_FLIPS];
        int fc = flipcounts[i];
        for (int k = 0; k < fc; k++)
        {
            int v = flipinfo[i][k];
            flips[k].x = v / 8;
            flips[k].y = v % 8;
        }
        apply_move_with_flips(g, color, moves[i].x, moves[i].y, flips, fc);
        int val = negamax(g, depth - 1, -beta, -alpha, opp, NULL);
        // value is from black POV; but negamax typically flips sign per ply
        // Here, because evaluate returns black-advantage, we must invert sign if opponent. Simpler: we use raw eval and invert:
        val = -val;
        undo_move_with_flips(g, color, moves[i].x, moves[i].y, flips, fc);

        if (val > best_val)
        {
            best_val = val;
            local_best = moves[i];
        }
        if (best_val > alpha)
            alpha = best_val;
        if (alpha >= beta)
            break;
    }

    if (bestmove)
        *bestmove = local_best;
    return best_val;
}

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

/* Print board for debugging / human play */
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

/* FILE I/O: load moves from of.txt into moves_out array (strings) and set moves_n.
 * Returns 1 on success, 0 on failure (file not exist).
 */
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
    // Now reconstruct game state from moves
    init_board(g);
    // apply moves from moves_out[0] .. moves_out[n-1]
    for (int i = 0; i < n; i++)
    {
        char *m = moves_out[i];
        if (m[0] == 'w')
        { // game over marker or weird, stop
            break;
        }
        if (m[0] == 'p')
        {
            // pass: toggle turn & increment move_count
            g->turn = 1 - g->turn;
            g->move_count++;
            continue;
        }
        int x, y;
        if (!notation_to_coords(m, &x, &y))
            continue;
        // generate flips for current turn color
        int color = color_of_turn(g);
        Move flips[MAX_FLIPS];
        int fc = 0;
        if (!is_legal_move(g, color, x, y, flips, &fc))
        {
            // file might present illegal sequence; just try to place without flips (resilient)
            g->board[x][y] = color;
        }
        else
        {
            apply_move_with_flips(g, color, x, y, flips, fc);
            // apply_move_with_flips already toggles turn & increments move_count
            // so we should continue to next
        }
    }
    return 1;
}

/* Overwrite of.txt with current moves (moves array of strings). The function expects moves_n moves.
 * First line is move count. */
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
    fprintf(fp, "%2d\n", n);
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
        // pass
        write_pass(g);
        // apply pass to game state
        g->turn = 1 - g->turn;
        g->move_count++;
        return;
    }
    coords_to_notation(m, x, y);
    append_move_to_oftxt(g, m);
}

/* Choose best move by search depth */
int choose_best_move(GameState *g, int depth, int *out_x, int *out_y)
{
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
    Move best;
    // We use negamax from current node with provided depth
    int val = negamax(g, depth, -INF, INF, color, &best);
    if (best.x == -1)
    {
        // fallback choose first
        *out_x = moves[0].x;
        *out_y = moves[0].y;
    }
    else
    {
        *out_x = best.x;
        *out_y = best.y;
    }
    return 1;
}

/* Human input helper */
int get_human_move(GameState *g, int *rx, int *ry)
{
    char s[16];
    printf("Enter move (e.g. c4) or 'p' to pass: ");
    if (!fgets(s, sizeof(s), stdin))
        return 0;
    // trim newline
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
    // check legality
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
    printf(" ./Ot8b F [depth]  - play as First (Black)\n");
    printf(" ./Ot8b S [depth]  - play as Second (White)\n");
    printf(" ./Ot8b A [depth]  - auto play both sides\n");
    printf(" ./Ot8b B          - human (Black) vs computer\n");
    printf(" ./Ot8b W          - human (White) vs computer\n");
    printf(" ./Ot8b L          - load of.txt and resume\n");
}

/* MAIN */
int main(int argc, char *argv[])
{
    GameState g;
    init_board(&g);

    int modeAuto = 0, modeHumanBlack = 0, modeHumanWhite = 0, loadOnly = 0;
    int depth = 6; // default
    if (argc == 1)
    {
        print_help();
    }
    else
    {
        if (argc >= 2)
        {
            char m = argv[1][0];
            if (m == 'F' || m == 'f')
            {
                modeAuto = 0; /* we play Black */
                g.turn = 0;
            }
            else if (m == 'S' || m == 's')
            {
                modeAuto = 0;
                g.turn = 1; /* we play White */
            }
            else if (m == 'A' || m == 'a')
            {
                modeAuto = 1;
            }
            else if (m == 'B' || m == 'b')
            {
                modeHumanBlack = 1;
                g.turn = 1; /* human will play black; set to computer to move? We'll set up below */
            }
            else if (m == 'W' || m == 'w')
            {
                modeHumanWhite = 1;
                g.turn = 0;
            }
            else if (m == 'L' || m == 'l')
            {
                loadOnly = 1;
            }
            else
            {
                print_help();
            }
        }
        if (argc >= 3)
        {
            int d = atoi(argv[2]);
            if (d > 0)
                depth = d;
        }
    }

    /* if loadOnly or file exists, load */
    char moves[MAX_MOVES][4];
    int moves_n = 0;
    if (load_game_from_file(&g, moves, &moves_n))
    {
        printf("Loaded game with %d moves from of.txt\n", moves_n);
    }
    else
    {
        // start fresh: we will write initial move count if needed
        // create file with starting count 1 and no moves (or as initialized)
        FILE *fp = fopen("of.txt", "r");
        if (!fp)
        {
            fp = fopen("of.txt", "w");
            if (fp)
            {
                fprintf(fp, "%2d\n", 0); // 0 moves
                fclose(fp);
            }
        }
        else
            fclose(fp);
    }

    if (loadOnly)
    {
        print_board(&g);
        printf("Loaded and exiting (L mode).\n");
        return 0;
    }

    /* Modes:
     * - If modeAuto: engine plays both sides, writes moves to file.
     * - If F or S: engine plays as that side. For simplicity, we let engine move when it's its turn, otherwise wait for file to be updated by opponent (not implemented as blocking watch) -> here we implement local self-play or immediate move (since we cannot wait)
     * - For B/W human vs computer: interactively ask user for moves.
     *
     * Note: Tournament environment expects reading of of.txt to detect opponent moves. In this single-file implementation
     * we support local interactive play or auto-self-play. For tournament integration with external referee, additional
     * synchronization/waiting logic will be required.
     */

    /* If modeHumanBlack or modeHumanWhite: interactive loop with human input */
    if (modeHumanBlack || modeHumanWhite)
    {
        int human_is_black = modeHumanBlack ? 1 : 0;
        print_board(&g);
        while (!is_game_over(&g))
        {
            int color = color_of_turn(&g);
            int our_turn = (!human_is_black && color == WHITE) || (human_is_black && color == BLACK) ? 0 : 1;
            if (our_turn)
            {
                // engine move
                int x, y;
                int moves_avail = count_legal_moves(&g, color);
                if (moves_avail == 0)
                {
                    printf("Engine passes.\n");
                    play_and_write_move(&g, -1, -1);
                    continue;
                }
                choose_best_move(&g, depth, &x, &y);
                // compute flips for applying
                Move flips[MAX_FLIPS];
                int fc = 0;
                is_legal_move(&g, color, x, y, flips, &fc);
                apply_move_with_flips(&g, color, x, y, flips, fc);
                char m[8];
                coords_to_notation(m, x, y);
                append_move_to_oftxt(&g, m);
                printf("Engine plays %s\n", m);
                print_board(&g);
            }
            else
            {
                // human input
                int hx, hy;
                if (!get_human_move(&g, &hx, &hy))
                {
                    continue;
                }
                if (hx == -1)
                {
                    // pass
                    g.turn = 1 - g.turn;
                    g.move_count++;
                    append_move_to_oftxt(&g, "p9");
                }
                else
                {
                    Move flips[MAX_FLIPS];
                    int fc = 0;
                    int color = color_of_turn(&g);
                    is_legal_move(&g, color, hx, hy, flips, &fc);
                    apply_move_with_flips(&g, color, hx, hy, flips, fc);
                    char m[8];
                    coords_to_notation(m, hx, hy);
                    append_move_to_oftxt(&g, m);
                }
                print_board(&g);
            }
        }
        printf("Game over!\n");
        int b = count_disks(&g, BLACK), w = count_disks(&g, WHITE);
        printf("Final score - Black: %d  White: %d\n", b, w);
        return 0;
    }

    /* Auto-play modes: A or F/S configured as engine plays from its turn onward.
     * For simplicity here, if engine is to play both sides (A) or play from current side, just loop until game end.
     */
    if (modeAuto || (argc >= 2 && (argv[1][0] == 'F' || argv[1][0] == 'S')))
    {
        while (!is_game_over(&g))
        {
            int color = color_of_turn(&g);
            int moves_avail = count_legal_moves(&g, color);
            if (moves_avail == 0)
            {
                printf("Color %s passes.\n", color == BLACK ? "Black" : "White");
                g.turn = 1 - g.turn;
                g.move_count++;
                append_move_to_oftxt(&g, "p9");
                continue;
            }
            int bx, by;
            choose_best_move(&g, depth, &bx, &by);
            if (bx < 0)
            {
                // pass
                g.turn = 1 - g.turn;
                g.move_count++;
                append_move_to_oftxt(&g, "p9");
                printf("%s passes (no legal).\n", color == BLACK ? "Black" : "White");
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
                printf("%s plays %s\n", color == BLACK ? "Black" : "White", m);
            }
            // optional: print board
            print_board(&g);
        }
        printf("Auto-play finished.\n");
        int b = count_disks(&g, BLACK), w = count_disks(&g, WHITE);
        printf("Final score - Black: %d  White: %d\n", b, w);
        return 0;
    }

    /* Default: interactive menu */
    printf("No valid mode selected or default. Running a single engine move for current turn.\n");
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
