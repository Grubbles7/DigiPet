#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include "time.h"
#include "config.h"
#include "pet.h"
#include "sprites.h"
#include "weather.h"
#include "minigame.h"
#include "snake.h"
#include "catchgame.h"
#include "healgame.h"

TFT_eSPI tft = TFT_eSPI();

#define ENC_A   25
#define ENC_B   26
#define BTN1    13
#define BTN2    12
#define BTN3    15

#define HEADER_H   28
#define CLOCK_Y    33
#define DATE_Y     58
#define DIV_TOP    75
#define PET_ZONE_Y 76
#define PET_ZONE_H 68
#define DIV_BOT    144
#define STATS_Y    145

#define STAT_TICK_MS     3600000UL
//#define STAT_TICK_MS   60000UL
#define SAVE_INTERVAL_MS 60000UL

#define SPR_SCALE  4
#define SPR_SIZE   (16 * SPR_SCALE)
#define SPR_LEFT   ((135 - SPR_SIZE) / 2)

#define SCREEN_HOME      0
#define SCREEN_CARE      1
#define SCREEN_STATUS    2
#define SCREEN_MINIGAMES 3
#define SCREEN_SETTINGS  4
#define NUM_TOP_SCREENS  5

#define CARE_FEED  0
#define CARE_PLAY  1
#define CARE_TRAIN 2
#define CARE_HEAL  3
#define NUM_CARE_ITEMS 4

uint8_t topScreen   = SCREEN_HOME;
uint8_t careItem    = CARE_FEED;
bool    inCare      = false;
bool    inConfirm   = false;
bool    screenDirty = true;

volatile int  encDelta = 0;
volatile bool encMoved = false;

void IRAM_ATTR onEncoderA() {
  bool a = digitalRead(ENC_A);
  bool b = digitalRead(ENC_B);
  int d = encDelta;
  if (a == b) encDelta = d + 1;
  else        encDelta = d - 1;
  encMoved = true;
}

struct Button { uint8_t pin; bool lastState; unsigned long lastMs; };
Button btn1 = { BTN1, HIGH, 0 };
Button btn2 = { BTN2, HIGH, 0 };
Button btn3 = { BTN3, HIGH, 0 };

bool btnPressed(Button &b) {
  bool state = digitalRead(b.pin);
  if (state == LOW && b.lastState == HIGH && millis() - b.lastMs > 40) {
    b.lastState = LOW; b.lastMs = millis(); return true;
  }
  if (state == HIGH) b.lastState = HIGH;
  return false;
}

PetStats pet;
unsigned long lastSaveMs = 0;
unsigned long lastStatMs = 0;
uint8_t lastDrawnStage   = 255;
bool statsNeedRedraw     = false;

#define DEATH_HOLD_MS  3000UL
unsigned long btn1HoldStart = 0;
bool          btn1Holding   = false;

const int8_t BOB_OFFSETS[] = { 0, -1, -2, -1 };
#define BOB_FRAMES         4
#define BOB_FRAME_MS       250
#define BLINK_INTERVAL_MS  4000
#define BLINK_DURATION_MS  120

uint8_t       animBobFrame = 0;
bool          animBlinking = false;
unsigned long animLastMs   = 0;
unsigned long blinkLastMs  = 0;

// EyeRegion, StageEyes, STAGE_EYES, EYE_CLOSE_COL defined in sprites.h

uint16_t statColour(uint8_t val, uint16_t normalCol) {
  if (val <= 15) return TFT_RED;
  if (val <= 30) return TFT_ORANGE;
  return normalCol;
}

bool isHomeVisible() {
  return (!inCare && !inConfirm && topScreen == SCREEN_HOME);
}

int spriteTopY() {
  int base = PET_ZONE_Y + (PET_ZONE_H - SPR_SIZE) / 2 - 7;
  return base + BOB_OFFSETS[animBobFrame];
}

void redrawPetZone() {
  drawWeatherBackground(tft, currentWeather);
  drawStageSpriteInZone(tft, pet.stage, 0, PET_ZONE_Y, 135, PET_ZONE_H,
                        -7 + BOB_OFFSETS[animBobFrame]);
  lastDrawnStage = pet.stage;
  if (pet.hunger < 20 || pet.health < 20) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(1); tft.setCursor(120, PET_ZONE_Y + 4); tft.print("!");
  }
}

