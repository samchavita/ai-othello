#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void Delay(unsigned int mseconds)
{
    clock_t goal = mseconds + clock();
    while (goal > clock())
        ;
}

void WaitClose()
{
    FILE *fp;
    char tc[10];

    while (tc[0] != 'w')
    {
        fp = fopen("of.txt", "r");
        while ((fscanf(fp, "%s", tc)) != EOF)
            ;
        fclose(fp);
        Delay(100);
    }
}
void PlayFirst(int n, char *pn, char *tc, int d)
{
    // static int ss=0, fs=0, swin=0, fwin=0;
    char p[100], cm[30] = "", a[3], *s, ss[10];
    FILE *fp;

    s = ss;

    printf("#%d Game, I First.\n", n);

    sprintf(p, "%s.exe F %d", pn, d);
    printf("%s\n", p);
    system(p);
    WaitClose();

    Delay(1000);
    fp = fopen("of.txt", "r");
    while ((fscanf(fp, "%s", tc)) != EOF)
        ;
    fclose(fp);

    s = &(tc[1]);
    sprintf(cm, "copy of.txt of_%d_%s.txt", n, s);
    printf("\ncmd: copy of.txt of_%d_%s.txt\n", n, s);
    system(cm);

    s = &(tc[2]);
    fp = fopen("result.txt", "a");
    fprintf(fp, "%d, %c win, %d\n", n, tc[1], atoi(s));
    fclose(fp);
    // sprintf(TotalFile[n],"%s_%d_%s.txt", pn, n, tc );
    Delay(3000);
}

void PlaySecond(int n, char *pn, int d)
{
    char p[100];

    printf("#%d Game, I Second.\n", n);
    sprintf(p, "%s.exe S %d", pn, d);
    printf("cmd: %s\n", p);
    system(p);
    WaitClose();

    Delay(3000);
}

void ReadResult()
{
    FILE *fp;
    int i, n, bs = 0, ws = 0, w;
    // float fwin=0, swin=0;
    int fwin = 0, swin = 0;
    char c;

    fp = fopen("result.txt", "r");
    while ((fscanf(fp, "%d, %c win, %d", &n, &c, &w)) != EOF)
    {
        printf("%d, %c win, %d\n", n, c, w);
        if (n % 2 == 1)
        {
            if (c == 'B')
            {
                fwin++;
                bs += w;
            }
            else if (c == 'W')
            {
                swin++;
                ws += w;
            }
        }
        else
        {
            if (c == 'W')
            {
                fwin++;
                bs += w;
            }
            else if (c == 'B')
            {
                swin++;
                ws += w;
            }
        }
    }

    fclose(fp);
    if (fwin == swin)
    {
        if (bs > ws)
            w = 1; // Black win
        else
            w = 0;
    }
    else if (fwin > swin)
        w = 1;
    else
        w = 0;

    if (w == 1)
        printf("First:Second = %d:%d First win %2.2f%% games with %d\n", fwin, swin, (float)fwin * 100 / (fwin + swin), bs - ws);
    else
        printf("First:Second = %d:%d Second win %2.2f%% games with %d\n", fwin, swin, (float)swin * 100 / (fwin + swin), ws - bs);

    return;
}

int main(int argc, char *argv[])
{
    int i, n, winn, d = 0;
    char player[100], pn[100], c[10], tc[10] = "aaa", *s, ss[10], tt[10] = "aaaaa";
    FILE *fp;
    float winrate;

    if (argc < 4)
    {
        ReadResult();
        return 0;
    }
    sprintf(pn, "del of_*.txt", pn);
    system(pn);

    sprintf(player, "%s.exe", argv[1]);
    strcpy(pn, argv[1]);
    strcpy(c, argv[2]);

    if (argc >= 5)
        d = atoi(argv[4]);

    s = ss;
    fp = fopen("result.txt", "w");
    fclose(fp);

    for (i = 0; i < atoi(argv[3]); i++)
    {
        if (c[0] == 'F')
        {
            if (i % 2 == 0)
            {
                PlayFirst(i + 1, pn, tc, d);
            }
            else
            {
                PlaySecond(i + 1, pn, d);
            }
        }
        if (c[0] == 'S')
        {
            if (i % 2 == 1)
            {
                PlayFirst(i + 1, pn, tc, d);
            }
            else
            {
                PlaySecond(i + 1, pn, d);
            }
        }
    }
    ReadResult();

    return 0;
}