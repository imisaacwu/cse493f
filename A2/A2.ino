/*
  A2: Arduino OLED Video Game
  by Isaac Wu
*/

#include <FastLED.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <avr/pgmspace.h>

// Hardware Setup
#define SCREEN_W 128 // OLED display width, in pixels
#define SCREEN_H 64 // OLED display height, in pixels
#define OLED_RESET -1 // Sharing Arduino reset pin
#define SCREEN_ADDRESS 0x3D // Defined by datasheet for 128x64
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, OLED_RESET);

#define NUM_LEDS 8
#define HOLD_LED NUM_LEDS - 1
#define PREVIEW_COUNT 6

// Analog Pins
#define X_PIN A0
#define Y_PIN A1

// Digital Pins
#define BTN_PIN 5
#define BUZZER_PIN 6
#define RIGHT_BTN_PIN 7
#define LEFT_BTN_PIN 8
#define VIBRO 9
#define LED_PIN 10

// Game Setup
#define UPDATE_PERIOD 500 // ms
#define LOCK_DELAY 500 // ms before piece locks in
#define DAS_INITIAL 117 // ms before repeats kick in
#define DAS_REPEAT 0 // ms between repeated movements

#define BOARD_W 10
#define BOARD_H 20
#define CELL_PX 5
#define BOARD_X 0
#define BOARD_Y 26
#define BOARD_PX_W (BOARD_W * CELL_PX + 2)  // 102
#define BOARD_PX_H (BOARD_H * CELL_PX + 2)  // 52

// Pieces
enum Piece : uint8_t {
  I,
  J,
  L,
  O,
  S,
  T,
  Z,
  NONE
};

enum Rot : uint8_t {
  ZERO, RIGHT, TWO, LEFT
};

// Define the array of leds
CRGB leds[NUM_LEDS];

// {0, R, 2, L}
const uint16_t PIECES[7][4] PROGMEM = {
  {0x0F00, 0x2222, 0x00F0, 0x4444},  // I
  {0x8E00, 0x6440, 0x0E20, 0x44C0},  // J
  {0x2E00, 0x4460, 0x0E80, 0xC440},  // L
  {0x6600, 0x6600, 0x6600, 0x6600},  // O
  {0x6C00, 0x4620, 0x06C0, 0x8C40},  // S
  {0x4E00, 0x4640, 0x0E40, 0x4C40},  // T
  {0xC600, 0x2640, 0x0C60, 0x4C80},  // Z
};

// https://tetris.wiki/Super_Rotation_System
const uint8_t KICKS_CW[2][4][5] PROGMEM = {
  { // JLSTZ
    {0x88, 0x78, 0x79, 0x86, 0x76},  // 0 -> R
    {0x88, 0x98, 0x97, 0x8A, 0x9A},  // R -> 2
    {0x88, 0x98, 0x99, 0x86, 0x96},  // 2 -> L
    {0x88, 0x78, 0x77, 0x8A, 0x7A},  // L -> 0
  },
  { // I
    {0x88, 0x68, 0x98, 0x67, 0x9A},  // 0 -> R
    {0x88, 0x78, 0xA8, 0x7A, 0xA7},  // R -> 2
    {0x88, 0xA8, 0x78, 0xA9, 0x76},  // 2 -> L
    {0x88, 0x98, 0x68, 0x96, 0x69},  // L -> 0
  },
};

// Absolutely no idea why the colors are so wacky
const CRGB PIECE_COLOR[8] = {
  CRGB::Purple,         // I
  CRGB::Blue,           // J
  CHSV(85, 255, 255),   // L
  CRGB::Orange,         // O
  CRGB::Red,            // S
  CHSV(150, 255, 255),  // T
  CHSV(112, 255, 255),  // Z
  CRGB::Black           // None
};

const uint16_t LINE_SCORES[5] PROGMEM = {0, 100, 300, 500, 800};

struct PieceState {
  Piece piece;
  Rot rot;
  uint8_t r;
  uint8_t c;
};

