
#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>

/*
 * WAVESHARE ESP32-S3-RLCD-4.2
 * ========================================
 *
 * Chrome-style Dinosaur Game
 *
 * FEATURES:
 * - ST7305 reflective LCD (400x300 landscape)
 * - KEY button input
 * - Variable-height jumping
 * - Moving cactus obstacles
 * - Collision detection
 * - Increasing difficulty
 * - Score and high score
 * - Battery voltage monitoring
 * - Battery percentage and icon
 *
 * LIBRARIES:
 * - Arduino
 * - SPI
 * - U8g2 (ST7305 support)
 *
 * IMPORTANT:
 * After uploading, restart the board
 * normally with BOOT released!
 */


// 1. HARDWARE PIN DEFINITIONS


#define RLCD_SCK   11
#define RLCD_MOSI  12
#define RLCD_DC     5
#define RLCD_CS    40
#define RLCD_RST   41

#define KEY_PIN    18
#define BATTERY_PIN 4


// 2. DISPLAY INITIALIZATION


U8G2_ST7305_300X400_F_4W_HW_SPI display(
  U8G2_R1,
  RLCD_CS,
  RLCD_DC,
  RLCD_RST
);

const int SCREEN_WIDTH = 400;
const int SCREEN_HEIGHT = 300;
const int GROUND_Y = 245;


// 3. DINOSAUR CONFIGURATION


const int DINO_X = 45;
const int DINO_WIDTH = 32;
const int DINO_HEIGHT = 37;

float dinoY = GROUND_Y - DINO_HEIGHT;
float velocityY = 0;

bool jumping = false;
unsigned long jumpStartTime = 0;


// 4. VARIABLE-HEIGHT JUMP PHYSICS


// More negative = stronger initial jump
const float JUMP_FORCE = -10.5f;

// Gravity while rising after max hold
const float NORMAL_GRAVITY = 0.85f;

// Gravity while holding KEY
const float HOLD_GRAVITY = 0.35f;

// Gravity after releasing KEY
const float RELEASE_GRAVITY = 1.25f;

// Minimum jump assistance duration
const unsigned long MIN_HOLD_TIME = 65;

// Maximum jump assistance duration
const unsigned long MAX_HOLD_TIME = 220;


// 5. OBSTACLE CONFIGURATION


struct Cactus {
  float x;
  int width;
  int height;
  bool active;
};

const int MAX_CACTI = 3;
Cactus obstacles[MAX_CACTI];


// 6. GAME STATE


bool gameOver = false;
bool started = false;
bool previousKey = false;

int score = 0;
int highScore = 0;

float gameSpeed = 4.0f;

unsigned long lastFrame = 0;
unsigned long lastSpawn = 0;

// Approximately 25 frames per second
const unsigned long FRAME_INTERVAL = 40;


// 7. BATTERY MONITORING


float batteryVoltage = 0.0f;
int batteryPercent = 0;

unsigned long lastBatteryRead = 0;
bool batteryInitialized = false;

const unsigned long BATTERY_INTERVAL = 10000;

// Approximate Li-ion voltage limits
const float BATTERY_EMPTY_V = 2.5f;
const float BATTERY_FULL_V = 4.2f;

// Waveshare battery voltage divider
const float BATTERY_DIVIDER = 3.0f;


// 8. READ BATTERY VOLTAGE


void updateBattery() {
  unsigned long now = millis();

  if (batteryInitialized &&
      now - lastBatteryRead < BATTERY_INTERVAL) {
    return;
  }

  lastBatteryRead = now;
  batteryInitialized = true;

  uint32_t totalMillivolts = 0;

  // Average 16 ADC measurements
  for (int i = 0; i < 16; i++) {
    totalMillivolts +=
      analogReadMilliVolts(BATTERY_PIN);

    delayMicroseconds(200);
  }

  float measuredMillivolts =
    totalMillivolts / 16.0f;

  // Recover actual battery voltage
  batteryVoltage =
    measuredMillivolts *
    BATTERY_DIVIDER / 1000.0f;

  // Approximate percentage
  float percentage =
    (batteryVoltage - BATTERY_EMPTY_V) /
    (BATTERY_FULL_V - BATTERY_EMPTY_V) *
    100.0f;

  batteryPercent = constrain(
    (int)(percentage + 0.5f),
    0,
    100
  );

  Serial.printf(
    "Battery: %.2f V | %d%%\n",
    batteryVoltage,
    batteryPercent
  );
}


// 9. DRAW BATTERY ICON


void drawBattery(int x, int y) {
  const int width = 32;
  const int height = 16;

  // Battery outline
  display.drawFrame(
    x,
    y,
    width,
    height
  );

  // Battery positive terminal
  display.drawBox(
    x + width,
    y + 5,
    4,
    6
  );

  // Calculate fill width
  int fillWidth =
    batteryPercent * (width - 4) / 100;

  // Draw battery charge
  if (fillWidth > 0) {
    display.drawBox(
      x + 2,
      y + 2,
      fillWidth,
      height - 4
    );
  }

  // Percentage
  display.setFont(u8g2_font_6x12_tr);

  display.setCursor(
    x + 43,
    y + 12
  );

  display.print(batteryPercent);
  display.print("%");
}


