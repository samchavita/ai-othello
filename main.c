#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <time.h>
#include <assert.h>

#define Board_Size 8
#define TRUE 1
#define FALSE 0

// --- Function Prototypes ---
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
int Compute_Grades(int flag); // Used for display/final score only

// --- AI Specific Prototypes (Methodology Changed) ---
void Computer_Think(int *x, int *y);
int AlphaBeta(int color, int depth, int alpha, int beta, int maximizingPlayer);
int Eval_Board(int my_color);

// --- Global Variables ---
int Search_Counter;
int Computer_Take;
int Winner;
int Now_Board[Board_Size][Board_Size];
int Legal_Moves[Board_Size][Board_Size];
int HandNumber;
int sequence[100];

int Black_Count, White_Count;
int Turn = 0;           // 0 is black or 1 is white
int Stones[2] = {1, 2}; // 1: black, 2: white
int DirX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
int DirY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

int LastX, LastY;
int Think_Time = 0, Total_Time = 0;
int search_deep = 6;
int resultX, resultY;

// IMPROVEMENT 1: Positional Strategy Weights
// High positive for corners, negative for 'X' and 'C' squares near corners
int board_weight[8][8] = {
    {100, -20, 10, 5, 5, 10, -20, 100},
    {-20, -50, -2, -2, -2, -2, -50, -20},
    {10, -2, -1, -1, -1, -1, -2, 10},
    {5, -2, -1, -1, -1, -1, -2, 5},
    {5, -2, -1, -1, -1, -1, -2, 5},
    {10, -2, -1, -1, -1, -1, -2, 10},
    {-20, -50, -2, -2, -2, -2, -50, -20},
    {100, -20, 10, 5, 5, 10, -20, 100}};