// Binary representation
uint16_t board[20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

PieceState curr;
Piece held = NONE;

uint8_t bag[7];
uint8_t bag_idx = 7;
uint8_t next_bag[7];
bool next_bag_ready = false;

uint32_t score = 0;
bool title_screen = true;
bool game_over = false;

// Timers
uint32_t t_das = 0;
uint32_t t_grav = 0;
uint32_t t_lock = 0; // How long a piece can be touching before locking
uint32_t t_shift = 0; // das timer
bool is_locking = false;

// Input edges
int8_t prev_dir = 0;
bool prev_btn;
bool prev_ccw = false, prev_cw = false;

// Cell (r, c) -> pixel top-left.
static inline int16_t cellX(int8_t c) { return BOARD_X + CELL_PX * c + 1; }
static inline int16_t cellY(int8_t r) { return BOARD_Y + CELL_PX * (BOARD_H - 1 - r) + 1; }

static void shuffleBag(uint8_t *b) {
  for (uint8_t i = 0; i < 7; i++) {
    b[i] = i;
  }
  for (uint8_t i = 6; i > 0; i--) {
    uint8_t j = random(i + 1);
    uint8_t t = b[i];
    b[i] = b[j];
    b[j] = t;
  }
}

static Piece nextPiece() {
  if (bag_idx >= 7) {
    if (next_bag_ready) {
      memcpy(bag, next_bag, 7);
      next_bag_ready = false;
    } else {
      shuffleBag(bag);
    }
    bag_idx = 0;
  }

  return bag[bag_idx++];
}

static Piece queueAt(uint8_t offset) {
  uint8_t idx = bag_idx + offset;
  if (idx < 7) {
    return bag[idx];
  }

  if (!next_bag_ready) {
    shuffleBag(next_bag);
    next_bag_ready = true;
  }

  return next_bag[idx - 7];
}

static bool pieceValid(const PieceState &p) {
  uint16_t bits = pgm_read_word(&PIECES[p.piece][p.rot]);
  for (uint8_t i = 0; i < 16; i++) {
    if (bits & (0x8000 >> i)) {
      int8_t tr = p.r + 3 - (i / 4);
      int8_t tc = p.c + (i % 4);
      if (tr < 0 || tr >= BOARD_H || tc < 0 || tc >= BOARD_W) {
        // Mino oob
        return false;
      }
      if (board[tr] & (1 << (BOARD_W - 1 - tc))) {
        // Other mino in the way
        return false;
      }
    }
  }
  return true;
}
static inline bool pieceValid(Piece p, Rot rot, int8_t r, int8_t c) {
  return pieceValid({p, rot, r, c});
}

// Assuming PieceState is valid
static void lockPiece(const PieceState &p) {
  uint16_t bits = pgm_read_word(&PIECES[p.piece][p.rot]);
  for (uint8_t i = 0; i < 16; i++) {
    if (bits & (0x8000 >> i)) {
      int8_t tr = p.r + 3 - (i / 4);
      int8_t tc = p.c + (i % 4);
      board[tr] |= (1 << (BOARD_W - 1 - tc));
    }
  }
}

static uint8_t clearLines() {
  uint8_t cleared = 0;
  for (int8_t r = 0; r < BOARD_H; r++) {
    if (board[r] == 0x3FF) {
      // Move the entire board down
      for (int8_t dr = r; dr < BOARD_H - 1; dr++) {
        board[dr] = board[dr+1];
      }
      board[BOARD_H - 1] = 0;
      cleared++;
      r--;  // re-test this row, contents shifted down
    }
  }
  return cleared;
}

static bool tryMove(PieceState &p, int8_t dr, int8_t dc) {
  if (!pieceValid(p.piece, p.rot, p.r + dr, p.c + dc)) {
    return false;
  }
  p.r += dr;
  p.c += dc;
  is_locking = false;
  return true;
}

static bool tryRotate(PieceState &p, int8_t dir) {
  Rot to = (p.rot + dir + 4) % 4;

  if (p.piece == O) {
    p.rot = to;
    is_locking = false;
    return true;
  }

  // CW (dir=+1): kicks for from-state.
  // CCW (dir=-1): kicks(X->Y) = -kicks(Y->X), and kicks(Y->X) is CW from Y.
  uint8_t kickRot = (dir > 0) ? p.rot : to;
  int8_t  sign    = (int8_t)dir;
  uint8_t fam     = (p.piece == I) ? 1 : 0;

  for (uint8_t i = 0; i < 5; i++) {
    uint8_t pk = pgm_read_byte(&KICKS_CW[fam][kickRot][i]);

    int8_t  dx = ((pk >> 4) & 0x0F) - 8;
    int8_t  dy =  (pk       & 0x0F) - 8;

    int8_t nr = p.r + sign * dy;
    int8_t nc = p.c + sign * dx;

    if (pieceValid(p.piece, to, nr, nc)) {
      p.r = nr;
      p.c = nc;
      p.rot = to;
      is_locking = false;
      return true;
    }
  }
  return false;
}

static void spawnNew(Piece p) {
  curr = {p, ZERO, 16, 3};
  if (!pieceValid(curr)) {
    game_over = true;
  }
  is_locking = false;
}

static void doHold() {
  Piece prev = held;
  held = curr.piece;
  renderLEDs();
  spawnNew(prev == NONE ? nextPiece() : prev);
}

static void placeRoutine(const PieceState &p) {
  lockPiece(p);
  uint8_t cleared = clearLines();
  if (cleared > 0) {
    score += (uint32_t) pgm_read_word(&LINE_SCORES[cleared]);
    tone(BUZZER_PIN, 700 + 220 * cleared, 70);
    analogWrite(VIBRO, 220);
    delay(50);
    analogWrite(VIBRO, 0);
  } else {
    tone(BUZZER_PIN, 180, 15);
  }
  spawnNew(nextPiece());
  renderLEDs();
  t_grav = millis();
}

static void resetGame() {
  for (uint8_t i = 0; i < 19; i++) {
    board[i] = 0;
  }
  score = 0;
  held = NONE;
  bag_idx = 7;
  next_bag_ready = false;
  is_locking = false;
  prev_dir = 0;
  game_over = false;
  spawnNew(nextPiece());
  renderLEDs();
  t_grav = millis();
}

static void handleInput(uint32_t now) {
  bool btn = !digitalRead(BTN_PIN);
  bool ccw = !digitalRead(LEFT_BTN_PIN);
  bool cw = !digitalRead(RIGHT_BTN_PIN);
  bool btn_edge = btn && !prev_btn;
  bool ccw_edge = ccw && !prev_ccw;
  bool cw_edge = cw && !prev_cw;

  if (title_screen) {
    if (btn_edge || ccw_edge || cw_edge) {
      title_screen = false;
      resetGame();
    }
    prev_btn = btn;
    prev_ccw = ccw;
    prev_cw = cw;
    return;
  }

  if (game_over) {
    if (ccw_edge || cw_edge) {
      resetGame();
    }
    prev_btn = btn;
    prev_ccw = ccw;
    prev_cw = cw;
    return;
  }

  int xv = analogRead(X_PIN);
  int yv = analogRead(Y_PIN);

  int8_t dir = constrain((xv - 512) / 250, -1, 1);

  if (dir != prev_dir) {
    if (dir != 0) {
      tryMove(curr, 0, dir);
      t_das = now;
      t_shift = now;
      prev_dir = dir;
    }
  } else if (dir != 0 && 
             (uint32_t)(now - t_das) >= DAS_INITIAL &&
             (uint32_t)(now - t_shift) >= DAS_REPEAT) {
    tryMove(curr, 0, dir);
    t_shift = now;
  }

  if (btn_edge) {
    doHold();
  }

  if (ccw_edge && tryRotate(curr, -1)) {
    tone(BUZZER_PIN, 380, 12);    
  }
  if (cw_edge && tryRotate(curr, 1)) {
    tone(BUZZER_PIN, 380, 12);
  }

  prev_btn = btn;
  prev_ccw = ccw;
  prev_cw = cw;
}

static void gravity(uint32_t now) {
  if (title_screen || game_over) { return; }

  bool soft = (analogRead(Y_PIN) > 762);
  uint16_t period = UPDATE_PERIOD;

  if (soft) {
    period /= 8;
  }

  if (pieceValid(curr.piece, curr.rot, curr.r - 1, curr.c)) {
    if ((uint32_t)(now - t_grav) >= period) {
      curr.r--;
      t_grav = now;
      if (soft) { score += 1; }
      is_locking = false;
    }
  } else {
    if (!is_locking) {
      is_locking = true;
      t_lock = now;
    } else if ((uint32_t)(now - t_lock) >= LOCK_DELAY) {
      placeRoutine(curr);
    }
  }
}

// The Tetris board has 0,0 as bottom left
// drawRect has origin on top left, so this works out. We can use drawRect with bottom left in the same way.
static void drawMino(int r, int c) {
  // Transform row, column to OLED pixel
  int x = cellX(c), y = cellY(r);
  display.fillRect(x, y, CELL_PX - 1, CELL_PX - 1, SSD1306_WHITE);
}

static void drawBoard() {
  display.drawRect(BOARD_X, BOARD_Y, BOARD_PX_W, BOARD_PX_H, SSD1306_WHITE);

  for (int8_t r = 0; r < BOARD_H; r++) {
    uint16_t row = board[r];
    if (row == 0) { continue; }
    for (int c = 0; c < BOARD_W; c++) {
      if (row & (1 << (BOARD_W - 1 - c))) {
        drawMino(r, c);
      }
    }
  }
}

static void drawPiece(const PieceState &p) {
  uint16_t bits = pgm_read_word(&PIECES[p.piece][p.rot]);
  for (uint8_t i = 0; i < 16; i++) {
    if (bits & (0x8000 >> i)) {
      drawMino(p.r + 3 - (i / 4), p.c + (i % 4));
    }
  }
}

static void drawHUD() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print(score);
}

