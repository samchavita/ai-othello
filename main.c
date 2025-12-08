#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <time.h>
#include <assert.h>
#include <limits.h>

// --- CONSTANTS ---
#define Board_Size 8
#define TRUE 1
#define FALSE 0
#define MAX_MOVES 200

// Transposition Table Size (Must be power of 2)
#define TT_SIZE 1048576 // ~1 million entries

// Bitboard Macros
#define SET_BIT(bb, i) ((bb) |= (1ULL << (i)))
#define GET_BIT(bb, i) ((bb) & (1ULL << (i)))
#define CLEAR_BIT(bb, i) ((bb) &= ~(1ULL << (i)))

typedef unsigned long long Bitboard;

// --- EXISTING I/O & GAME LOGIC PROTOTYPES (UNCHANGED) ---
void Delay(unsigned int mseconds);
char Load_File(void);
void Init();
int Play_a_Move(int x, int y);
void Show_Board_and_Set_Legal_Moves(void);
int Put_a_Stone(int x, int y);
int In_Board(int x, int y);
int Check_Cross(int x, int y, int update);
int Check_Straight_Army(int x, int y, int d, int update);
int Find_Legal_Moves(int color);
int Check_EndGame(void);
int Compute_Grades(int flag);

// --- NEW ENGINE PROTOTYPES ---
void Computer_Think(int *x, int *y);
int Solve_Endgame(Bitboard my_bb, Bitboard opp_bb, int alpha, int beta);
int PVS(Bitboard my_bb, Bitboard opp_bb, int depth, int alpha, int beta, int passed);

// --- GLOBAL VARIABLES (EXISTING) ---
int Search_Counter;
int Computer_Take;
int Winner;
int Now_Board[Board_Size][Board_Size];
int Legal_Moves[Board_Size][Board_Size];
int HandNumber;
int sequence[MAX_MOVES];
int Black_Count, White_Count;
int Turn = 0; // 0: Black, 1: White
int Stones[2] = {1, 2};
int DirX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
int DirY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
int LastX, LastY;
int Think_Time = 0, Total_Time = 0;
int search_deep = 8; // Default depth
int resultX, resultY;

// --- NEW ENGINE VARIABLES ---
Bitboard ZobristTable[2][64];
unsigned long long ZobristHash;

typedef struct
{
    unsigned long long key;
    int value;
    int depth;
    int flag;      // 0: Exact, 1: LowerBound, 2: UpperBound
    int best_move; // 0-63 index
} TTEntry;

TTEntry *TranspositionTable;

// Positional Weights (Approximating patterns)
int static_weights[64] = {
    100, -20, 10, 5, 5, 10, -20, 100,
    -20, -50, -2, -2, -2, -2, -50, -20,
    10, -2, -1, -1, -1, -1, -2, 10,
    5, -2, -1, -1, -1, -1, -2, 5,
    5, -2, -1, -1, -1, -1, -2, 5,
    10, -2, -1, -1, -1, -1, -2, 10,
    -20, -50, -2, -2, -2, -2, -50, -20,
    100, -20, 10, 5, 5, 10, -20, 100};