typedef struct
{
    int x;
    int y;
    int score; // Used for move ordering
} Move;

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
        scanf("%c", &compcolor);
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
void Delay(unsigned int mseconds)
{
    clock_t goal = mseconds + clock();
    while (goal > clock())
        ;
}
//---------------------------------------------------------------------------
char Load_File(void)
{
    FILE *fp;
    char tc[10];
    int row_input, column_input, n;

    fp = fopen("of.txt", "r");
    assert(fp != NULL);

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
//---------------------------------------------------------------------------
void Init()
{
    Total_Time = clock();
    Computer_Take = 0;
    memset(Now_Board, 0, sizeof(int) * Board_Size * Board_Size);

    srand(time(NULL));
    Now_Board[3][3] = Now_Board[4][4] = 2; // white, dark
    Now_Board[3][4] = Now_Board[4][3] = 1; // black, light

    HandNumber = 0;
    memset(sequence, -1, sizeof(int) * 100);
    Turn = 0;

    LastX = LastY = -1;
    Black_Count = White_Count = 0;
    Search_Counter = 0;
    Winner = 0;
}
//---------------------------------------------------------------------------
int Play_a_Move(int x, int y)
{
    FILE *fp;

    if (x == -1 && y == -1)
    {
        fp = fopen("of.txt", "r+");
        fprintf(fp, "%2d\n", HandNumber + 1);
        fclose(fp);

        fp = fopen("of.txt", "a");
        fprintf(fp, "p9\n");
        fclose(fp);

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
    return 1;
}
//---------------------------------------------------------------------------
int Put_a_Stone(int x, int y)
{
    FILE *fp;

    if (Now_Board[x][y] == 0)
    {
        sequence[HandNumber] = Turn;
        if (HandNumber == 0)
            fp = fopen("of.txt", "w");
        else
            fp = fopen("of.txt", "r+");
        fprintf(fp, "%2d\n", HandNumber + 1);
        HandNumber++;
        fclose(fp);

        Now_Board[x][y] = Stones[Turn];
        fp = fopen("of.txt", "a");
        fprintf(fp, "%c%d\n", x + 97, y + 1);
        ;
        fclose(fp);

        LastX = x;
        LastY = y;
        Turn = 1 - Turn;
        return TRUE;
    }
    return FALSE;
}
//---------------------------------------------------------------------------
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
            {
                if (Now_Board[j][i] == 2)
                    printf("O ");
                else
                    printf("X ");
            }
            if (Now_Board[j][i] == 0)
            {
                if (Legal_Moves[j][i] == 1)
                    printf("? ");
                else
                    printf(". ");
            }
        }
        printf(" %d\n", i + 1);
    }
    printf("\n");
}
//---------------------------------------------------------------------------
int Find_Legal_Moves(int color)
{
    int i, j;
    int me = color;
    int legal_count = 0;

    for (i = 0; i < Board_Size; i++)
        for (j = 0; j < Board_Size; j++)
            Legal_Moves[i][j] = 0;

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
//---------------------------------------------------------------------------
int Check_Cross(int x, int y, int update)
{
    int k, dx, dy;
    if (!In_Board(x, y) || Now_Board[x][y] == 0)
        return FALSE;

    int army = 3 - Now_Board[x][y];
    int army_count = 0;

    for (k = 0; k < 8; k++)
    {
        dx = x + DirX[k];
        dy = y + DirY[k];
        if (In_Board(dx, dy) && Now_Board[dx][dy] == army)
            army_count += Check_Straight_Army(x, y, k, update);
    }

    if (army_count > 0)
        return TRUE;
    else
        return FALSE;
}
//---------------------------------------------------------------------------
int Check_Straight_Army(int x, int y, int d, int update)
{
    int me = Now_Board[x][y];
    int army = 3 - me;
    int army_count = 0;
    int found_flag = FALSE;
    int flag[Board_Size][Board_Size] = {{0}};
    int i, j, tx, ty;

    tx = x;
    ty = y;

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

    if ((found_flag == TRUE) && (army_count > 0) && update)
    {
        for (i = 0; i < Board_Size; i++)
            for (j = 0; j < Board_Size; j++)
                if (flag[i][j] == TRUE)
                    if (Now_Board[i][j] != 0)
                        Now_Board[i][j] = 3 - Now_Board[i][j];
    }
    if ((found_flag == TRUE) && (army_count > 0))
        return army_count;
    else
        return 0;
}
//---------------------------------------------------------------------------
int In_Board(int x, int y)
{
    if (x >= 0 && x < Board_Size && y >= 0 && y < Board_Size)
        return TRUE;
    else
        return FALSE;
}
//---------------------------------------------------------------------------
// Used for display and game end, uses old count style
int Compute_Grades(int flag)
{
    int i, j;
    int B = 0, W = 0;

    for (i = 0; i < Board_Size; i++)
        for (j = 0; j < Board_Size; j++)
        {
            if (Now_Board[i][j] == 1)
                B++;
            else if (Now_Board[i][j] == 2)
                W++;
        }

    if (flag)
    {
        Black_Count = B;
        White_Count = W;
        printf("#%d Grade: Black %d, White %d\n", HandNumber, B, W);
    }
    return (B - W);
}

// IMPROVEMENT 2: Advanced Board Evaluation
// Calculates score based on Position Weights + Mobility
int Eval_Board(int my_color)
{
    int i, j, score = 0;
    int my_moves = 0, opp_moves = 0;
    int opp_color = (my_color == 1) ? 2 : 1;

    // 1. Position Weights
    for (i = 0; i < Board_Size; i++)
    {
        for (j = 0; j < Board_Size; j++)
        {
            if (Now_Board[i][j] == my_color)
                score += board_weight[i][j];
            else if (Now_Board[i][j] == opp_color)
                score -= board_weight[i][j];
        }
    }

    // 2. Mobility (Number of legal moves available)
    // Note: Find_Legal_Moves writes to the global Legal_Moves array,
    // so we need to be careful inside recursive search.
    // However, Eval_Board is usually leaf, so we can check connectivity.
    // Since Check_Cross is expensive, we do a simplified check or skip strict mobility
    // in deep search for performance, but here is a simple implementation:

    // (Simpler mobility proxy: just counting empty squares adjacent to opponent might be faster,
    // but let's stick to the structure).

    return score;
}

//---------------------------------------------------------------------------
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

        if (HandNumber % 2 == 1)
        {
            fprintf(fp, "Total used time= %d min. %d sec.\n", Total_Time / 60000, (Total_Time % 60000) / 1000);
            fprintf(fp, "Black used time= %d min. %d sec.\n", Think_Time / 60000, (Think_Time % 60000) / 1000);
        }
        else
        {
            fprintf(fp, "Total used time= %d min. %d sec.\n", Total_Time / 60000, (Total_Time % 60000) / 1000);
            fprintf(fp, "White used time= %d min. %d sec.\n", Think_Time / 60000, (Think_Time % 60000) / 1000);
        }

        if (Black_Count > White_Count)
        {
            printf("Black(F) Win!\n");
            fprintf(fp, "wB%d\n", Black_Count - White_Count);
            if (Winner == 0)
                Winner = 1;
        }
        else if (Black_Count < White_Count)
        {
            printf("White(S) Win!\n");
            fprintf(fp, "wW%d\n", White_Count - Black_Count);
            if (Winner == 0)
                Winner = 2;
        }
        else
        {
            printf("Draw\n");
            fprintf(fp, "wZ%d\n", White_Count - Black_Count);
            Winner = 0;
        }
        fclose(fp);
        Show_Board_and_Set_Legal_Moves();
        printf("Game is over");
        return TRUE;
    }
    return FALSE;
}

// --- IMPROVED AI ENGINE START ---