void drawEyesClosed() {
  if (pet.stage >= 5) return;
  const StageEyes &se = STAGE_EYES[pet.stage];
  if (!se.hasEyes) return;
  int baseY = spriteTopY();
  tft.fillRect(SPR_LEFT + se.left.col  * SPR_SCALE, baseY + se.left.row  * SPR_SCALE, se.left.w  * SPR_SCALE, se.left.h  * SPR_SCALE, EYE_CLOSE_COL);
  tft.fillRect(SPR_LEFT + se.right.col * SPR_SCALE, baseY + se.right.row * SPR_SCALE, se.right.w * SPR_SCALE, se.right.h * SPR_SCALE, EYE_CLOSE_COL);
}

void animTick() {
  unsigned long now = millis();
  if (now - animLastMs >= BOB_FRAME_MS) {
    animLastMs = now; animBobFrame = (animBobFrame + 1) % BOB_FRAMES;
    if (isHomeVisible()) redrawPetZone();
  }
  if (!animBlinking && now - blinkLastMs >= BLINK_INTERVAL_MS) {
    animBlinking = true; blinkLastMs = now;
    if (isHomeVisible() && STAGE_EYES[pet.stage].hasEyes) drawEyesClosed();
  }
  if (animBlinking && now - blinkLastMs >= BLINK_DURATION_MS) {
    animBlinking = false;
    if (isHomeVisible()) redrawPetZone();
  }
}

void drawDeathScreen(float holdProgress = 0.0f) {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 135, HEADER_H, 0x2104);
  tft.setTextColor(TFT_DARKGREY, 0x2104); tft.setTextSize(2); tft.setCursor(28, 6); tft.print("R . I . P");
  tft.drawFastHLine(0, HEADER_H, 135, 0x2945);
  int sx = 42, sy = PET_ZONE_Y + 4, sw = 50, sh = 58;
  tft.fillRoundRect(sx, sy, sw, sh, 6, 0x3186); tft.drawRoundRect(sx, sy, sw, sh, 6, 0x5ACB);
  tft.fillCircle(sx+sw/2, sy+14, 20, 0x3186); tft.drawCircle(sx+sw/2, sy+14, 20, 0x5ACB);
  tft.fillRect(sx, sy+14, sw, 6, 0x3186);
  tft.drawFastVLine(sx, sy+14, 6, 0x5ACB); tft.drawFastVLine(sx+sw-1, sy+14, 6, 0x5ACB);
  tft.setTextSize(1); tft.setTextColor(0x5ACB, 0x3186); tft.setCursor(sx+9, sy+8); tft.print("x  x");
  tft.setTextColor(TFT_WHITE, 0x3186); tft.setCursor(sx+6, sy+22); tft.print(stageName(pet.deathStage));
  tft.setTextColor(0x8C71, 0x3186); tft.setCursor(sx+4, sy+34);
  char ageBuf[10]; snprintf(ageBuf, sizeof(ageBuf), "Age %dh", pet.age); tft.print(ageBuf);
  tft.drawFastHLine(20, PET_ZONE_Y+PET_ZONE_H-4, 94, 0x2945);
  tft.drawFastHLine(0, DIV_BOT, 135, 0x2945);
  tft.setTextSize(1);
  tft.setTextColor(TFT_RED, TFT_BLACK); tft.setCursor(8, STATS_Y+4);
  tft.print(pet.deathCause == 0 ? "Cause: Starvation" : "Cause: Neglect");
  tft.setTextColor(0x4208, TFT_BLACK); tft.setCursor(8, STATS_Y+18); tft.print("Left without care...");
  tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.setCursor(8, STATS_Y+48); tft.print("Hold BTN1 to restart");
  tft.fillRect(8, STATS_Y+60, 119, 8, 0x1082);
  if (holdProgress > 0.0f) tft.fillRect(8, STATS_Y+60, (int)(holdProgress*119.0f), 8, TFT_CYAN);
}

void drawHeader(const char* title, uint16_t bg, bool showDots = false) {
  tft.fillRect(0, 0, 135, HEADER_H, bg);
  tft.setTextColor(TFT_WHITE, bg); tft.setTextSize(2); tft.setCursor(8, 6); tft.print(title);
  if (showDots) for (int i=0;i<NUM_TOP_SCREENS;i++) tft.fillRect(88+i*9,12,5,5,(i==topScreen)?TFT_CYAN:0x2945u);
  tft.drawFastHLine(0, HEADER_H, 135, TFT_DARKGREY);
}

