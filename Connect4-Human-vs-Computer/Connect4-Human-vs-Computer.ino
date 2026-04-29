/*
 * PROJECT: Connect 4 "Saddlebag" AI Game
 * DESCRIPTION:
 * This sketch runs an automated, physical Connect 4 board using a Raspberry Pi Pico.
 * The system monitors 7 IR sensors to detect human moves and controls a NeoPixel 
 * strip to indicate the CPU's move and provide game status. A 128x64 OLED display 
 * provides a user interface for turn tracking, difficulty selection (Levels 1-6), 
 * and a system menu for resetting the game or undoing mistakes. The AI uses a 
 * Minimax algorithm with Alpha-Beta pruning for high-level difficulty.
 *
 * HARDWARE COMPONENTS:
 * 1. Raspberry Pi Pico (Microcontroller)
 * 2. SSD1306 128x64 OLED Display (I2C)
 * 3. 7-LED NeoPixel Strip (WS2812B)
 * 4. 7 IR Sensors (to monitor board columns)
 * 5. 1 IR Gatekeeper Pin (to power/enable sensors)
 * 6. 1 Momentary Tactile Button (Multi-function Control)
 *
 * LIBRARIES:
 * - Wire.h: Handles I2C communication between the Pico and the OLED.
 * - Adafruit_GFX.h: Core graphics library for drawing text/shapes on the OLED.
 * - Adafruit_SSD1306.h: Specific driver for the SSD1306 OLED controller.
 * - Adafruit_NeoPixel.h: Controls the addressable LED strip.
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// --- OLED SETTINGS ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- HARDWARE PINS ---
#define PIXEL_PIN     22  // NeoPixel Data Pin
#define PIXEL_COUNT   7   // Number of LEDs in the strip
#define IR_GATEKEEPER 20  // Pin used to enable IR sensors
#define BUTTON_PIN    19  // Main user interface button

// --- GAME CONSTANTS ---
#define ROWS 6
#define COLS 7
#define EMPTY 0
#define HUMAN 1
#define CPU   2

// IR sensor pin assignments for columns 1-7
const int irPins[7] = {7, 8, 9, 10, 11, 12, 13}; 
Adafruit_NeoPixel strip(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

// --- GLOBAL STATE ---
int board[ROWS][COLS];
int columnHeights[COLS];
int currentLevel = 1;
bool isHumanTurn = false;
bool gameOver = false;
int cpuTargetCol = -1;
unsigned long levelDisplayTimer = 0; // Timer for OLED level pop-up

// Menu and Button Control State
enum MenuState { GAME_PLAY, MENU_RESET, MENU_UNDO };
MenuState currentMenu = GAME_PLAY;
unsigned long buttonPressStart = 0;
bool buttonActive = false;
bool menuMode = false;

// History Tracking for Undo Functionality
int moveHistory[42]; // Max moves in Connect 4
int historyIndex = 0; // Current position in the stack

// --- FUNCTION PROTOTYPES ---
void resetGame();
void handleHumanTurn();
void handleCPUTurn();
void makeMove(int col, int player);
int calculateCPUMove();
int minimax(int depth, int alpha, int beta, bool maximizing, int maxDepth);
int scoreBoard();
bool checkWin(int p);
bool checkTie();
void updateOLED();
void showWinAnimation(int times, int winner);
void showTieAnimation();

/**
 * Standard Arduino Setup Function
 * Configures pins, initializes serial, OLED, and LED strip.
 */
void setup() {
  Serial.begin(115200);

  // Initialize I2C OLED on GP4/GP5 pins
  Wire.setSDA(4);
  Wire.setSCL(5);
  Wire.begin();
  if(display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    display.setRotation(2); // Flip upside down for mounting orientation
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 20);
    display.println(F("Connect 4"));
    display.display();
    delay(2000);
  }

  // Initialize NeoPixel strip
  strip.begin();
  strip.setBrightness(40);
  strip.show();

  // Configure Hardware I/O
  pinMode(IR_GATEKEEPER, OUTPUT);
  digitalWrite(IR_GATEKEEPER, HIGH); 
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  for(int i = 0; i < 7; i++) pinMode(irPins[i], INPUT_PULLUP);

  // Seed random generator and start a new game
  randomSeed(analogRead(A0));
  resetGame();
}