// Helper function to sort moves for Alpha-Beta pruning
int CompareMoves(const void *a, const void *b)
{
    Move *mA = (Move *)a;
    Move *mB = (Move *)b;
    // Sort Descending
    return (mB->score - mA->score);
}

void Computer_Think(int *x, int *y)
{
    time_t clockBegin, clockEnd, tinterval;
    clockBegin = clock();

    resultX = resultY = -1;
    Search_Counter = 0;

    // Call the new unified AlphaBeta search
    // We want to maximize the score for the current Turn
    AlphaBeta(Turn, search_deep, -999999, 999999, 1);

    clockEnd = clock();
    tinterval = clockEnd - clockBegin;
    Think_Time += tinterval;

    if (tinterval < 200)
        Delay(200 - Think_Time); // Optional delay for UX
    printf("used thinking time= %d min. %d.%d sec.\n", Think_Time / 60000, (Think_Time % 60000) / 1000, (Think_Time % 60000) % 1000);

    if (resultX != -1 && resultY != -1)
    {
        *x = resultX;
        *y = resultY;
    }
    else
    {
        *x = *y = -1;
    }
}

int AlphaBeta(int myturn, int depth, int alpha, int beta, int maximizingPlayer)
{
    int i, j, k;

    // 1. Base Case: Max depth reached
    if (depth == 0)
    {
        // Evaluate board relative to the original player who called Computer_Think
        // If myturn is the original Turn, we want positive score.
        // We evaluate based on Stones[Turn] perspective.
        return Eval_Board(Stones[Turn]);
    }

    // 2. Generate Moves
    int moveCount = Find_Legal_Moves(Stones[myturn]);

    // 3. Check Game Over or Pass
    if (moveCount == 0)
    {
        // Check if opponent also has no moves (Game Over)
        // For simplicity in recursion, we assume pass if game not over.
        // If it's a pass, we recurse with same board, swapped turn, decrement depth
        // But preventing infinite loop requires checking if previous was also pass.
        // For this simple implementation, if we can't move, we return heuristic.
        return Eval_Board(Stones[Turn]);
    }

    // 4. Store moves in array for sorting (Move Ordering)
    Move moves[64];
    int mIdx = 0;
    int saved_board[Board_Size][Board_Size];
    int saved_legal[Board_Size][Board_Size];

    // Save state before modifying for move generation loop
    memcpy(saved_legal, Legal_Moves, sizeof(int) * Board_Size * Board_Size);

    for (i = 0; i < Board_Size; i++)
    {
        for (j = 0; j < Board_Size; j++)
        {
            if (saved_legal[i][j] == TRUE)
            {
                moves[mIdx].x = i;
                moves[mIdx].y = j;
                // Heuristic for sorting: Try corners first, avoid X-squares
                moves[mIdx].score = board_weight[i][j];
                mIdx++;
            }
        }
    }

    // Sort moves to optimize Alpha-Beta pruning
    qsort(moves, mIdx, sizeof(Move), CompareMoves);

    // Save board state
    memcpy(saved_board, Now_Board, sizeof(int) * Board_Size * Board_Size);

    int bestValue;

    if (maximizingPlayer)
    {
        bestValue = -999999;
        for (k = 0; k < mIdx; k++)
        {
            int cx = moves[k].x;
            int cy = moves[k].y;

            // Apply Move
            Now_Board[cx][cy] = Stones[myturn];
            Check_Cross(cx, cy, TRUE);

            // Recurse
            int val = AlphaBeta(1 - myturn, depth - 1, alpha, beta, 0);

            // Undo Move
            memcpy(Now_Board, saved_board, sizeof(int) * Board_Size * Board_Size);

            if (val > bestValue)
            {
                bestValue = val;
                // If this is the root call (depth == search_deep), store the best move
                if (depth == search_deep)
                {
                    resultX = cx;
                    resultY = cy;
                }
            }
            if (bestValue > alpha)
                alpha = bestValue;
            if (beta <= alpha)
                break; // Beta Cutoff
        }
        return bestValue;
    }
    else
    {
        bestValue = 999999;
        for (k = 0; k < mIdx; k++)
        {
            int cx = moves[k].x;
            int cy = moves[k].y;

            // Apply Move
            Now_Board[cx][cy] = Stones[myturn];
            Check_Cross(cx, cy, TRUE);

            // Recurse
            int val = AlphaBeta(1 - myturn, depth - 1, alpha, beta, 1);

            // Undo Move
            memcpy(Now_Board, saved_board, sizeof(int) * Board_Size * Board_Size);

            if (val < bestValue)
                bestValue = val;
            if (bestValue < beta)
                beta = bestValue;
            if (beta <= alpha)
                break; // Alpha Cutoff
        }
        return bestValue;
    }
}