void drawHomeScreen() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("DigiEDC", TFT_DARKGREEN, true);
  struct tm t;
  if (getLocalTime(&t)) {
    char timeBuf[9]; strftime(timeBuf,sizeof(timeBuf),"%H:%M:%S",&t);
    tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextSize(3); tft.setCursor(4,CLOCK_Y); tft.print(timeBuf);
    char dateBuf[16]; strftime(dateBuf,sizeof(dateBuf),"%a %d %b %Y",&t);
    tft.setTextColor(TFT_CYAN,TFT_BLACK); tft.setTextSize(1); tft.setCursor(8,DATE_Y); tft.print(dateBuf);
  } else {
    tft.setTextColor(TFT_DARKGREY,TFT_BLACK); tft.setTextSize(3); tft.setCursor(4,CLOCK_Y); tft.print("--:--:--");
    tft.setTextSize(1); tft.setCursor(8,DATE_Y); tft.print("No time synced  ");
  }
  tft.drawFastHLine(0,DIV_TOP,135,TFT_DARKGREY);
  redrawPetZone();
  tft.drawFastHLine(0,DIV_BOT,135,TFT_DARKGREY);
  tft.setTextSize(1); tft.setTextColor(TFT_YELLOW,TFT_BLACK); tft.setCursor(8,STATS_Y+2);
  tft.print(stageName(pet.stage)); tft.print("  Age:"); tft.print(pet.age); tft.print("h");
  struct { const char* label; uint8_t val; uint16_t col; int y; } bars[] = {
    {"HUN",pet.hunger,TFT_ORANGE,STATS_Y+14},{"HAP",pet.happiness,TFT_YELLOW,STATS_Y+24},
    {"TRN",pet.training,TFT_CYAN,STATS_Y+34},{"HP",pet.health,TFT_GREEN,STATS_Y+44},
  };
  for (auto &b : bars) {
    tft.setTextColor(TFT_DARKGREY,TFT_BLACK); tft.setCursor(8,b.y); tft.print(b.label);
    tft.fillRect(28,b.y,98,7,0x1082);
    tft.fillRect(28,b.y,map(b.val,0,100,0,98),7,statColour(b.val,b.col));
  }
}

struct CareOption { const char* label; const char* desc; uint16_t col; };
const CareOption CARE_OPTIONS[] = {
  {"Feed",  "+25 Hunger",    TFT_ORANGE},
  {"Play",  "+20 Happiness", TFT_YELLOW},
  {"Train", "+15 Training",  TFT_CYAN  },
  {"Heal",  "+30 Health",    TFT_GREEN },
};

void drawCareScreen() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("Care", 0x5800, true);
  tft.setTextSize(1); tft.setTextColor(0x4208,TFT_BLACK); tft.setCursor(8,34); tft.print("rotate:scroll  btn1:select");
  for (int i=0;i<NUM_CARE_ITEMS;i++) {
    bool sel=(i==careItem); int y=46+i*46; uint16_t col=CARE_OPTIONS[i].col;
    if (sel) { tft.fillRect(3,y,3,38,col); tft.fillRect(6,y,129,38,0x0821); }
    else       tft.fillRect(0,y,135,38,TFT_BLACK);
    tft.setTextSize(2); tft.setTextColor(sel?col:0x4208u,sel?0x0821u:(uint16_t)TFT_BLACK);
    tft.setCursor(14,y+6); tft.print(CARE_OPTIONS[i].label);
    tft.setTextSize(1); tft.setTextColor(sel?TFT_WHITE:0x2945u,sel?0x0821u:(uint16_t)TFT_BLACK);
    tft.setCursor(14,y+26); tft.print(CARE_OPTIONS[i].desc);
    if (sel) { tft.setTextColor(col,0x0821); tft.setCursor(118,y+14); tft.print(">"); }
  }
}

