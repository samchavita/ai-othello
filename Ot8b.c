#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <time.h>
#include <assert.h>

#define Board_Size 8
#define TRUE 1
#define FALSE 0
#define INF 1000000000

void Delay(unsigned int mseconds);
// int Read_File( FILE *p, char *c );//open a file and get the next move, for play by file
char Load_File(void); // load a file and start a game

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

void Computer_Think(int *x, int *y);
int Search(int myturn, int mylevel);
int search_next(int x, int y, int myturn, int mylevel, int alpha, int beta);

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

int search_deep = 8; // slightly deeper default search for stronger play

int alpha_beta_option = TRUE;
int resultX, resultY;

// Improved positional weights for stronger play
int board_weight[8][8] =
    // a,  b,   c,   d,   e,   f,   g,   h
    {
        {120, -25, 20, 5, 5, 20, -25, 120},     // 1
        {-25, -45, -10, -5, -5, -10, -45, -25}, // 2
        {20, -10, 15, 3, 3, 15, -10, 20},       // 3
        {5, -5, 3, 1, 1, 3, -5, 5},             // 4
        {5, -5, 3, 1, 1, 3, -5, 5},             // 5
        {20, -10, 15, 3, 3, 15, -10, 20},       // 6
        {-25, -45, -10, -5, -5, -10, -45, -25}, // 7
        {120, -25, 20, 5, 5, 20, -25, 120}      // 8
};

// Transposition table & Zobrist hashing for caching positions
typedef struct
{
    unsigned long long key;
    int depth;
    int value;
    int flag; // 0: exact, 1: lower bound, 2: upper bound
    int bestX;
    int bestY;
} TTEntry;

// Larger TT for stronger play
#define TT_SIZE (1 << 21)
#define TT_FLAG_EXACT 0
#define TT_FLAG_LOWER 1
#define TT_FLAG_UPPER 2

unsigned long long zobrist_table[Board_Size][Board_Size][3];
unsigned long long zobrist_turn[2];
TTEntry transTable[TT_SIZE];

typedef struct
{
    int x;
    int y;
    int score;
} Move;

void init_zobrist(void);
unsigned long long rand64(void);
unsigned long long compute_hash(int myturn);

int count_empty(void);
int is_corner(int x, int y);
int is_x_square(int x, int y);
int is_c_square(int x, int y);
int move_heuristic(int x, int y);

int negamax(int depth, int alpha, int beta, int myturn);
int negamax_root(int depth, int myturn, int *outX, int *outY);

typedef struct location
{
    int i;
    int j;
    int g;
} Location;

//---------------------------------------------------------------------------

// Zobrist hashing initialization
void init_zobrist(void)
{
    int i, j, k;
    for (i = 0; i < Board_Size; ++i)
        for (j = 0; j < Board_Size; ++j)
            for (k = 0; k < 3; ++k)
                zobrist_table[i][j][k] = rand64();

    zobrist_turn[0] = rand64();
    zobrist_turn[1] = rand64();

    memset(transTable, 0, sizeof(transTable));
}

unsigned long long rand64(void)
{
    unsigned long long r = (unsigned long long)rand();
    r ^= ((unsigned long long)rand() << 15);
    r ^= ((unsigned long long)rand() << 30);
    r ^= ((unsigned long long)rand() << 45);
    r ^= ((unsigned long long)rand() << 60);
    return r;
}

unsigned long long compute_hash(int myturn)
{
    unsigned long long h = zobrist_turn[myturn];
    int i, j;
    for (i = 0; i < Board_Size; ++i)
        for (j = 0; j < Board_Size; ++j)
            if (Now_Board[i][j] != 0)
                h ^= zobrist_table[i][j][Now_Board[i][j]];
    return h;
}

int count_empty(void)
{
    int i, j;
    int empty = 0;
    for (i = 0; i < Board_Size; ++i)
        for (j = 0; j < Board_Size; ++j)
            if (Now_Board[i][j] == 0)
                empty++;
    return empty;
}

int is_corner(int x, int y)
{
    return ((x == 0 || x == Board_Size - 1) && (y == 0 || y == Board_Size - 1));
}

