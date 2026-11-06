#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// ── Hit-the-Zone Training Minigame ────────────────────────────────────────────
// Marker slides left/right along a bar — press BTN1 when inside green zone.
// 5 rounds, speed increases each round.
// Score → training: perfect(3) good(2) edge(1) miss(0), max 15.
// Returns training points earned (0–15). 0 = bailed or all misses.

#define MG_ROUNDS     5
#define MG_BAR_X      8
#define MG_BAR_W      119
#define MG_BAR_Y      88
#define MG_BAR_H      20
#define MG_ZONE_W     26
#define MG_MKR_W      8
#define MG_BASE_SPD   0.55f   // px/frame round 1
#define MG_SPD_INC    0.20f   // added per round

// Eye-comfort: zone is always centred on the bar
#define MG_ZONE_X     (MG_BAR_X + (MG_BAR_W - MG_ZONE_W) / 2)

// ── Internal helpers ──────────────────────────────────────────────────────────

static bool _mgInZone(float mx) {
  return (mx + MG_MKR_W > MG_ZONE_X) && (mx < MG_ZONE_X + MG_ZONE_W);
}

static uint8_t _mgScoreHit(float mx) {
  float mc   = mx + MG_MKR_W * 0.5f;
  float zc   = MG_ZONE_X + MG_ZONE_W * 0.5f;
  float dist = fabsf(mc - zc);
  if (dist < MG_ZONE_W * 0.18f) return 3;   // perfect centre
  if (dist < MG_ZONE_W * 0.42f) return 2;   // good
  return 1;                                  // edge
}

