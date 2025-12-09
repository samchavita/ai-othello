# Othello AI Engine - Complete Rewrite Specification

## Overview
Rewrite the Othello AI engine (`Ot8b.c`) from scratch using the most efficient and intelligent algorithms known to competitive game AI. This is a tournament-grade Othello/Reversi solver that must:

1. **Execute moves with perfect legality** - every move must be valid and properly recorded
2. **Maintain file synchronization** - all moves MUST be written to `of.txt` for the dependent tournament referee (`run.c`)
3. **Maximize playing strength** - implement state-of-the-art evaluation and search techniques
4. **Optimize for speed** - complete moves within tournament time constraints while maintaining depth

---

## Critical Constraints

### File I/O Protocol (Non-Negotiable)
- **Input file**: `of.txt`
  - Line 1: Move count (integer)
  - Lines 2+: Previous moves in format `<col><row>` (e.g., `c4`, `h8`) or `p9` for PASS
  - End marker: `w` prefix (e.g., `wB`, `wW`, `wZ`) signals game over
  
- **Output file**: `of.txt` (append-only during game)
  - EVERY move must be appended with format: `<col><row>\n` or `p9\n` for PASS
  - Update line 1 with new move count: `fprintf(fp, "%2d\n", HandNumber + 1);`
  - Example sequence:
    ```
    1
    c4
    d3
    c5
    e6
    ```

### Tournament Modes
Support command-line arguments:
- `Ot8b.exe F` - Play as First player (Black, move 1)
- `Ot8b.exe S` - Play as Second player (White, move 2)
- Default `[depth]` = 6 ply

### Board Representation
```c
// Stone values
#define EMPTY    0
#define BLACK    1
#define WHITE    2

// Board[x][y] where x = column (0-7, a-h), y = row (0-7, 1-8)
// Column-major: a=0, b=1, ..., h=7
// Row-major: 1=0, 2=1, ..., 8=7
int Board[8][8];
```

### Turn Management
- Turn 0 = Black (moves 1, 3, 5, ...)
- Turn 1 = White (moves 2, 4, 6, ...)
- Move count starts at 1 (increment after each move)
- Detect pass when no legal moves exist → write `p9\n`

---

## Algorithm Requirements (Priority Order)

### 1. **Move Validation & Generation** (CRITICAL)
- **Implement efficient legal move detection**:
  - For each empty square, test all 8 directions simultaneously
  - A move is legal if ≥1 opponent piece in a direction, followed by friendly piece
  - Use direction vectors: `DirX[8] = {0,1,1,1,0,-1,-1,-1}`, `DirY[8] = {-1,-1,0,1,1,1,0,-1}`
  
- **Optimize piece flipping**:
  - Instead of allocating 8×8 flag arrays per move, track flip coordinates in a simple array:
    ```c
    struct {
        int x, y;
    } flip_list[6];  // Max ~6 pieces per direction
    ```
  - Single-pass flip: no nested O(64) loops
  - Performance: ~90% faster than original implementation

### 2. **Search Algorithm** (Alpha-Beta Pruning with Enhancements)
- **Base**: Minimax with alpha-beta pruning
- **Iterative Deepening**: Search depth 6 + overtime extension to depth 8 if time permits
- **Move Ordering** (critical for pruning efficiency):
  - Sort moves by position value (corners >> edges >> center)
  - Prioritize moves that maximize piece count
  - Include killer move heuristics from previous ply
  
- **Transposition Table** (optional but recommended):
  - Store evaluated positions in hash table with Zobrist hashing
  - Prune re-evaluation of equivalent board states
  - Memory: ~100K positions
  
- **Endgame Detection**:
  - Switch to exact solver (full minimax) in last 12 moves
  - No evaluation needed—count final material directly

### 3. **Position Evaluation Function** (Replaces Uniform Weights)
Implement multi-factor evaluation:

```
eval = w1 * material_score + w2 * mobility_score + w3 * stability_score + 
       w4 * positional_score + w5 * frontier_score
```

**A. Material Score** (Piece Count)
```
material = black_count - white_count
Range: [-64, 64]
```

**B. Mobility Score** (Available Moves)
```
mobility = log(legal_moves_black + 1) - log(legal_moves_white + 1)
Rationale: More options = better position (early/mid-game crucial)
Range: [-3, 3]
```

**C. Stability Score** (Corner & Edge Control)
```
Stable pieces = pieces that cannot be flipped next move
- Corners (a1, a8, h1, h8): +25 each if owned, -25 if opponent
- Edge pieces: +3 each if stable, -3 if threatened
- Never count unstable pieces
```

**D. Positional Score** (Strategic Squares)
```
Position value matrix (by square):
┌─────────────────────────────────┐
│100 -20  10  5  5  10 -20 100│
│-20 -50  -2 -1 -1  -2 -50 -20│
│ 10  -2   1  1  1   1  -2  10│
│  5  -1   1  0  0   1  -1   5│
│  5  -1   1  0  0   1  -1   5│
│ 10  -2   1  1  1   1  -2  10│
│-20 -50  -2 -1 -1  -2 -50 -20│
│100 -20  10  5  5  10 -20 100│
└─────────────────────────────────┘

Corners (100): Most valuable, stable endgame pieces
X-squares (-20): Adjacent to corners, easily flipped
Edges (10): Good for reducing opponent mobility
Center (0-1): Low inherent value
```

**E. Frontier Score** (Mobility Reduction)
```
frontier = count of opponent pieces adjacent to empty squares
Lower = better (limits opponent options)
frontier_score = (opponent_frontier - our_frontier) * 0.5
```