int is_x_square(int x, int y)
{
    return ((x == 1 && y == 1) ||
            (x == Board_Size - 2 && y == 1) ||
            (x == 1 && y == Board_Size - 2) ||
            (x == Board_Size - 2 && y == Board_Size - 2));
}

int is_c_square(int x, int y)
{
    if ((x == 0 && y == 1) || (x == 1 && y == 0) ||
        (x == Board_Size - 2 && y == 0) || (x == Board_Size - 1 && y == 1) ||
        (x == 0 && y == Board_Size - 2) || (x == 1 && y == Board_Size - 1) ||
        (x == Board_Size - 2 && y == Board_Size - 1) || (x == Board_Size - 1 && y == Board_Size - 2))
        return 1;
    return 0;
}

int move_heuristic(int x, int y)
{
    int score = board_weight[x][y];
    int empty = count_empty();
    int discs = Board_Size * Board_Size - empty;

    // Corners are extremely valuable
    if (is_corner(x, y))
        score += 10000;

    // X-squares are dangerous if corner empty
    if (is_x_square(x, y))
    {
        if (x == 1 && y == 1 && Now_Board[0][0] == 0)
            score -= (discs < 48) ? 8000 : 2000;
        if (x == Board_Size - 2 && y == 1 && Now_Board[Board_Size - 1][0] == 0)
            score -= (discs < 48) ? 8000 : 2000;
        if (x == 1 && y == Board_Size - 2 && Now_Board[0][Board_Size - 1] == 0)
            score -= (discs < 48) ? 8000 : 2000;
        if (x == Board_Size - 2 && y == Board_Size - 2 && Now_Board[Board_Size - 1][Board_Size - 1] == 0)
            score -= (discs < 48) ? 8000 : 2000;
    }

    // C-squares also dangerous early if corner empty
    if (is_c_square(x, y))
    {
        int penalty = (discs < 40) ? 4000 : 1000;

        if (x == 0 && y == 1 && Now_Board[0][0] == 0)
            score -= penalty;
        if (x == 1 && y == 0 && Now_Board[0][0] == 0)
            score -= penalty;

        if (x == Board_Size - 2 && y == 0 && Now_Board[Board_Size - 1][0] == 0)
            score -= penalty;
        if (x == Board_Size - 1 && y == 1 && Now_Board[Board_Size - 1][0] == 0)
            score -= penalty;

        if (x == 0 && y == Board_Size - 2 && Now_Board[0][Board_Size - 1] == 0)
            score -= penalty;
        if (x == 1 && y == Board_Size - 1 && Now_Board[0][Board_Size - 1] == 0)
            score -= penalty;

        if (x == Board_Size - 2 && y == Board_Size - 1 && Now_Board[Board_Size - 1][Board_Size - 1] == 0)
            score -= penalty;
        if (x == Board_Size - 1 && y == Board_Size - 2 && Now_Board[Board_Size - 1][Board_Size - 1] == 0)
            score -= penalty;
    }

    return score;
}

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
        Show_Board_and_Set_Legal_Moves();

        Computer_Think(&rx, &ry);
        printf("Computer played %c%d\n", rx + 97, ry + 1);
        Play_a_Move(rx, ry);
        if (Check_EndGame())
            return 0;
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

    srand((unsigned int)time(NULL));
    init_zobrist();
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
                    printf("O "); // white
                else
                    printf("X "); // black
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
    int k;
    int dx, dy;

    if (!In_Board(x, y) || Now_Board[x][y] == 0)
        return FALSE;

    {
        int army = 3 - Now_Board[x][y];
        int army_count = 0;

        for (k = 0; k < 8; k++)
        {
            dx = x + DirX[k];
            dy = y + DirY[k];
            if (In_Board(dx, dy) && Now_Board[dx][dy] == army)
            {
                army_count += Check_Straight_Army(x, y, k, update);
            }
        }

        if (army_count > 0)
            return TRUE;
        else
            return FALSE;
    }
}
//---------------------------------------------------------------------------