void drawConfirmScreen() {
  const CareOption &opt=CARE_OPTIONS[careItem];
  tft.fillScreen(TFT_BLACK); drawHeader(opt.label,0x0821,false);
  tft.setTextSize(3); tft.setTextColor(opt.col,TFT_BLACK); tft.setCursor(8,42); tft.print(opt.label);
  tft.setTextSize(1); tft.setTextColor(0x4208,TFT_BLACK); tft.setCursor(8,82); tft.print("Effect:");
  uint8_t vals[]={pet.hunger,pet.happiness,pet.training,pet.health};
  uint8_t deltas[]={0,0,0,0};
  uint16_t cols[]={TFT_ORANGE,TFT_YELLOW,TFT_CYAN,TFT_GREEN};
  const char* lbls[]={"HUN","HAP","TRN","HP"};
  switch (careItem) { case CARE_FEED:deltas[0]=25;break; case CARE_PLAY:deltas[1]=20;break; case CARE_TRAIN:deltas[2]=15;break; case CARE_HEAL:deltas[3]=30;break; }
  for (int i=0;i<4;i++) {
    int y=94+i*22; bool changed=deltas[i]>0;
    tft.setTextColor(changed?cols[i]:0x2945u,TFT_BLACK); tft.setCursor(8,y); tft.print(lbls[i]);
    tft.fillRect(28,y,78,7,0x1082); tft.fillRect(28,y,map(vals[i],0,100,0,78),7,changed?0x2945u:(uint16_t)0x1082);
    if (changed) { tft.fillRect(28,y,map(min(100,(int)vals[i]+deltas[i]),0,100,0,78),7,cols[i]); tft.setTextColor(cols[i],TFT_BLACK); tft.setCursor(110,y); tft.print("+"); tft.print(deltas[i]); }
  }
  tft.setTextColor(opt.col,TFT_BLACK); tft.setCursor(8,196); tft.print("BTN1: Confirm");
  tft.setTextColor(0x4208,TFT_BLACK); tft.setCursor(8,210); tft.print("BTN2: Cancel");
}

void drawStatusScreen() {
  tft.fillScreen(TFT_BLACK); drawHeader("Status",0x000F,true);
  tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY,TFT_BLACK); tft.setCursor(8,36);
  tft.print(stageName(pet.stage)); tft.print("  Stage "); tft.print(pet.stage+1); tft.print("/5  Age:"); tft.print(pet.age); tft.print("h");
  struct { const char* label; uint8_t val; uint16_t col; } rows[]={{"Hunger",pet.hunger,TFT_ORANGE},{"Happiness",pet.happiness,TFT_YELLOW},{"Training",pet.training,TFT_CYAN},{"HP",pet.health,TFT_GREEN}};
  for (int i=0;i<4;i++) {
    int y=52+i*46; uint16_t col=statColour(rows[i].val,rows[i].col);
    tft.setTextColor(col,TFT_BLACK); tft.setTextSize(1); tft.setCursor(8,y); tft.print(rows[i].label);
    tft.setTextColor(TFT_DARKGREY,TFT_BLACK); tft.setCursor(100,y); tft.print(rows[i].val); tft.print("/100");
    tft.fillRect(8,y+12,119,9,0x1082); tft.fillRect(8,y+12,map(rows[i].val,0,100,0,119),9,col);
  }
}

void drawMinigamesScreen() {
  tft.fillScreen(TFT_BLACK); drawHeader("Minigames",0x3009,true);
  tft.setTextSize(2); tft.setTextColor(0x4208,TFT_BLACK); tft.setCursor(44,110); tft.print("?");
  tft.setTextSize(1); tft.setTextColor(0x2945,TFT_BLACK); tft.setCursor(8,140); tft.print("Unlocks at Baby stage");
}

void drawSettingsScreen() {
  tft.fillScreen(TFT_BLACK); drawHeader("Settings",0x2104,true);
  tft.setTextSize(1); tft.setTextColor(0x2945,TFT_BLACK); tft.setCursor(8,80); tft.print("Coming soon");
}

void redrawScreen() {
  if      (inConfirm) drawConfirmScreen();
  else if (inCare)    drawCareScreen();
  else switch (topScreen) {
    case SCREEN_HOME:      drawHomeScreen();      break;
    case SCREEN_CARE:      drawCareScreen();      break;
    case SCREEN_STATUS:    drawStatusScreen();    break;
    case SCREEN_MINIGAMES: drawMinigamesScreen(); break;
    case SCREEN_SETTINGS:  drawSettingsScreen();  break;
  }
  screenDirty = false;
}

void flashFeedback(const char* msg, uint16_t col) {
  tft.fillRect(10,100,115,30,TFT_BLACK); tft.drawRect(10,100,115,30,col);
  tft.setTextColor(col,TFT_BLACK); tft.setTextSize(1); tft.setCursor(16,112); tft.print(msg);
  delay(800); screenDirty = true;
}