/**
 * Main Execution Loop
 * Handles multi-function button timing and state management.
 */
void loop() {
  int btnReading = digitalRead(BUTTON_PIN);

  // --- BUTTON LOGIC (Handles Short vs Long Press) ---
  if (btnReading == LOW) {
    if (!buttonActive) {
      buttonActive = true;
      buttonPressStart = millis();
    }
    
    // Check for 2-second Long Press to enter or interact with the menu
    if (buttonActive && (millis() - buttonPressStart > 2000)) {
      handleLongPress();
      buttonActive = false; 
      if (menuMode) drawMenu(); 
      while(digitalRead(BUTTON_PIN) == LOW) { delay(10); } // Wait for button release
    }
  } else {
    if (buttonActive) {
      // Short Press detected if button is released before the 2-second threshold
      if (millis() - buttonPressStart < 1000) {
        handleShortPress(); 
      }
      buttonActive = false;
    }
  }

  // --- GAMEPLAY EXECUTION ---
  if (gameOver) {
    // Halt logic while game results are displayed; wait for reset via button
    return; 
  }

  // Determine whether to run gameplay logic or display the system menu
  if (!menuMode) {
    updateOLED(); 
    if (isHumanTurn) handleHumanTurn();
    else handleCPUTurn();
  } else {
    drawMenu(); 
  }
}

/**
 * Main Gameplay OLED Interface
 * Draws turn status, level indicators, and the long-press progress bar.
 */
void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  // 1. --- PROGRESS BAR (Visual feedback for long-press) ---
  if (buttonActive && !menuMode) {
    unsigned long holdTime = millis() - buttonPressStart;
    if (holdTime > 100) { 
      int barWidth = map(holdTime, 0, 2000, 0, 128);
      barWidth = constrain(barWidth, 0, 128);
      
      display.drawRect(0, 58, 128, 6, SSD1306_WHITE);
      display.fillRect(0, 58, barWidth, 6, SSD1306_WHITE);
    }
  }

  // 2. --- LEVEL POP-UP (Displayed for 2 seconds after a level change) ---
  if (millis() - levelDisplayTimer < 2000) {
    display.setTextSize(3); 
    display.setCursor(20, 5);
    display.println(F("Level"));
    display.setTextSize(4);
    display.setCursor(55, 32); 
    display.print(currentLevel);
  } 
  // 3. --- MAIN GAMEPLAY SCREEN (Displays current turn and L-prefix level) ---
  else {
    display.setTextSize(3);
    
    // Left-Justified Turn Text
    if (isHumanTurn) {
      display.setCursor(0, 5);
      display.println(F("YOUR"));
      display.setCursor(0, 35);
      display.println(F("TURN"));
    } else {
      display.setCursor(0, 5);
      display.println(F("MY"));
      display.setCursor(0, 35);
      display.println(F("TURN"));
    }
    
    // Bottom-Right Level Indicator (Size 3)
    display.setCursor(90, 35); 
    display.print(F("L"));
    display.print(currentLevel);
  }
  display.display();
}

/**
 * Game State Initialization
 * Clears the board and runs the "NEW GAME" startup sequence.
 */
void resetGame() {
  // 1. Clear Internal Board Logic and history
  for(int r=0; r<ROWS; r++) for(int c=0; c<COLS; c++) board[r][c] = EMPTY;
  for(int c=0; c<COLS; c++) columnHeights[c] = 0;
  historyIndex = 0;
  gameOver = false;
  cpuTargetCol = -1;
  levelDisplayTimer = 0;

  // 2. Display Splash Screen on OLED
  display.clearDisplay();
  display.setTextSize(3); 
  display.setCursor(0, 5);
  display.println(F("NEW"));
  display.setCursor(0, 35);
  display.println(F("GAME!"));
  display.display();

  // 3. Run Start-up LED sequence (3 seconds total)
  for (int t=0; t<3; t++) {
    uint32_t white = strip.Color(150, 150, 150);
    for(int i=0; i<7; i++) { strip.setPixelColor(i, white); strip.show(); delay(70); }
    for(int i=6; i>=0; i--) { strip.setPixelColor(i, 0); strip.show(); delay(70); }
  }

  // 4. Randomize starting player
  isHumanTurn = (random(0, 2) == 0);
  Serial.println("New Match Started.");
}