int Check_Straight_Army(int x, int y, int d, int update)
{
    int me = Now_Board[x][y];
    int army = 3 - me;
    int army_count = 0;
    int found_flag = FALSE;
    int flag[Board_Size][Board_Size] = {{0}};

    int i, j;
    int tx, ty;

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
                {
                    if (Now_Board[i][j] != 0)
                        Now_Board[i][j] = 3 - Now_Board[i][j];
                }
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

int Compute_Grades(int flag)
{
    int i, j;
    int B = 0, W = 0;
    int BW = 0, WW = 0;
    int frontierB = 0, frontierW = 0;

    // Disc counts and positional weights
    for (i = 0; i < Board_Size; ++i)
    {
        for (j = 0; j < Board_Size; ++j)
        {
            if (Now_Board[i][j] == 1)
            {
                B++;
                BW += board_weight[i][j];
            }
            else if (Now_Board[i][j] == 2)
            {
                W++;
                WW += board_weight[i][j];
            }
        }
    }

    // Frontier discs (stones adjacent to at least one empty square)
    for (i = 0; i < Board_Size; ++i)
    {
        for (j = 0; j < Board_Size; ++j)
        {
            if (Now_Board[i][j] == 0)
                continue;

            int k;
            int isFrontier = 0;
            for (k = 0; k < 8; ++k)
            {
                int nx = i + DirX[k];
                int ny = j + DirY[k];
                if (In_Board(nx, ny) && Now_Board[nx][ny] == 0)
                {
                    isFrontier = 1;
                    break;
                }
            }

            if (isFrontier)
            {
                if (Now_Board[i][j] == 1)
                    frontierB++;
                else if (Now_Board[i][j] == 2)
                    frontierW++;
            }
        }
    }

    // Mobility
    int mobilityBlack = Find_Legal_Moves(1);
    int mobilityWhite = Find_Legal_Moves(2);

    int totalDiscs = B + W;
    int stage = 0;
    if (totalDiscs > 0)
        stage = (totalDiscs * 100) / (Board_Size * Board_Size); // 0..100

    // Normalized differences
    int discDiffPerc = 0;
    int posDiffPerc = 0;
    int mobDiffPerc = 0;
    int frontierDiffPerc = 0;

    if (B + W != 0)
        discDiffPerc = 100 * (B - W) / (B + W);

    if (BW + WW != 0)
        posDiffPerc = 100 * (BW - WW) / (BW + WW);

    if (mobilityBlack + mobilityWhite != 0)
        mobDiffPerc = 100 * (mobilityBlack - mobilityWhite) / (mobilityBlack + mobilityWhite);

    if (frontierB + frontierW != 0)
        frontierDiffPerc = 100 * (frontierW - frontierB) / (frontierB + frontierW); // prefer fewer frontier

    // Game phase dependent weights
    int wDisc = 10 + stage;     // more important late
    int wPos = 100 - stage / 2; // slightly less important late
    int wMob = 100 - stage;     // opening/midgame
    int wFront = 100 - stage;   // opening/midgame

    int score = 0;
    score += wPos * posDiffPerc;
    score += wMob * mobDiffPerc;
    score += wFront * frontierDiffPerc;
    score += wDisc * discDiffPerc;

    score /= 10; // keep scale reasonable

    if (flag)
    {
        Black_Count = B;
        White_Count = W;
        printf("#%d Grade: Black %d, White %d\n", HandNumber, B, W);
    }

    // Positive score means advantage for Black, negative for White.
    return score;
}
//---------------------------------------------------------------------------

int Check_EndGame(void)
{
    int i, j;
    FILE *fp;

    Black_Count = White_Count = 0;
    for (i = 0; i < Board_Size; i++)
        for (j = 0; j < Board_Size; j++)
            if (Now_Board[i][j] == 1)
                Black_Count++;
            else if (Now_Board[i][j] == 2)
                White_Count++;

    if (Black_Count + White_Count == Board_Size * Board_Size)
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
//---------------------------------------------------------------------------

int negamax(int depth, int alpha, int beta, int myturn)
{
    int moveCount;
    int opponentMoves;
    int bestVal = -INF;
    int originalAlpha = alpha;

    unsigned long long key = compute_hash(myturn);
    int index = (int)(key & (TT_SIZE - 1));
    TTEntry *entry = &transTable[index];

    Search_Counter++;

    // TT lookup
    if (entry->key == key && entry->depth >= depth)
    {
        if (entry->flag == TT_FLAG_EXACT)
            return entry->value;
        else if (entry->flag == TT_FLAG_LOWER && entry->value > alpha)
            alpha = entry->value;
        else if (entry->flag == TT_FLAG_UPPER && entry->value < beta)
            beta = entry->value;

        if (alpha >= beta)
            return entry->value;
    }

    moveCount = Find_Legal_Moves(Stones[myturn]);
    if (moveCount == 0)
    {
        opponentMoves = Find_Legal_Moves(Stones[1 - myturn]);
        if (depth == 0 || opponentMoves == 0)
        {
            int eval = (myturn == 0 ? 1 : -1) * Compute_Grades(FALSE);
            entry->key = key;
            entry->depth = depth;
            entry->value = eval;
            entry->flag = TT_FLAG_EXACT;
            return eval;
        }
        // pass move
        {
            int val = -negamax(depth - 1, -beta, -alpha, 1 - myturn);
            entry->key = key;
            entry->depth = depth;
            entry->value = val;
            if (val <= originalAlpha)
                entry->flag = TT_FLAG_UPPER;
            else if (val >= beta)
                entry->flag = TT_FLAG_LOWER;
            else
                entry->flag = TT_FLAG_EXACT;
            return val;
        }
    }

    if (depth == 0)
    {
        int eval = (myturn == 0 ? 1 : -1) * Compute_Grades(FALSE);
        entry->key = key;
        entry->depth = depth;
        entry->value = eval;
        entry->flag = TT_FLAG_EXACT;
        return eval;
    }

    // Generate & order moves
    {
        Move moves[Board_Size * Board_Size];
        int m = 0;
        int i, j;
        for (i = 0; i < Board_Size; ++i)
            for (j = 0; j < Board_Size; ++j)
                if (Legal_Moves[i][j] == TRUE)
                {
                    moves[m].x = i;
                    moves[m].y = j;
                    moves[m].score = move_heuristic(i, j);
                    m++;
                }

        // TT best move ordering bonus if available
        if (entry->key == key && entry->bestX >= 0 && entry->bestY >= 0)
        {
            for (int t = 0; t < m; ++t)
                if (moves[t].x == entry->bestX && moves[t].y == entry->bestY)
                    moves[t].score += 1000000;
        }

        // sort by heuristic descending (insertion sort)
        for (int a = 1; a < m; ++a)
        {
            Move keyMove = moves[a];
            int b = a - 1;
            while (b >= 0 && moves[b].score < keyMove.score)
            {
                moves[b + 1] = moves[b];
                b--;
            }
            moves[b + 1] = keyMove;
        }

        {
            int B[Board_Size][Board_Size];
            int idxMove;
            int bestX = -1, bestY = -1;

            for (idxMove = 0; idxMove < m; ++idxMove)
            {
                int x = moves[idxMove].x;
                int y = moves[idxMove].y;
                int val;

                memcpy(B, Now_Board, sizeof(int) * Board_Size * Board_Size);
                Now_Board[x][y] = Stones[myturn];
                Check_Cross(x, y, TRUE);

                val = -negamax(depth - 1, -beta, -alpha, 1 - myturn);

                memcpy(Now_Board, B, sizeof(int) * Board_Size * Board_Size);

                if (val > bestVal)
                {
                    bestVal = val;
                    bestX = x;
                    bestY = y;
                }
                if (val > alpha)
                    alpha = val;
                if (alpha_beta_option && alpha >= beta)
                    break;
            }

            entry->key = key;
            entry->depth = depth;
            entry->value = bestVal;
            entry->bestX = bestX;
            entry->bestY = bestY;

            if (bestVal <= originalAlpha)
                entry->flag = TT_FLAG_UPPER;
            else if (bestVal >= beta)
                entry->flag = TT_FLAG_LOWER;
            else
                entry->flag = TT_FLAG_EXACT;
        }
    }

    return bestVal;
}

int negamax_root(int depth, int myturn, int *outX, int *outY)
{
    int moveCount = Find_Legal_Moves(Stones[myturn]);
    if (moveCount <= 0)
    {
        *outX = *outY = -1;
        return -INF;
    }

    Move moves[Board_Size * Board_Size];
    int m = 0;
    int i, j;

    for (i = 0; i < Board_Size; ++i)
        for (j = 0; j < Board_Size; ++j)
            if (Legal_Moves[i][j] == TRUE)
            {
                moves[m].x = i;
                moves[m].y = j;
                moves[m].score = move_heuristic(i, j);
                m++;
            }

    // Root: use TT best move for ordering if available
    {
        unsigned long long key = compute_hash(myturn);
        int index = (int)(key & (TT_SIZE - 1));
        TTEntry *entry = &transTable[index];
        if (entry->key == key && entry->bestX >= 0 && entry->bestY >= 0)
        {
            for (int t = 0; t < m; ++t)
                if (moves[t].x == entry->bestX && moves[t].y == entry->bestY)
                    moves[t].score += 1000000;
        }
    }

    // sort moves by heuristic descending (insertion sort)
    for (int a = 1; a < m; ++a)
    {
        Move keyMove = moves[a];
        int b = a - 1;
        while (b >= 0 && moves[b].score < keyMove.score)
        {
            moves[b + 1] = moves[b];
            b--;
        }
        moves[b + 1] = keyMove;
    }

    {
        int bestVal = -INF;
        int alpha = -INF;
        int beta = INF;
        int B[Board_Size][Board_Size];
        int idxMove;

        *outX = *outY = -1;

        for (idxMove = 0; idxMove < m; ++idxMove)
        {
            int x = moves[idxMove].x;
            int y = moves[idxMove].y;
            int val;

            memcpy(B, Now_Board, sizeof(int) * Board_Size * Board_Size);
            Now_Board[x][y] = Stones[myturn];
            Check_Cross(x, y, TRUE);

            val = -negamax(depth - 1, -beta, -alpha, 1 - myturn);

            memcpy(Now_Board, B, sizeof(int) * Board_Size * Board_Size);

            if (val > bestVal)
            {
                bestVal = val;
                *outX = x;
                *outY = y;
            }
            if (val > alpha)
                alpha = val;
        }

        return bestVal;
    }
}

int Search(int myturn, int mylevel)
{
    int legal;
    int empty;
    int maxDepth;
    int d;

    (void)mylevel; // unused

    legal = Find_Legal_Moves(Stones[myturn]);
    if (legal <= 0)
        return FALSE;

    empty = count_empty();
    maxDepth = search_deep;

    // Extend deeper in late game
    if (empty <= 14 && empty < maxDepth)
        maxDepth = empty;

    resultX = resultY = -1;

    // Iterative deepening for better move ordering and TT usage
    for (d = 1; d <= maxDepth; ++d)
    {
        int x = -1, y = -1;
        negamax_root(d, myturn, &x, &y);
        if (x != -1 && y != -1)
        {
            resultX = x;
            resultY = y;
        }
    }

    return (resultX != -1 && resultY != -1);
}

int search_next(int x, int y, int myturn, int mylevel, int alpha, int beta)
{
    // Legacy interface not used by new search; keep stub for compatibility.
    (void)x;
    (void)y;
    (void)myturn;
    (void)mylevel;
    (void)alpha;
    (void)beta;
    return Compute_Grades(FALSE);
}
//---------------------------------------------------------------------------

void Computer_Think(int *x, int *y)
{
    time_t clockBegin, clockEnd;
    int flag;

    clockBegin = clock();

    resultX = resultY = -1;
    Search_Counter = 0;

    flag = Search(Turn, 0);

    clockEnd = clock();
    {
        int tinterval = (int)(clockEnd - clockBegin);
        Think_Time += tinterval;
        if (tinterval < 200)
            Delay((unsigned int)(200 - tinterval));
    }
    printf("used thinking time= %d min. %d.%d sec.\n",
           Think_Time / 60000,
           (Think_Time % 60000) / 1000,
           (Think_Time % 60000) % 1000);

    if (flag)
    {
        *x = resultX;
        *y = resultY;
    }
    else
    {
        *x = *y = -1;
    }
}
//---------------------------------------------------------------------------