//---------------------------------------------------------------------------
// MAIN & I/O FUNCTIONS (DO NOT MODIFY THESE)
//---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    char compcolor = 'W', c[10];
    int column_input, row_input;
    int rx, ry, m = 0, n;
    FILE *fp;

    Init();

    if (argc == 3)
    {
        compcolor = *argv[1];
        if (atoi(argv[2]) > 0)
            search_deep = atoi(argv[2]);
        printf("%c, %d\n", compcolor, search_deep);
    }
    else if (argc == 2)
    {
        compcolor = *argv[1];
    }
    else
    {
        printf("Computer take?(B/W/All/File play as first/file play as Second/Load and play): ");
        scanf(" %c", &compcolor);
    }

    Show_Board_and_Set_Legal_Moves();

    if (compcolor == 'L' || compcolor == 'l')
        compcolor = Load_File();

    if (compcolor == 'B' || compcolor == 'b')
    {
        Computer_Think(&rx, &ry);
        printf("Computer played %c%d\n", rx + 97, ry + 1);
        Play_a_Move(rx, ry);
        Show_Board_and_Set_Legal_Moves();
    }

    if (compcolor == 'A' || compcolor == 'a')
        while (m++ < 64)
        {
            Computer_Think(&rx, &ry);
            if (!Play_a_Move(rx, ry))
            {
                printf("Wrong Computer moves %c%d\n", rx + 97, ry + 1);
                scanf("%d", &n);
                break;
            }
            if (rx == -1)
                printf("Computer Pass\n");
            else
                printf("Computer played %c%d\n", rx + 97, ry + 1);

            if (Check_EndGame())
                return 0;
            Show_Board_and_Set_Legal_Moves();
        }

    if (compcolor == 'F')
    {
        printf("First/Black start!\n");
        Computer_Think(&rx, &ry);
        Play_a_Move(rx, ry);
    }

    while (m++ < 64)
    {
        while (1)
        {
            if (compcolor == 'F' || compcolor == 'S')
            {
                fp = fopen("of.txt", "r");
                if (fp)
                {
                    fscanf(fp, "%d", &n);
                    char tc[10];
                    if (compcolor == 'F')
                    {
                        if (n % 2 == 0)
                        {
                            while ((fscanf(fp, "%s", tc)) != EOF)
                            {
                                c[0] = tc[0];
                                c[1] = tc[1];
                            }
                            fclose(fp);

                            if (c[0] == 'w')
                                return 0;
                            if (c[0] != 'p' && Now_Board[c[0] - 97][c[1] - 49] != 0)
                            {
                                printf("%s is wrong F\n", c);
                                continue;
                            }
                        }
                        else
                        {
                            fclose(fp);
                            Delay(100);
                            continue;
                        }
                    }
                    else
                    {
                        if (n % 2 == 1)
                        {
                            while ((fscanf(fp, "%s", tc)) != EOF)
                            {
                                c[0] = tc[0];
                                c[1] = tc[1];
                            }
                            fclose(fp);
                            if (c[0] == 'w')
                                return 0;
                            if (c[0] != 'p' && Now_Board[c[0] - 97][c[1] - 49] != 0)
                            {
                                printf("%s is wrong S\n", c);
                                continue;
                            }
                        }
                        else
                        {
                            fclose(fp);
                            Delay(100);
                            continue;
                        }
                    }
                }
                else
                {
                    Delay(100);
                    continue;
                }
            }

            if (compcolor == 'B')
            {
                printf("input White move:(a-h 1-8), or PASS\n");
                scanf("%s", c);
            }
            else if (compcolor == 'W')
            {
                printf("input Black move:(a-h 1-8), or PASS\n");
                scanf("%s", c);
            }

            if (c[0] == 'P' || c[0] == 'p')
                row_input = column_input = -1;
            else if (c[0] == 'M' || c[0] == 'm')
            {
                Computer_Think(&rx, &ry);
                if (!Play_a_Move(rx, ry))
                {
                    printf("Wrong Computer moves %c%d\n", rx + 97, ry + 1);
                    scanf("%d", &n);
                    break;
                }
                if (rx == -1)
                    printf("Computer Pass");
                else
                    printf("Computer played %c%d\n", rx + 97, ry + 1);
                if (Check_EndGame())
                    break;
                Show_Board_and_Set_Legal_Moves();
            }
            else
            {
                row_input = c[0] - 97;
                column_input = c[1] - 49;
            }

            if (!Play_a_Move(row_input, column_input))
            {
                printf("#%d, %c%d is a Wrong move\n", HandNumber, c[0], column_input + 1);
                return 0;
            }
            else
                break;
        }
        if (Check_EndGame())
            return 0;
        ;
        Show_Board_and_Set_Legal_Moves();

        Computer_Think(&rx, &ry);
        printf("Computer played %c%d\n", rx + 97, ry + 1);
        Play_a_Move(rx, ry);
        if (Check_EndGame())
            return 0;
        ;
        Show_Board_and_Set_Legal_Moves();
    }

    printf("Game is over!!");
    printf("\n%d", argc);
    if (argc <= 1)
        scanf("%d", &n);

    return 0;
}