/**
 * Human Turn Logic
 * Pulses green LEDs and checks IR sensors for a physical checker drop.
 */
void handleHumanTurn() {
  // Breathing effect for LEDs
  float breath = (exp(sin(millis() / 500.0 * PI)) - 0.36787944) * 108.0;
  for (int i = 0; i < COLS; i++) {
    int ledIdx = 6 - i;
    if (columnHeights[i] < ROWS) strip.setPixelColor(ledIdx, strip.Color(0, breath, 0));
    else strip.setPixelColor(ledIdx, 0);
  }
  strip.show();

  // Scan IR sensors for input
  for (int i = 0; i < COLS; i++) {
    if (digitalRead(irPins[i]) == HIGH && columnHeights[i] < ROWS) {
      makeMove(i, HUMAN);
      isHumanTurn = false;
      delay(1000);
      break;
    }
  }
}

/**
 * CPU Turn Logic
 * Selects a column based on AI level and waits for physical checker drop.
 */
void handleCPUTurn() {
  if (cpuTargetCol == -1) cpuTargetCol = calculateCPUMove();

  // Breathing effect for target LED (Blue)
  float breath = (exp(sin(millis() / 500.0 * PI)) - 0.36787944) * 108.0;
  strip.clear();
  strip.setPixelColor(6 - cpuTargetCol, strip.Color(0, 0, breath)); 
  strip.show();

  // Wait for the human to drop the CPU's checker into the correct column
  if (digitalRead(irPins[cpuTargetCol]) == HIGH) {
    makeMove(cpuTargetCol, CPU);
    cpuTargetCol = -1;
    isHumanTurn = true;
    delay(1000);
  }
}

/**
 * Move Processor
 * Updates the board array, move history, and checks for win/tie conditions.
 */
void makeMove(int col, int player) {
  board[columnHeights[col]][col] = player;
  
  // Save move to history for Undo function
  moveHistory[historyIndex] = col;
  historyIndex++;
  
  columnHeights[col]++;
  
  // Check game results
  if (checkWin(player)) {
    delay(1000); 
    if (player == HUMAN) showEndGame("YOU WIN!");
    else showEndGame("I WIN!");
    showWinAnimation(5, player);
    gameOver = true;
  } 
  else if (checkTie()) {
    delay(1000);
    showEndGame("TIE GAME");
    showTieAnimation();
    gameOver = true;
  }
}

/**
 * AI Decision Engine
 * Routes move selection based on current difficulty level (1-6).
 */
int calculateCPUMove() {
  // Levels 1-2 utilize simple immediate threat/opportunity checking
  if (currentLevel <= 2) {
    for (int c=0; c<7; c++) if (columnHeights[c]<6) { 
      board[columnHeights[c]][c]=CPU; 
      if(checkWin(CPU)){ board[columnHeights[c]][c]=EMPTY; return c; } 
      board[columnHeights[c]][c]=EMPTY; 
    }
    for (int c=0; c<7; c++) if (columnHeights[c]<6) { 
      board[columnHeights[c]][c]=HUMAN; 
      if(checkWin(HUMAN)){ board[columnHeights[c]][c]=EMPTY; return c; } 
      board[columnHeights[c]][c]=EMPTY; 
    }

    if (currentLevel == 1) {
      int avail[7], count=0;
      for (int c=0; c<7; c++) if(columnHeights[c]<6) avail[count++]=c;
      return avail[random(0, count)];
    } else {
      int preferred[] = {3, 2, 4, 1, 5, 0, 6};
      for (int i=0; i<7; i++) {
        if (columnHeights[preferred[i]] < 6) return preferred[i];
      }
    }
  }

  // Levels 3-6 utilize Minimax depth-based lookahead
  int bestCol = 3, bestVal = -30000, searchDepth;
  switch(currentLevel) {
    case 3: searchDepth = 2; break; 
    case 4: searchDepth = 3; break; 
    case 5: searchDepth = 4; break; 
    case 6: searchDepth = 6; break; 
    default: searchDepth = 3; break;
  }

  // Iterate through columns to find the move with the highest Minimax score
  for (int c = 0; c < COLS; c++) {
    if (columnHeights[c] < ROWS) {
      board[columnHeights[c]][c] = CPU; columnHeights[c]++;
      int moveVal = minimax(0, -30000, 30000, false, searchDepth);
      columnHeights[c]--; board[columnHeights[c]][c] = EMPTY;
      if (moveVal > bestVal) { bestVal = moveVal; bestCol = c; }
      else if (moveVal == bestVal && abs(c-3) < abs(bestCol-3)) bestCol = c;
    }
  }
  return bestCol;
}