// 10. DRAW PIXEL DINOSAUR


void drawDino(int x, int y, bool running) {

  // Head
  display.drawBox(
    x + 15, y, 17, 14
  );

  // Eye
  display.setDrawColor(0);
  display.drawPixel(x + 27, y + 4);
  display.setDrawColor(1);

  // Jaw
  display.drawBox(
    x + 19, y + 12, 10, 5
  );

  // Neck
  display.drawBox(
    x + 12, y + 12, 12, 16
  );

  // Body
  display.drawBox(
    x + 7, y + 19, 19, 10
  );

  // Tail
  display.drawBox(
    x + 3, y + 23, 10, 5
  );

  display.drawBox(
    x, y + 18, 5, 8
  );

  // Arm
  display.drawBox(
    x + 23, y + 20, 7, 3
  );

  display.drawBox(
    x + 27, y + 20, 3, 6
  );

  // Running leg animation
  if (running) {
    display.drawBox(
      x + 10, y + 28, 5, 9
    );

    display.drawBox(
      x + 21, y + 28, 5, 5
    );
  } else {
    display.drawBox(
      x + 10, y + 28, 5, 5
    );

    display.drawBox(
      x + 21, y + 28, 5, 9
    );
  }
}


// 11. DRAW CACTUS


void drawCactus(int x, int height, int width) {

  int y = GROUND_Y - height;
  int trunkX = x + width / 2 - 4;

  // Main trunk
  display.drawBox(
    trunkX, y, 8, height
  );

  // Left branch
  display.drawBox(
    trunkX - 8, y + 12, 8, 5
  );

  display.drawBox(
    trunkX - 8, y + 4, 5, 13
  );

  // Right branch
  display.drawBox(
    trunkX + 8, y + 19, 8, 5
  );

  display.drawBox(
    trunkX + 12, y + 10, 4, 14
  );
}


// 12. RESET GAME


void resetGame() {

  dinoY = GROUND_Y - DINO_HEIGHT;
  velocityY = 0;

  jumping = false;
  jumpStartTime = 0;

  score = 0;
  gameSpeed = 4.0f;

  gameOver = false;
  started = false;

  lastSpawn = millis();

  for (int i = 0; i < MAX_CACTI; i++) {
    obstacles[i].active = false;
  }
}


// 13. SPAWN CACTUS


void spawnCactus() {

  for (int i = 0; i < MAX_CACTI; i++) {

    if (!obstacles[i].active) {

      obstacles[i].x =
        SCREEN_WIDTH + 10;

      obstacles[i].height =
        random(25, 45);

      obstacles[i].width = 24;
      obstacles[i].active = true;

      break;
    }
  }
}


// 14. COLLISION DETECTION


bool checkCollision(Cactus &c) {

  // Slightly smaller hitbox
  int dx = DINO_X + 5;
  int dy = (int)dinoY + 4;

  int dw = DINO_WIDTH - 10;
  int dh = DINO_HEIGHT - 5;

  int cx = (int)c.x + 4;
  int cy = GROUND_Y - c.height;

  int cw = c.width - 8;
  int ch = c.height;

  return (
    dx < cx + cw &&
    dx + dw > cx &&
    dy < cy + ch &&
    dy + dh > cy
  );
}


// 15. START JUMP


void startJump(unsigned long now) {

  if (jumping) {
    return;
  }

  jumping = true;

  jumpStartTime = now;
  velocityY = JUMP_FORCE;
}


// 16. UPDATE VARIABLE-HEIGHT JUMP


void updateJump(bool keyHeld, unsigned long now) {

  if (!jumping) {
    return;
  }

  unsigned long heldTime =
    now - jumpStartTime;

  float gravity = NORMAL_GRAVITY;

  // Apply variable-height jump physics
  if (velocityY < 0) {

    if (heldTime < MIN_HOLD_TIME) {
      gravity = HOLD_GRAVITY;
    }

    else if (
      keyHeld &&
      heldTime < MAX_HOLD_TIME
    ) {
      gravity = HOLD_GRAVITY;
    }

    else {
      gravity = RELEASE_GRAVITY;
    }
  }

  // Apply physics
  velocityY += gravity;
  dinoY += velocityY;

  // Ground collision
  if (dinoY >= GROUND_Y - DINO_HEIGHT) {

    dinoY = GROUND_Y - DINO_HEIGHT;
    velocityY = 0;
    jumping = false;
  }
}


// 17. DRAW COMPLETE GAME SCREEN