//---------------------------------------------------------------------------
// IMPLEMENTATION HELPERS (UNCHANGED)
//---------------------------------------------------------------------------
void Delay(unsigned int mseconds)
{
    clock_t goal = mseconds + clock();
    while (goal > clock())
        ;
}

char Load_File(void)
{
    FILE *fp;
    char tc[10];
    int row_input, column_input, n;

    fp = fopen("of.txt", "r");
    if (!fp)
        return 'W';

    fscanf(fp, "%d", &n);

    while ((fscanf(fp, "%s", tc)) != EOF)
    {
        row_input = tc[0] - 97;
        column_input = tc[1] - 49;
        if (!Play_a_Move(row_input, column_input))
            printf("%c%d is a Wrong move\n", tc[0], column_input + 1);

        Show_Board_and_Set_Legal_Moves();
    }
    fclose(fp);
    return (n % 2 == 1) ? 'B' : 'W';
}

void Init()
{
    Total_Time = clock();
    Computer_Take = 0;
    memset(Now_Board, 0, sizeof(int) * Board_Size * Board_Size);

    srand(time(NULL));
    Now_Board[3][3] = Now_Board[4][4] = 2; // White
    Now_Board[3][4] = Now_Board[4][3] = 1; // Black

    HandNumber = 0;
    memset(sequence, -1, sizeof(int) * MAX_MOVES);
    Turn = 0;

    LastX = LastY = -1;
    Black_Count = White_Count = 0;
    Search_Counter = 0;
    Winner = 0;

    // --- NEW: Initialize Zobrist & TT ---
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 64; j++)
            ZobristTable[i][j] = ((unsigned long long)rand() << 32) | rand();

    TranspositionTable = (TTEntry *)calloc(TT_SIZE, sizeof(TTEntry));
}

int Play_a_Move(int x, int y)
{
    FILE *fp;

    if (x == -1 && y == -1) // PASS
    {
        fp = fopen("of.txt", "r+");
        if (fp)
        {
            fprintf(fp, "%2d\n", HandNumber + 1);
            fclose(fp);
        }
        fp = fopen("of.txt", "a");
        if (fp)
        {
            fprintf(fp, "p9\n");
            fclose(fp);
        }

        if (HandNumber < MAX_MOVES)
            sequence[HandNumber] = -1;
        HandNumber++;
        Turn = 1 - Turn;
        return 1;
    }

    if (!In_Board(x, y))
        return 0;
    Find_Legal_Moves(Stones[Turn]);
    if (Legal_Moves[x][y] == FALSE)
        return 0;

    if (Put_a_Stone(x, y))
    {
        Check_Cross(x, y, 1);
        Compute_Grades(TRUE);
        return 1;
    }
    else
        return 0;
}

int Put_a_Stone(int x, int y)
{
    FILE *fp;
    if (Now_Board[x][y] == 0)
    {
        if (HandNumber < MAX_MOVES)
            sequence[HandNumber] = Turn;
        if (HandNumber == 0)
            fp = fopen("of.txt", "w");
        else
            fp = fopen("of.txt", "r+");
        if (fp)
        {
            fprintf(fp, "%2d\n", HandNumber + 1);
            fclose(fp);
        }

        HandNumber++;
        Now_Board[x][y] = Stones[Turn];
        fp = fopen("of.txt", "a");
        if (fp)
        {
            fprintf(fp, "%c%d\n", x + 97, y + 1);
            fclose(fp);
        }

        LastX = x;
        LastY = y;
        Turn = 1 - Turn;
        return TRUE;
    }
    return FALSE;
}