/**
 * Minimax Algorithm with Alpha-Beta Pruning
 * Recursively evaluates future game states to find optimal moves.
 */
int minimax(int depth, int alpha, int beta, bool maximizing, int maxDepth) {
  if (checkWin(CPU)) return 10000 - depth;
  if (checkWin(HUMAN)) return -10000 + depth;
  if (depth == maxDepth || checkTie()) return scoreBoard();

  if (maximizing) {
    int maxEval = -20000;
    for (int c=0; c<7; c++) if (columnHeights[c]<6) {
      board[columnHeights[c]][c]=CPU; columnHeights[c]++;
      int eval = minimax(depth+1, alpha, beta, false, maxDepth);
      columnHeights[c]--; board[columnHeights[c]][c]=EMPTY;
      maxEval = max(maxEval, eval); alpha = max(alpha, eval);
      if (beta <= alpha) break; // Pruning
    }
    return maxEval;
  } else {
    int minEval = 20000;
    for (int c=0; c<7; c++) if (columnHeights[c]<6) {
      board[columnHeights[c]][c]=HUMAN; columnHeights[c]++;
      int eval = minimax(depth+1, alpha, beta, true, maxDepth);
      columnHeights[c]--; board[columnHeights[c]][c]=EMPTY;
      minEval = min(minEval, eval); beta = min(beta, eval);
      if (beta <= alpha) break; // Pruning
    }
    return minEval;
  }
}

/**
 * Line Evaluator
 * Assigns scores to groups of 4 cells for the AI heuristic.
 */
int evaluateLine(int b1, int b2, int b3, int b4) {
  int score = 0, cpu = 0, hum = 0, emp = 0;
  int v[] = {b1, b2, b3, b4};
  for(int i=0; i<4; i++) { if(v[i]==CPU) cpu++; else if(v[i]==HUMAN) hum++; else emp++; }
  if (cpu == 4) return 1000;
  if (cpu == 3 && emp == 1) score += 10;
  if (hum == 3 && emp == 1) score -= 80;
  return score;
}

/**
 * Board Scorer
 * Calculates total heuristic value of the current board for the AI.
 */
int scoreBoard() {
  int score = 0;
  for(int r=0; r<ROWS; r++) if(board[r][3] == CPU) score += 3; // Center column bonus
  for(int r=0; r<6; r++) for(int c=0; c<4; c++) score += evaluateLine(board[r][c], board[r][c+1], board[r][c+2], board[r][c+3]);
  for(int c=0; c<7; c++) for(int r=0; r<3; r++) score += evaluateLine(board[r][c], board[r+1][c], board[r+2][c], board[r+3][c]);
  return score;
}

/**
 * Win Detection
 * Scans board for 4-in-a-row in all directions.
 */
bool checkWin(int p) {
  for (int r=0; r<6; r++) for (int c=0; c<4; c++) if (board[r][c]==p && board[r][c+1]==p && board[r][c+2]==p && board[r][c+3]==p) return true;
  for (int r=0; r<3; r++) for (int c=0; c<7; c++) if (board[r][c]==p && board[r+1][c]==p && board[r+2][c]==p && board[r+3][c]==p) return true;
  for (int r=0; r<3; r++) for (int c=0; c<4; c++) if (board[r][c]==p && board[r+1][c+1]==p && board[r+2][c+2]==p && board[r+3][c+3]==p) return true;
  for (int r=3; r<6; r++) for (int c=0; c<4; c++) if (board[r][c]==p && board[r-1][c+1]==p && board[r-2][c+2]==p && board[r-3][c+3]==p) return true;
  return false;
}

/**
 * Tie Detection
 * Checks if all columns are filled.
 */
bool checkTie() {
  for (int c=0; c<7; c++) if (columnHeights[c]<6) return false;
  return true;
}

/**
 * Win Animation
 * Flashes LEDs in Green (Human) or Blue (CPU) upon a win.
 */