**F. Weights** (Tune empirically):
```
Early game (moves 1-15):
  w1=0.1, w2=2.0, w3=0.5, w4=1.0, w5=0.3

Mid-game (moves 16-52):
  w1=1.0, w2=1.5, w3=1.0, w4=2.0, w5=0.5

Endgame (moves 53+):
  w1=10.0, w2=0, w3=2.0, w4=0.5, w5=0  (material dominates)
```

### 4. **File Synchronization** (Rock-Solid)

**Read opponent moves**:
```c
int last_move_count = 0;  // Track position in file

int Read_Opponent_Move(char *move) {
    FILE *fp = fopen("of.txt", "r");
    int move_count;
    fscanf(fp, "%d", &move_count);
    
    // Skip moves we've already processed
    char buffer[10];
    for (int i = 0; i < move_count; i++) {
        fscanf(fp, "%s", buffer);
        if (i == move_count - 1) {  // Last move
            strcpy(move, buffer);
            if (move[0] == 'w') return GAME_OVER;  // End marker
            if (move[0] == 'p') return PASS;
        }
    }
    fclose(fp);
    return MOVE_OK;
}
```

**Write our move**:
```c
void Write_Move(int col, int row) {
    FILE *fp = fopen("of.txt", "r+");
    fprintf(fp, "%2d\n", HandNumber + 1);  // Update count
    fclose(fp);
    
    fp = fopen("of.txt", "a");
    fprintf(fp, "%c%d\n", col + 'a', row + 1);  // Append move
    fclose(fp);
}

void Write_Pass(void) {
    FILE *fp = fopen("of.txt", "r+");
    fprintf(fp, "%2d\n", HandNumber + 1);
    fclose(fp);
    
    fp = fopen("of.txt", "a");
    fprintf(fp, "p9\n");
    fclose(fp);
}
```

---

## Code Structure (Recommended Organization)

```c
// ============= BOARD STATE =============
typedef struct {
    int board[8][8];
    int move_count;
    int turn;  // 0=black, 1=white
} GameState;

// ============= DIRECTION VECTORS =============
int DirX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
int DirY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// ============= CORE FUNCTIONS =============
int Is_Legal_Move(GameState *g, int x, int y);
int Get_Legal_Moves(GameState *g, int moves[][2], int color);
void Apply_Move(GameState *g, int x, int y);
void Undo_Move(GameState *g, int x, int y);  // For search

// ============= EVALUATION =============
int Evaluate_Position(GameState *g);
int Count_Stable_Pieces(GameState *g, int color);
int Count_Legal_Moves(GameState *g, int color);
int Count_Frontier(GameState *g, int color);

// ============= SEARCH =============
int Minimax(GameState *g, int depth, int alpha, int beta, int is_max);
int Iterative_Deepening(GameState *g, int max_depth, int time_limit_ms);

// ============= FILE I/O =============
int Read_Opponent_Move(char *move);
void Write_Move(int col, int row);
void Write_Pass(void);
int Load_Game_From_File(GameState *g);

// ============= MAIN GAME LOOP =============
int main(int argc, char *argv[]);
```

---

## Implementation Priorities

### Phase 1: **Correctness** (MUST HAVE)
- [ ] Board initialization (4-piece start)
- [ ] Legal move detection (all 8 directions)
- [ ] Piece flipping logic
- [ ] File read/write with perfect synchronization
- [ ] No out-of-bounds access
- [ ] Pass detection
- [ ] Game end detection

### Phase 2: **Intelligence** (HIGH VALUE)
- [ ] Strategic position evaluation (not uniform 1s)
- [ ] Alpha-beta pruning
- [ ] Move ordering by board position value
- [ ] Separate endgame handling

### Phase 3: **Optimization** (COMPETITIVE EDGE)
- [ ] Transposition table (Zobrist hashing)
- [ ] Iterative deepening with time management
- [ ] Killer move heuristics
- [ ] Bitboard representation (if rewrite for extreme speed)

### Phase 4: **Robustness**
- [ ] Timeout handling
- [ ] Tournament mode testing
- [ ] Stale move detection
- [ ] File corruption recovery

---

## Testing Protocol

1. **Unit Tests**:
   - Legal move detection on known positions
   - Flip counting accuracy
   - File I/O round-trip validation

2. **Integration Tests**:
   - Self-play (mode A): Verify all moves written correctly
   - File mode (F vs S): Simulate tournament with external referee
   - Endgame: Verify exact minimax on final 12 moves

3. **Performance Benchmarks**:
   - Moves per second: Target >5000 evals/sec at depth 6
   - Move time: <2 seconds per turn (tournament standard)
   - Memory: <50MB for full game

---

## Command-Line Interface

```bash
# Tournament mode (F = First/Black, S = Second/White)
./Ot8b F               # Black, depth 6
./Ot8b S               # White, depth 8

# Default
./Ot8b                  # Interactive menu
```

---

## Success Criteria

✓ All moves written to `of.txt` with correct format  
✓ Zero illegal moves generated  
✓ Plays valid passes when no moves available  
✓ Beats original AI consistently  
✓ Completes move in <2 seconds at depth 6  
✓ Handles endgame perfectly (exact solve last 12 moves)  
✓ Tournament-compatible with `run.c` referee  

---

## References & Best Practices

1. **Zobrist Hashing** for transposition tables
2. **Killer Move Heuristic** for move ordering
3. **Endgame Solver** (alpha-beta with no evaluation, pure minimax)
4. **Bitboard Techniques** for ultra-fast move generation (optional)
5. **Opening Book** for first 8 moves (optional strength boost)

---

## Performance Target

Against original `Ot8b.c`:
- **Win rate**: >80% in tournament play
- **Move quality**: Selects objectively better moves in 90% of positions
- **Execution**: Completes all 60 moves in <120 seconds total