void Show_Board_and_Set_Legal_Moves(void)
{
    int i, j;
    Find_Legal_Moves(Stones[Turn]);
    printf("a b c d e f g h\n");
    for (i = 0; i < Board_Size; i++)
    {
        for (j = 0; j < Board_Size; j++)
        {
            if (Now_Board[j][i] > 0)
                printf(Now_Board[j][i] == 2 ? "O " : "X ");
            else if (Now_Board[j][i] == 0)
                printf(Legal_Moves[j][i] == 1 ? "? " : ". ");
        }
        printf(" %d\n", i + 1);
    }
    printf("\n");
}

int Find_Legal_Moves(int color)
{
    int i, j, legal_count = 0;
    int me = color;
    memset(Legal_Moves, 0, sizeof(Legal_Moves));

    for (i = 0; i < Board_Size; i++)
        for (j = 0; j < Board_Size; j++)
            if (Now_Board[i][j] == 0)
            {
                Now_Board[i][j] = me;
                if (Check_Cross(i, j, FALSE) == TRUE)
                {
                    Legal_Moves[i][j] = TRUE;
                    legal_count++;
                }
                Now_Board[i][j] = 0;
            }
    return legal_count;
}

int Check_Cross(int x, int y, int update)
{
    int k, dx, dy, army, army_count = 0;
    if (!In_Board(x, y) || Now_Board[x][y] == 0)
        return FALSE;
    army = 3 - Now_Board[x][y];

    for (k = 0; k < 8; k++)
    {
        dx = x + DirX[k];
        dy = y + DirY[k];
        if (In_Board(dx, dy) && Now_Board[dx][dy] == army)
            army_count += Check_Straight_Army(x, y, k, update);
    }
    return (army_count > 0);
}

int Check_Straight_Army(int x, int y, int d, int update)
{
    int me = Now_Board[x][y], army = 3 - me;
    int army_count = 0, found_flag = FALSE;
    int flag[Board_Size][Board_Size] = {{0}};
    int i, j, tx = x, ty = y;

    for (i = 0; i < Board_Size; i++)
    {
        tx += DirX[d];
        ty += DirY[d];
        if (In_Board(tx, ty))
        {
            if (Now_Board[tx][ty] == army)
            {
                army_count++;
                flag[tx][ty] = TRUE;
            }
            else if (Now_Board[tx][ty] == me)
            {
                found_flag = TRUE;
                break;
            }
            else
                break;
        }
        else
            break;
    }

    if (found_flag && army_count > 0 && update)
        for (i = 0; i < Board_Size; i++)
            for (j = 0; j < Board_Size; j++)
                if (flag[i][j])
                    Now_Board[i][j] = me;

    return (found_flag && army_count > 0) ? army_count : 0;
}

int In_Board(int x, int y) { return (x >= 0 && x < Board_Size && y >= 0 && y < Board_Size); }

int Compute_Grades(int flag)
{
    int i, j, B = 0, W = 0;
    for (i = 0; i < Board_Size; i++)
        for (j = 0; j < Board_Size; j++)
            if (Now_Board[i][j] == 1)
                B++;
            else if (Now_Board[i][j] == 2)
                W++;

    if (flag)
    {
        Black_Count = B;
        White_Count = W;
        printf("#%d Grade: Black %d, White %d\n", HandNumber, B, W);
    }
    return (B - W);
}