void applySelectedCareAction() {
  switch (careItem) {
    case CARE_FEED:  pet.hunger    = min(100,(int)pet.hunger    +25); flashFeedback("Fed! +25 Hunger",  TFT_ORANGE); break;
    case CARE_PLAY:  pet.happiness = min(100,(int)pet.happiness +20); flashFeedback("Play! +20 Happy",  TFT_YELLOW); break;
    case CARE_TRAIN: pet.training  = min(100,(int)pet.training  +15); flashFeedback("Train! +15 Train", TFT_CYAN);   break;
    case CARE_HEAL:  pet.health    = min(100,(int)pet.health    +30); flashFeedback("Heal! +30 HP",     TFT_GREEN);  break;
  }
  statsNeedRedraw = true;
}

void tickStats() {
  pet.hunger    = (pet.hunger    >= 3) ? pet.hunger    - 3 : 0;
  pet.happiness = (pet.happiness >= 2) ? pet.happiness - 2 : 0;
  if (pet.hunger    < 15) pet.health = (pet.health >= 5) ? pet.health - 5 : 0;
  if (pet.happiness < 15) pet.health = (pet.health >= 2) ? pet.health - 2 : 0;
  pet.age += 1;
  Serial.printf("Tick — HUN:%d HAP:%d TRN:%d HP:%d Age:%dh\n", pet.hunger, pet.happiness, pet.training, pet.health, pet.age);
  if (pet.health == 0 && !pet.dead) {
    uint8_t cause = (pet.hunger == 0) ? 0 : 1;
    killPet(pet, cause); Serial.printf("Pet died! Cause:%d Stage:%d Age:%dh\n", cause, pet.deathStage, pet.age);
    savePet(pet); screenDirty = true; return;
  }
  if (canEvolve(pet)) {
    uint8_t old = pet.stage; evolvePet(pet); Serial.printf("Evolved! %d -> %d\n", old, pet.stage);
    tft.fillRect(0,0,135,HEADER_H,0x4010); tft.setTextColor(TFT_MAGENTA,0x4010); tft.setTextSize(1); tft.setCursor(8,10); tft.print("** EVOLVING! **"); delay(1200);
  }
  statsNeedRedraw = true;
}

// doWeatherUpdate() is defined in weather.h

void setup() {
  Serial.begin(115200);
  pinMode(4, OUTPUT); digitalWrite(4, HIGH);
  pinMode(ENC_A, INPUT_PULLUP); pinMode(ENC_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_A), onEncoderA, CHANGE);
  pinMode(BTN1, INPUT_PULLUP); pinMode(BTN2, INPUT_PULLUP); pinMode(BTN3, INPUT_PULLUP);
  tft.init(); tft.setRotation(0);
  pet = loadPet(); runMigrationIfNeeded(pet);
  currentWeather = loadWeatherCache();
  Serial.printf("Pet loaded — Stage:%d HUN:%d HAP:%d HP:%d Age:%dh\n", pet.stage, pet.hunger, pet.happiness, pet.health, pet.age);
  redrawScreen(); doWeatherUpdate();
  time_t now; time(&now); pet.lastSave=(uint32_t)now; savePet(pet);
  lastSaveMs=millis(); lastStatMs=millis(); animLastMs=millis(); blinkLastMs=millis();
}