void showWinAnimation(int times, int winner) {
  uint32_t color = (winner == HUMAN) ? strip.Color(0, 255, 0) : (winner == CPU ? strip.Color(0, 0, 255) : strip.Color(255, 255, 255));
  for (int t=0; t<times; t++) {
    for(int i=0; i<7; i++) { strip.setPixelColor(i, color); strip.show(); delay(40); }
    for(int i=6; i>=0; i--) { strip.setPixelColor(i, 0); strip.show(); delay(40); }
  }
}

/**
 * Tie Animation
 * Cycles LEDs through a rainbow pattern.
 */
void showTieAnimation() {
  for (long firstPixelHue = 0; firstPixelHue < 3 * 65536; firstPixelHue += 512) {
    for (int i = 0; i < 7; i++) {
      int pixelHue = firstPixelHue + (i * 65536L / 7);
      strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue)));
    }
    strip.show(); delay(10);
  }
  strip.clear(); strip.show();
}

/**
 * Short Button Press Handler
 * Manages level cycling, menu navigation, and end-game resets.
 */
void handleShortPress() {
  if (gameOver) {
    resetGame(); 
    return;
  }

  if (!menuMode) {
    currentLevel++;
    if (currentLevel > 6) currentLevel = 1;
    levelDisplayTimer = millis();
  } else {
    if (currentMenu == MENU_RESET) currentMenu = MENU_UNDO;
    else if (currentMenu == MENU_UNDO) exitMenu();
  }
}

/**
 * Long Button Press Handler (2 Seconds)
 * Enters the system menu or executes a Reset/Undo action.
 */
void handleLongPress() {
  if (!menuMode) {
    menuMode = true;
    currentMenu = MENU_RESET;
  } else {
    if (currentMenu == MENU_RESET) {
      resetGame();
      exitMenu();
    } else if (currentMenu == MENU_UNDO) {
      executeUndo();
    }
  }
}

/**
 * Menu Exit Logic
 * Reverts system state back to standard gameplay.
 */
void exitMenu() {
  menuMode = false;
  currentMenu = GAME_PLAY;
}

/**
 * System Menu Drawing
 * Renders Reset/Undo options and the confirmation progress bar.
 */
void drawMenu() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);

  if (currentMenu == MENU_RESET) {
    display.setCursor(0, 5);
    display.println(F("Long:Reset"));
    display.setCursor(0, 30);
    display.println(F("Short:Next"));
  } 
  else if (currentMenu == MENU_UNDO) {
    display.setCursor(0, 5);
    display.println(F("Long:Undo"));
    display.setCursor(0, 30);
    display.println(F("Short:Exit"));
  }

  if (buttonActive && menuMode) {
    unsigned long holdTime = millis() - buttonPressStart;
    if (holdTime > 100) {
      int barWidth = map(holdTime, 0, 2000, 0, 128);
      barWidth = constrain(barWidth, 0, 128);
      display.drawRect(0, 58, 128, 6, SSD1306_WHITE);
      display.fillRect(0, 58, barWidth, 6, SSD1306_WHITE);
    }
  }
  display.display();
}

/**
 * Undo Execution Logic
 * Reverts board state by 1 turn and prompts user to physically remove piece.
 */
void executeUndo() {
  if (historyIndex == 0) {
    exitMenu();
    return;
  }

  undoSinglePiece();
  isHumanTurn = !isHumanTurn; // Rewind turn flag
  cpuTargetCol = -1;
  gameOver = false; 

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 5);
  display.println(F("UNDONE"));
  display.setCursor(0, 32);
  display.println(F("Press when"));
  display.setCursor(0, 50);
  display.println(F("ready"));
  display.display();

  // Block execution until user confirms physical board cleanup
  delay(500); 
  while(digitalRead(BUTTON_PIN) == HIGH); 
  delay(500); 
  exitMenu();
}

/**
 * Move History Stack Management
 * Pops the last move off the stack and clears that board slot.
 */
void undoSinglePiece() {
  if (historyIndex > 0) {
    historyIndex--; 
    int col = moveHistory[historyIndex];
    if (columnHeights[col] > 0) {
      columnHeights[col]--; 
      board[columnHeights[col]][col] = EMPTY; 
    }
  }
}

/**
 * End Game OLED Display
 * Renders centered "YOU WIN!", "I WIN!", or "TIE GAME" messages.
 */
void showEndGame(String msg) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(15, 25); 
  display.println(msg);
  display.display();
}