// ── Draw the full game frame ───────────────────────────────────────────────────
static void _mgDraw(TFT_eSPI &tft, uint8_t round, uint8_t score,
                    float markerX, float speed, bool roundDone,
                    const char *flashMsg, uint16_t flashCol, uint8_t flashTimer) {

  tft.fillScreen(TFT_BLACK);

  // ── Header ──────────────────────────────────────────────────────────────────
  tft.fillRect(0, 0, 135, 28, tft.color565(0, 30, 60));
  tft.setTextColor(TFT_CYAN, tft.color565(0, 30, 60));
  tft.setTextSize(2);
  tft.setCursor(8, 6);
  tft.print("TRAIN!");
  tft.setTextColor(tft.color565(80, 120, 160), tft.color565(0, 30, 60));
  tft.setTextSize(1);
  tft.setCursor(88, 10);
  tft.print("Rnd ");
  tft.print(round);
  tft.print("/");
  tft.print(MG_ROUNDS);
  tft.drawFastHLine(0, 28, 135, TFT_DARKGREY);

  // ── Score ────────────────────────────────────────────────────────────────────
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(8, 36);
  tft.print("Score: ");
  tft.print(score);
  tft.print("/15");

  // ── Speed bar ────────────────────────────────────────────────────────────────
  tft.setTextColor(tft.color565(60, 60, 60), TFT_BLACK);
  tft.setCursor(8, 48);
  tft.print("Speed");
  tft.fillRect(40, 47, 87, 6, tft.color565(25, 25, 25));
  float spd_pct = (speed - MG_BASE_SPD) / (MG_SPD_INC * (MG_ROUNDS - 1));
  if (spd_pct < 0) spd_pct = 0;
  if (spd_pct > 1) spd_pct = 1;
  uint16_t spdCol = spd_pct < 0.4f
    ? tft.color565(50, 180, 50)
    : spd_pct < 0.7f
    ? tft.color565(220, 140, 0)
    : tft.color565(220, 60, 60);
  tft.fillRect(40, 47, (int)(spd_pct * 87), 6, spdCol);

  // ── Flash message ────────────────────────────────────────────────────────────
  if (flashTimer > 0 && flashMsg) {
    tft.setTextColor(flashCol, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(8, 62);
    tft.print(flashMsg);
  }

  // ── Sliding bar ──────────────────────────────────────────────────────────────
  // Track background
  tft.fillRect(MG_BAR_X, MG_BAR_Y, MG_BAR_W, MG_BAR_H, tft.color565(20, 20, 20));
  // Red danger ends
  tft.fillRect(MG_BAR_X,                  MG_BAR_Y, 4, MG_BAR_H, tft.color565(150, 30, 30));
  tft.fillRect(MG_BAR_X + MG_BAR_W - 4,  MG_BAR_Y, 4, MG_BAR_H, tft.color565(150, 30, 30));
  // Green zone fill
  tft.fillRect(MG_ZONE_X, MG_BAR_Y, MG_ZONE_W, MG_BAR_H, tft.color565(15, 50, 15));
  // Zone border
  tft.drawRect(MG_ZONE_X, MG_BAR_Y, MG_ZONE_W, MG_BAR_H, tft.color565(60, 180, 60));
  // Centre target line
  int zc = MG_ZONE_X + MG_ZONE_W / 2;
  tft.drawFastVLine(zc, MG_BAR_Y + 3, MG_BAR_H - 6, tft.color565(40, 120, 40));

  // Marker
  bool inZone = _mgInZone(markerX);
  uint16_t mCol = roundDone
    ? tft.color565(80, 80, 80)
    : inZone
    ? TFT_WHITE
    : TFT_CYAN;
  tft.fillRect((int)markerX, MG_BAR_Y, MG_MKR_W, MG_BAR_H, mCol);

  // ── HIT prompt ───────────────────────────────────────────────────────────────
  if (inZone && !roundDone) {
    tft.setTextColor(tft.color565(60, 200, 60), TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(52, MG_BAR_Y + MG_BAR_H + 6);
    tft.print(">> HIT! <<");
  }

  // ── Round progress dots ───────────────────────────────────────────────────────
  for (int i = 0; i < MG_ROUNDS; i++) {
    uint16_t dotCol = (i < (int)round - 1)
      ? tft.color565(60, 180, 60)          // done
      : (i == (int)round - 1)
      ? TFT_CYAN                            // current
      : tft.color565(30, 30, 30);           // future
    tft.fillCircle(20 + i * 24, MG_BAR_Y + MG_BAR_H + 20, 5, dotCol);
  }

  // ── Instructions ─────────────────────────────────────────────────────────────
  tft.setTextColor(tft.color565(50, 50, 50), TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(8, MG_BAR_Y + MG_BAR_H + 34);
  tft.print("BTN1=hit  BTN2=quit");
}

// ── Result screen ─────────────────────────────────────────────────────────────
static void _mgDrawResult(TFT_eSPI &tft, uint8_t score) {
  uint8_t earned = score > 15 ? 15 : score;

  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 135, 28, tft.color565(0, 30, 60));
  tft.setTextColor(TFT_CYAN, tft.color565(0, 30, 60));
  tft.setTextSize(2);
  tft.setCursor(8, 6);
  tft.print("DONE!");
  tft.drawFastHLine(0, 28, 135, TFT_DARKGREY);

  // Big score
  uint16_t scoreCol = earned >= 12
    ? tft.color565(60, 220, 60)
    : earned >= 6
    ? tft.color565(220, 180, 0)
    : tft.color565(220, 120, 30);

  tft.setTextSize(3);
  tft.setTextColor(scoreCol, TFT_BLACK);
  tft.setCursor(40, 50);
  tft.print("+");
  tft.print(earned);

  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(20, 82);
  tft.print("Training earned!");

  // Stars
  uint8_t stars = earned >= 12 ? 3 : earned >= 6 ? 2 : earned > 0 ? 1 : 0;
  tft.setTextSize(2);
  for (int i = 0; i < 3; i++) {
    tft.setTextColor(i < stars ? tft.color565(255, 200, 0) : tft.color565(40, 40, 40), TFT_BLACK);
    tft.setCursor(28 + i * 28, 100);
    tft.print("*");
  }

  // Training bar
  tft.fillRect(8, 130, 119, 10, tft.color565(20, 40, 20));
  tft.fillRect(8, 130, (int)(earned / 15.0f * 119), 10, scoreCol);

  tft.setTextSize(1);
  tft.setTextColor(tft.color565(60, 60, 60), TFT_BLACK);
  tft.setCursor(8, 148);
  tft.print("Score: ");
  tft.print(score);
  tft.print("  (");
  tft.print(earned);
  tft.print("/15)");

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(8, 200);
  tft.print("BTN1: back to Care");
}

// ── Main entry point — call instead of applySelectedCareAction for Train ───────
// Runs a fully blocking game loop. Returns training points earned (0–15).
inline uint8_t runTrainingMinigame(TFT_eSPI &tft, uint8_t btn1Pin, uint8_t btn2Pin) {

  uint8_t round     = 1;
  uint8_t score     = 0;
  float   markerX   = MG_BAR_X;
  float   markerDir = 1.0f;
  float   speed     = MG_BASE_SPD;
  bool    roundDone = false;

  const char *flashMsg  = "Get Ready!";
  uint16_t    flashCol  = TFT_CYAN;
  uint8_t     flashTimer = 30;

  unsigned long roundEndMs = 0;
  bool waitingNextRound    = false;

  // Debounce state
  bool lastBtn1 = HIGH, lastBtn2 = HIGH;
  unsigned long btn1Ms = 0, btn2Ms = 0;

  _mgDraw(tft, round, score, markerX, speed, roundDone, flashMsg, flashCol, flashTimer);

  while (true) {

    // ── Button read with debounce ──────────────────────────────────────────────
    bool b1 = digitalRead(btn1Pin);
    bool b2 = digitalRead(btn2Pin);

    bool btn1Pressed = (b1 == LOW && lastBtn1 == HIGH && millis() - btn1Ms > 40);
    bool btn2Pressed = (b2 == LOW && lastBtn2 == HIGH && millis() - btn2Ms > 40);
    if (b1 == LOW && lastBtn1 == HIGH) btn1Ms = millis();
    if (b2 == LOW && lastBtn2 == HIGH) btn2Ms = millis();
    lastBtn1 = b1; lastBtn2 = b2;

    // ── BTN2: bail out ─────────────────────────────────────────────────────────
    if (btn2Pressed) {
      return 0;
    }

    // ── Waiting between rounds ────────────────────────────────────────────────
    if (waitingNextRound) {
      if (millis() - roundEndMs >= 700) {
        round++;
        if (round > MG_ROUNDS) break; // done
        speed     = MG_BASE_SPD + (round - 1) * MG_SPD_INC;
        markerX   = MG_BAR_X;
        markerDir = 1.0f;
        roundDone = false;
        waitingNextRound = false;
        flashMsg   = nullptr;
        flashTimer = 0;
      }
      delay(20);
      _mgDraw(tft, round - 1, score, markerX, speed - MG_SPD_INC, true, flashMsg, flashCol, flashTimer);
      if (flashTimer > 0) flashTimer--;
      continue;
    }

    // ── Move marker ───────────────────────────────────────────────────────────
    if (!roundDone) {
      markerX += markerDir * speed;
      if (markerX + MG_MKR_W >= MG_BAR_X + MG_BAR_W) {
        markerX   = MG_BAR_X + MG_BAR_W - MG_MKR_W;
        markerDir = -1.0f;
      }
      if (markerX <= MG_BAR_X) {
        markerX   = MG_BAR_X;
        markerDir = 1.0f;
      }
    }

    // ── BTN1: hit attempt ─────────────────────────────────────────────────────
    if (btn1Pressed && !roundDone) {
      roundDone = true;
      waitingNextRound = true;
      roundEndMs = millis();

      if (_mgInZone(markerX)) {
        uint8_t pts = _mgScoreHit(markerX);
        score += pts;
        if (pts == 3) { flashMsg = "PERFECT!"; flashCol = tft.color565(60, 220, 60); }
        else if (pts == 2) { flashMsg = "Good!";    flashCol = tft.color565(150, 220, 30); }
        else               { flashMsg = "OK";       flashCol = tft.color565(220, 200, 0);  }
      } else {
        flashMsg = "Miss!";
        flashCol = tft.color565(220, 60, 60);
      }
      flashTimer = 35;
    }

    // ── Redraw ────────────────────────────────────────────────────────────────
    _mgDraw(tft, round, score, markerX, speed, roundDone, flashMsg, flashCol, flashTimer);
    if (flashTimer > 0) flashTimer--;

    delay(20);
  }

  // ── Show result screen ────────────────────────────────────────────────────────
  uint8_t earned = score > 15 ? 15 : score;
  _mgDrawResult(tft, score);

  // Wait for BTN1 to dismiss
  bool last = HIGH;
  while (true) {
    bool b = digitalRead(btn1Pin);
    if (b == LOW && last == HIGH) break;
    last = b;
    delay(20);
  }

  return earned;
}