void loop() {
  if (pet.dead) {
    if (screenDirty) { drawDeathScreen(0.0f); screenDirty=false; }
    bool b1Down=(digitalRead(BTN1)==LOW);
    if (b1Down && !btn1Holding) { btn1Holding=true; btn1HoldStart=millis(); }
    if (b1Down && btn1Holding) {
      unsigned long held=millis()-btn1HoldStart;
      float progress=(float)held/(float)DEATH_HOLD_MS; if (progress>1.0f) progress=1.0f;
      tft.fillRect(8,STATS_Y+60,119,8,0x1082); tft.fillRect(8,STATS_Y+60,(int)(progress*119.0f),8,TFT_CYAN);
      if (held>=DEATH_HOLD_MS) {
        resetPet(); pet=defaultPet(); savePet(pet); btn1Holding=false;
        topScreen=SCREEN_HOME; inCare=false; inConfirm=false; screenDirty=true;
        lastStatMs=millis(); lastSaveMs=millis(); animLastMs=millis(); blinkLastMs=millis();
      }
    }
    if (!b1Down && btn1Holding) { btn1Holding=false; tft.fillRect(8,STATS_Y+60,119,8,0x1082); }
    delay(20); return;
  }

  if (encMoved) {
    encMoved=false; int dir=(encDelta>0)?1:-1; encDelta=0;
    if (!inConfirm) {
      if (inCare) { careItem=(careItem+dir+NUM_CARE_ITEMS)%NUM_CARE_ITEMS; screenDirty=true; }
      else        { topScreen=(topScreen+dir+NUM_TOP_SCREENS)%NUM_TOP_SCREENS; screenDirty=true; }
    }
  }
  if (btnPressed(btn1)) {
    if (inConfirm) {
      if (careItem==CARE_TRAIN) {
        inConfirm=false; inCare=false; screenDirty=false;
        uint8_t e=runTrainingMinigame(tft,BTN1,BTN2);
        if (e>0) { pet.training=min(100,(int)pet.training+e); char m[20]; snprintf(m,20,"Train! +%d",e); flashFeedback(m,TFT_CYAN); statsNeedRedraw=true; }
        inCare=true; screenDirty=true;
      } else if (careItem==CARE_FEED) {
        inConfirm=false; inCare=false; screenDirty=false;
        uint8_t e=runCatchMinigame(tft,BTN1,BTN2,encDelta);
        if (e>0) { pet.hunger=min(100,(int)pet.hunger+e); char m[22]; snprintf(m,22,"Fed! +%d Hunger",e); flashFeedback(m,TFT_ORANGE); statsNeedRedraw=true; }
        inCare=true; screenDirty=true;
      } else if (careItem==CARE_PLAY) {
        inConfirm=false; inCare=false; screenDirty=false;
        uint8_t e=runSnakeMinigame(tft,BTN1,BTN2,encDelta);
        if (e>0) { pet.happiness=min(100,(int)pet.happiness+e); char m[22]; snprintf(m,22,"Play! +%d Happy",e); flashFeedback(m,TFT_YELLOW); statsNeedRedraw=true; }
        inCare=true; screenDirty=true;
      } else if (careItem==CARE_HEAL) {
        inConfirm=false; inCare=false; screenDirty=false;
        uint8_t e=runHealMinigame(tft,BTN1,BTN2);
        if (e>0) { pet.health=min(100,(int)pet.health+e); char m[20]; snprintf(m,20,"Heal! +%d HP",e); flashFeedback(m,TFT_GREEN); statsNeedRedraw=true; }
        inCare=true; screenDirty=true;
      } else { applySelectedCareAction(); inConfirm=false; inCare=true; screenDirty=true; }
    }
    else if (inCare)                   { inConfirm=true; screenDirty=true; }
    else if (topScreen==SCREEN_CARE)   { inCare=true;    screenDirty=true; }
  }
  if (btnPressed(btn2)) {
    if      (inConfirm) { inConfirm=false; screenDirty=true; }
    else if (inCare)    { inCare=false;    screenDirty=true; }
  }
  if (btnPressed(btn3)) { Serial.println("BTN3 pressed (TBD)"); }

  if (isHomeVisible() && !screenDirty) animTick();
  if (isHomeVisible() && !screenDirty) {
    struct tm t;
    if (getLocalTime(&t)) {
      char timeBuf[9]; strftime(timeBuf,sizeof(timeBuf),"%H:%M:%S",&t);
      tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextSize(3); tft.setCursor(4,CLOCK_Y); tft.print(timeBuf);
    }
  }
  if (millis()-lastStatMs>=STAT_TICK_MS) {
    tickStats(); lastStatMs=millis(); doWeatherUpdate();
    if (isHomeVisible()) screenDirty=true;
  }
  if (pet.stage!=lastDrawnStage && isHomeVisible()) screenDirty=true;
  if (statsNeedRedraw) { statsNeedRedraw=false; if (isHomeVisible()) screenDirty=true; }
  if (screenDirty) redrawScreen();
  if (millis()-lastSaveMs>=SAVE_INTERVAL_MS) {
    time_t now; time(&now); pet.lastSave=(uint32_t)now; savePet(pet); lastSaveMs=millis(); Serial.println("Auto-saved.");
  }
  delay(20);
}