static void drawTitle() {
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  const char title[] = "TETRIS";
  for (uint8_t i = 0; i < 6; i++) {
    display.setCursor(26, 4 + i * 16);   // x=26 centers a 12-wide size-2 char
    display.print(title[i]);
  }

  display.setTextSize(1);
  display.setCursor(5, 108);             // (64 - 54) / 2 for "Press any"
  display.print(F("Press any"));
  display.setCursor(14, 118);            // (64 - 36) / 2 for "button"
  display.print(F("button"));
}

static void drawGameOver() {
  int16_t bw = 56, bh = 36;
  int16_t bx = (64 - bw) / 2, by = (128 - bh) / 2;
  display.fillRect(bx, by, bw, bh, SSD1306_BLACK);
  display.drawRect(bx, by, bw, bh, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(bx + 1,  by + 4);    // "GAME OVER" = 54px in 56px box
  display.print(F("GAME OVER"));
  display.setCursor(bx + 13, by + 16);   // "Press"
  display.print(F("Press"));
  display.setCursor(bx + 10, by + 24);   // "button"
  display.print(F("button"));
}

static void renderLEDs() {
  for (uint8_t i = 0; i < PREVIEW_COUNT; i++) {
    leds[PREVIEW_COUNT - 1 - i] = PIECE_COLOR[queueAt(i)];
  }
  leds[HOLD_LED] = PIECE_COLOR[held];
  FastLED.show();
}

static void render() {
  display.clearDisplay();

  if (title_screen) {
    drawTitle();
    display.display();
    return;
  }

  drawBoard();
  if (!game_over) { drawPiece(curr); }
  drawHUD();
  if (game_over) { drawGameOver(); }
  display.display();
}


void setup() {
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LEFT_BTN_PIN, INPUT_PULLUP);
  pinMode(RIGHT_BTN_PIN, INPUT_PULLUP);
  pinMode(VIBRO, OUTPUT);

  FastLED.addLeds<WS2812,LED_PIN,RGB>(leds,NUM_LEDS);
  FastLED.setBrightness(20);
  FastLED.show();

  pinMode(X_PIN, INPUT);
  pinMode(Y_PIN, INPUT);

  // Wait for display
  delay(500);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    for(;;);      // Don't proceed, loop forever
  }
  Wire.setClock(400000);          // fast I2C -> ~40 fps render budget
  display.setRotation(1);         // portrait
  display.clearDisplay();
  display.display();

  uint32_t seed = 0;
  for (uint8_t i = 0; i < 16; i++) seed = (seed << 1) ^ analogRead(A5);
  randomSeed(seed);

  render();
}

void loop() {
  uint32_t now = millis();
  handleInput(now);
  gravity(now);
  render();
}