int Check_EndGame(void)
{
    int lc1, lc2;
    FILE *fp;
    lc2 = Find_Legal_Moves(Stones[1 - Turn]);
    lc1 = Find_Legal_Moves(Stones[Turn]);

    if (lc1 == 0 && lc2 == 0)
    {
        fp = fopen("of.txt", "a");
        Total_Time = clock() - Total_Time;
        if (fp)
        {
            fprintf(fp, "Total used time= %d min. %d sec.\n", Total_Time / 60000, (Total_Time % 60000) / 1000);
            if (Black_Count > White_Count)
            {
                printf("Black Win!\n");
                fprintf(fp, "wB%d\n", Black_Count - White_Count);
            }
            else if (Black_Count < White_Count)
            {
                printf("White Win!\n");
                fprintf(fp, "wW%d\n", White_Count - Black_Count);
            }
            else
            {
                printf("Draw\n");
                fprintf(fp, "wZ%d\n", White_Count - Black_Count);
            }
            fclose(fp);
        }
        Show_Board_and_Set_Legal_Moves();
        printf("Game is over");
        return TRUE;
    }
    return FALSE;
}

//---------------------------------------------------------------------------
// 🚀 HIGH PERFORMANCE ENGINE CORE 🚀
//---------------------------------------------------------------------------

// --- Bitboard Helpers ---

int CountBits(Bitboard b)
{
// Brian Kernighan's Algorithm or Builtin
#ifdef __GNUC__
    return __builtin_popcountll(b);
#else
    int count = 0;
    while (b)
    {
        b &= (b - 1);
        count++;
    }
    return count;
#endif
}

// Generates moves using parallel bit shifts (Cogswell's)
Bitboard GetMoves(Bitboard my, Bitboard opp)
{
    Bitboard mask = opp & 0x7E7E7E7E7E7E7E7EULL; // Horizontal mask
    Bitboard moves = 0;
    // 8 Directions
    Bitboard t;
    // Right
    t = mask & (my >> 1);
    t |= mask & (t >> 1);
    t |= mask & (t >> 1);
    t |= mask & (t >> 1);
    t |= mask & (t >> 1);
    t |= mask & (t >> 1);
    moves |= (t >> 1);
    // Left
    t = mask & (my << 1);
    t |= mask & (t << 1);
    t |= mask & (t << 1);
    t |= mask & (t << 1);
    t |= mask & (t << 1);
    t |= mask & (t << 1);
    moves |= (t << 1);

    mask = opp & 0x00FFFFFFFFFFFF00ULL; // Vertical mask
    // Up
    t = mask & (my << 8);
    t |= mask & (t << 8);
    t |= mask & (t << 8);
    t |= mask & (t << 8);
    t |= mask & (t << 8);
    t |= mask & (t << 8);
    moves |= (t << 8);
    // Down
    t = mask & (my >> 8);
    t |= mask & (t >> 8);
    t |= mask & (t >> 8);
    t |= mask & (t >> 8);
    t |= mask & (t >> 8);
    t |= mask & (t >> 8);
    moves |= (t >> 8);

    mask = opp & 0x007E7E7E7E7E7E00ULL; // Diagonal mask
    // Up-Right
    t = mask & (my >> 7);
    t |= mask & (t >> 7);
    t |= mask & (t >> 7);
    t |= mask & (t >> 7);
    t |= mask & (t >> 7);
    t |= mask & (t >> 7);
    moves |= (t >> 7);
    // Up-Left
    t = mask & (my << 9);
    t |= mask & (t << 9);
    t |= mask & (t << 9);
    t |= mask & (t << 9);
    t |= mask & (t << 9);
    t |= mask & (t << 9);
    moves |= (t << 9);
    // Down-Right
    t = mask & (my >> 9);
    t |= mask & (t >> 9);
    t |= mask & (t >> 9);
    t |= mask & (t >> 9);
    t |= mask & (t >> 9);
    t |= mask & (t >> 9);
    moves |= (t >> 9);
    // Down-Left
    t = mask & (my << 7);
    t |= mask & (t << 7);
    t |= mask & (t << 7);
    t |= mask & (t << 7);
    t |= mask & (t << 7);
    t |= mask & (t << 7);
    moves |= (t << 7);

    return moves & ~(my | opp); // Moves must be on empty squares
}