void drawGame() {

  display.clearBuffer();
  display.setDrawColor(1);

  // Header title
  display.setFont(u8g2_font_6x12_tr);
  display.drawStr(
    10, 20,
    "DINO RUN"
  );

  // High score
  display.setCursor(165, 20);
  display.print("HI ");
  display.print(highScore);

  // Battery icon and percentage
  drawBattery(285, 8);

  // Current score
  display.setCursor(315, 38);
  display.print(score);

  // Ground line
  display.drawHLine(
    0,
    GROUND_Y,
    SCREEN_WIDTH
  );

  // Moving ground decoration
  for (int i = 0; i < 15; i++) {

    int x = (
      i * 37 -
      (score * 2) % 37 +
      SCREEN_WIDTH
    ) % SCREEN_WIDTH;

    display.drawHLine(
      x,
      GROUND_Y + 8 + i % 3,
      9
    );
  }

  // Running animation
  bool running =
    ((millis() / 130) % 2) == 0;

  if (jumping) {
    running = false;
  }

  drawDino(
    DINO_X,
    (int)dinoY,
    running
  );

  // Draw obstacles
  for (int i = 0; i < MAX_CACTI; i++) {

    if (obstacles[i].active) {

      drawCactus(
        (int)obstacles[i].x,
        obstacles[i].height,
        obstacles[i].width
      );
    }
  }

  // Start screen
  if (!started && !gameOver) {

    display.setFont(
      u8g2_font_helvB14_tr
    );

    display.drawStr(
      105, 110,
      "DINO RUN"
    );

    display.setFont(
      u8g2_font_6x12_tr
    );

    display.drawStr(
      115, 140,
      "PRESS KEY TO START"
    );
  }

  // Game over screen
  if (gameOver) {

    display.setFont(
      u8g2_font_helvB18_tr
    );

    display.drawStr(
      105, 105,
      "GAME OVER"
    );

    display.setFont(
      u8g2_font_6x12_tr
    );

    display.drawStr(
      125, 135,
      "PRESS KEY TO RETRY"
    );
  }

  // Footer
  display.setFont(
    u8g2_font_6x12_tr
  );

  display.drawStr(
    10, 290,
    "HOLD KEY = HIGHER JUMP"
  );

  // Transfer framebuffer to LCD
  display.sendBuffer();
}


// 18. SETUP


void setup() {

  Serial.begin(115200);

  // KEY button
  pinMode(
    KEY_PIN,
    INPUT_PULLUP
  );

  // Battery ADC
  analogReadResolution(12);

  analogSetPinAttenuation(
    BATTERY_PIN,
    ADC_11db
  );

  // SPI bus
  SPI.begin(
    RLCD_SCK,
    -1,
    RLCD_MOSI,
    RLCD_CS
  );

  // Initialize display
  display.begin();

  // SPI clock
  display.setBusClock(4000000);

  // Random seed
  randomSeed(micros());

  // First battery measurement
  updateBattery();

  // Initialize game
  resetGame();

  // Draw initial screen
  drawGame();

  Serial.println(
    "================================"
  );

  Serial.println(
    "WAVESHARE ESP32-S3 RLCD 4.2"
  );

  Serial.println(
    "DINO GAME INITIALIZED!"
  );

  Serial.println(
    "================================"
  );
}


// 19. MAIN LOOP


void loop() {

  unsigned long now = millis();

  // Periodic battery monitoring
  updateBattery();

  // Frame rate limiter
  if (now - lastFrame < FRAME_INTERVAL) {
    return;
  }

  lastFrame = now;

  // Read KEY button
  bool key =
    digitalRead(KEY_PIN) == LOW;

  // Detect a new button press
  bool pressed =
    key && !previousKey;

  previousKey = key;

 
  // GAME OVER
 

  if (gameOver) {

    if (pressed) {
      resetGame();
      started = true;
      startJump(now);
    }

    drawGame();
    return;
  }

  // WAITING TO START


  if (!started) {

    if (pressed) {
      started = true;
      startJump(now);
    }

    drawGame();
    return;
  }


  // JUMP INPUT


  if (pressed && !jumping) {
    startJump(now);
  }

  // Update jump physics
  updateJump(key, now);

 
  // DIFFICULTY


  gameSpeed =
    4.0f + score / 250.0f;

  if (gameSpeed > 9.0f) {
    gameSpeed = 9.0f;
  }


  // OBSTACLE SPAWNING
 

  unsigned long spawnInterval =
    (unsigned long)(
      1500 - gameSpeed * 75
    );

  if (now - lastSpawn > spawnInterval) {

    spawnCactus();
    lastSpawn = now;
  }

  // OBSTACLE MOVEMENT + COLLISION
  

  for (int i = 0; i < MAX_CACTI; i++) {

    if (!obstacles[i].active) {
      continue;
    }

    // Move cactus left
    obstacles[i].x -= gameSpeed;

    // Remove off-screen cactus
    if (obstacles[i].x < -30) {
      obstacles[i].active = false;
    }

    // Collision detection
    if (
      obstacles[i].active &&
      checkCollision(obstacles[i])
    ) {

      gameOver = true;

      if (score > highScore) {
        highScore = score;
      }

      Serial.println("GAME OVER!");
    }
  }


  // SCORE


  if (!gameOver) {
    score++;
  }

  if (score > highScore) {
    highScore = score;
  }


  // RENDER
 
  drawGame();
}