// Executes a move on bitboards
void MakeMove(Bitboard *my, Bitboard *opp, int move)
{
    Bitboard new_disk = 1ULL << move;
    Bitboard captured = 0;
    Bitboard mask;

    // Check 8 directions for captures
    // This is the slower "execute" part, but needed for recursion
    // A full high-speed engine uses delta-swap tables, but loop is fine here.
    int dirs[8] = {-1, 1, -8, 8, -9, 9, -7, 7};

    for (int d = 0; d < 8; d++)
    {
        Bitboard flippable = 0;
        int cur = move + dirs[d];
        while (cur >= 0 && cur < 64)
        {
            // Check wrapping for horizontal moves
            if ((dirs[d] == 1 || dirs[d] == 9 || dirs[d] == -7) && (cur % 8 == 0))
                break;
            if ((dirs[d] == -1 || dirs[d] == -9 || dirs[d] == 7) && (cur % 8 == 7))
                break;

            if (GET_BIT(*opp, cur))
            {
                flippable |= (1ULL << cur);
            }
            else if (GET_BIT(*my, cur))
            {
                captured |= flippable;
                break;
            }
            else
            {
                break;
            }
            cur += dirs[d];
        }
    }

    *my |= new_disk | captured;
    *opp &= ~captured;
}

// --- Transposition Table Helpers ---
void TT_Store(unsigned long long key, int value, int depth, int flag, int best_move)
{
    int index = key & (TT_SIZE - 1);
    TranspositionTable[index].key = key;
    TranspositionTable[index].value = value;
    TranspositionTable[index].depth = depth;
    TranspositionTable[index].flag = flag;
    TranspositionTable[index].best_move = best_move;
}

int TT_Lookup(unsigned long long key, int depth, int alpha, int beta, int *move)
{
    int index = key & (TT_SIZE - 1);
    if (TranspositionTable[index].key == key)
    {
        *move = TranspositionTable[index].best_move;
        if (TranspositionTable[index].depth >= depth)
        {
            if (TranspositionTable[index].flag == 0)
                return TranspositionTable[index].value; // Exact
            if (TranspositionTable[index].flag == 1 && TranspositionTable[index].value >= beta)
                return beta; // LowerBound
            if (TranspositionTable[index].flag == 2 && TranspositionTable[index].value <= alpha)
                return alpha; // UpperBound
        }
    }
    return -999999; // Not found
}

// --- Evaluation ---
int Evaluate(Bitboard my, Bitboard opp, Bitboard my_moves, Bitboard opp_moves)
{
    int score = 0;

    // 1. Mobility
    int my_mob = CountBits(my_moves);
    int opp_mob = CountBits(opp_moves);
    score += (my_mob - opp_mob) * 10;

    // 2. Positional Weights (Pattern Approximation)
    // Very fast extraction using bit loop
    Bitboard temp = my;
    while (temp)
    {
#ifdef __GNUC__
        int idx = __builtin_ctzll(temp);
#else
        int idx = 0;
        Bitboard t = temp;
        while (!(t & 1))
        {
            t >>= 1;
            idx++;
        }
#endif
        score += static_weights[idx];
        CLEAR_BIT(temp, idx);
    }
    temp = opp;
    while (temp)
    {
#ifdef __GNUC__
        int idx = __builtin_ctzll(temp);
#else
        int idx = 0;
        Bitboard t = temp;
        while (!(t & 1))
        {
            t >>= 1;
            idx++;
        }
#endif
        score -= static_weights[idx];
        CLEAR_BIT(temp, idx);
    }

    return score;
}

// --- Search Algorithms ---

// EXACT ENDGAME SOLVER (Alpha-Beta, no depth limit)
int Solve_Endgame(Bitboard my_bb, Bitboard opp_bb, int alpha, int beta)
{
    Bitboard moves = GetMoves(my_bb, opp_bb);

    if (moves == 0)
    {
        if (GetMoves(opp_bb, my_bb) == 0)
        {
            // Game Over: Count discs
            int diff = CountBits(my_bb) - CountBits(opp_bb);
            if (diff > 0)
                return 10000 + diff;
            if (diff < 0)
                return -10000 + diff;
            return 0;
        }
        return -Solve_Endgame(opp_bb, my_bb, -beta, -alpha);
    }

    int best = -999999;

    while (moves)
    {
#ifdef __GNUC__
        int move = __builtin_ctzll(moves);
#else
        int move = 0;
        Bitboard t = moves;
        while (!(t & 1))
        {
            t >>= 1;
            move++;
        }
#endif

        Bitboard new_my = my_bb;
        Bitboard new_opp = opp_bb;
        MakeMove(&new_my, &new_opp, move);

        int val = -Solve_Endgame(new_opp, new_my, -beta, -alpha);

        if (val > best)
            best = val;
        if (val > alpha)
            alpha = val;
        if (alpha >= beta)
            break;

        CLEAR_BIT(moves, move);
    }
    return best;
}

// PVS (NegaScout)
int PVS(Bitboard my_bb, Bitboard opp_bb, int depth, int alpha, int beta, int passed)
{
    // 0. Endgame Switch
    int empties = 64 - CountBits(my_bb | opp_bb);
    if (empties <= 14)
        return Solve_Endgame(my_bb, opp_bb, alpha, beta);

    // 1. TT Lookup
    // Simple hash: XOR position of set bits (Optimization: Incremental hash is better but complex to add here)
    // We will regenerate hash for simplicity in this condensed code
    unsigned long long hash = 0;
    Bitboard t = my_bb;
    while (t)
    {
        int idx = __builtin_ctzll(t);
        hash ^= ZobristTable[0][idx];
        CLEAR_BIT(t, idx);
    }
    t = opp_bb;
    while (t)
    {
        int idx = __builtin_ctzll(t);
        hash ^= ZobristTable[1][idx];
        CLEAR_BIT(t, idx);
    }

    int tt_move = -1;
    int val = TT_Lookup(hash, depth, alpha, beta, &tt_move);
    if (val != -999999)
        return val;

    // 2. Base Case
    if (depth == 0)
    {
        return Evaluate(my_bb, opp_bb, GetMoves(my_bb, opp_bb), GetMoves(opp_bb, my_bb));
    }

    // 3. Move Gen
    Bitboard moves = GetMoves(my_bb, opp_bb);
    if (moves == 0)
    {
        if (passed)
        { // Double pass = Game Over
            int diff = CountBits(my_bb) - CountBits(opp_bb);
            if (diff > 0)
                return 10000 + diff;
            if (diff < 0)
                return -10000 + diff;
            return 0;
        }
        return -PVS(opp_bb, my_bb, depth, -beta, -alpha, 1);
    }

    // 4. Move Sorting (Use array)
    int move_list[32];
    int count = 0;
    t = moves;
    while (t)
    {
        int m = __builtin_ctzll(t);
        move_list[count++] = m;
        CLEAR_BIT(t, m);
    }

    // Simple sort: Put TT move first, then corner moves
    for (int i = 0; i < count; i++)
    {
        int score = 0;
        if (move_list[i] == tt_move)
            score = 10000;
        else
            score = static_weights[move_list[i]];

        for (int j = i + 1; j < count; j++)
        {
            int score2 = static_weights[move_list[j]];
            if (move_list[j] == tt_move)
                score2 = 10000;

            if (score2 > score)
            {
                int temp = move_list[i];
                move_list[i] = move_list[j];
                move_list[j] = temp;
                score = score2;
            }
        }
    }

    // 5. PVS Loop
    int best_score = -999999;
    int best_move_local = -1;
    int a = alpha;
    int b = beta;

    for (int i = 0; i < count; i++)
    {
        Bitboard new_my = my_bb;
        Bitboard new_opp = opp_bb;
        MakeMove(&new_my, &new_opp, move_list[i]);

        int score;
        if (i == 0)
        {
            score = -PVS(new_opp, new_my, depth - 1, -b, -a, 0);
        }
        else
        {
            // Null window search
            score = -PVS(new_opp, new_my, depth - 1, -a - 1, -a, 0);
            if (a < score && score < b)
            { // Fail high on null window? Re-search full
                score = -PVS(new_opp, new_my, depth - 1, -b, -score, 0);
            }
        }

        if (score > best_score)
        {
            best_score = score;
            best_move_local = move_list[i];
        }
        if (score > a)
            a = score;
        if (a >= b)
            break;
    }

    // 6. TT Store
    int flag = 0;
    if (best_score <= alpha)
        flag = 2; // Upper Bound
    else if (best_score >= beta)
        flag = 1; // Lower Bound
    TT_Store(hash, best_score, depth, flag, best_move_local);

    return best_score;
}

void Computer_Think(int *x, int *y)
{
    time_t clockBegin = clock();
    resultX = -1;
    resultY = -1;
    Search_Counter = 0;

    // Convert Now_Board to Bitboards
    Bitboard my_bb = 0, opp_bb = 0;
    int my_color = Stones[Turn]; // 1 or 2

    for (int i = 0; i < 8; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            if (Now_Board[i][j] == my_color)
                SET_BIT(my_bb, (j * 8 + i)); // Note: Board is [x][y] (col, row)?
            // Wait, standard Othello 1D mapping is usually Row*8 + Col.
            // In this code, input is a-h (x, 0-7), 1-8 (y, 0-7).
            // Code uses Now_Board[x][y].
            // Standard Othello bitboard convention: A1 is usually bit 0 or 63.
            // Let's use: Index = y * 8 + x (Row Major)
            else if (Now_Board[i][j] != 0)
                SET_BIT(opp_bb, (j * 8 + i));
        }
    }

    // Iterative Deepening
    int best_move_overall = -1;

    // Check if only 1 move available (Optimization)
    Bitboard valid = GetMoves(my_bb, opp_bb);
    if (valid == 0)
    {
        *x = -1;
        *y = -1;
        return;
    }
    if (CountBits(valid) == 1)
    {
        int m = __builtin_ctzll(valid);
        *x = m % 8;
        *y = m / 8;
        return;
    }

    // Run ID
    for (int d = 1; d <= search_deep; d++)
    {
        // Clear history? No, PVS benefits from history.
        PVS(my_bb, opp_bb, d, -999999, 999999, 0);

        // Retrieve best move from TT for root
        unsigned long long hash = 0;
        Bitboard t = my_bb;
        while (t)
        {
            int idx = __builtin_ctzll(t);
            hash ^= ZobristTable[0][idx];
            CLEAR_BIT(t, idx);
        }
        t = opp_bb;
        while (t)
        {
            int idx = __builtin_ctzll(t);
            hash ^= ZobristTable[1][idx];
            CLEAR_BIT(t, idx);
        }

        int idx = hash & (TT_SIZE - 1);
        if (TranspositionTable[idx].key == hash)
        {
            best_move_overall = TranspositionTable[idx].best_move;
        }

        // Time Check (Simulated: if > 3 seconds, stop early)
        if ((clock() - clockBegin) > 3 * CLOCKS_PER_SEC)
            break;
    }

    // Decode Move
    if (best_move_overall != -1)
    {
        *x = best_move_overall % 8;
        *y = best_move_overall / 8;
    }
    else
    {
        // Fallback if TT fails (shouldn't happen with valid moves)
        int m = __builtin_ctzll(valid);
        *x = m % 8;
        *y = m / 8;
    }

    // Timing output (Requested in original code)
    Think_Time += (clock() - clockBegin);
    printf("used thinking time= %d min. %d.%d sec.\n", Think_Time / 60000, (Think_Time % 60000) / 1000, (Think_Time % 60000) % 1000);
}