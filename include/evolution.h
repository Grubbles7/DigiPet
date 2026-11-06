#pragma once
#include <Arduino.h>

// ── Form IDs ──────────────────────────────────────────────────────────────────
// These are the stable IDs used in NVS, Dex bitmask, and sprite lookup.
// Never reorder — only append.

// Stage 0
#define FORM_DIGIEGG      0
#define FORM_CORRUPT_EGG  1   // dark lineage only

// Stage 1
#define FORM_PUFFLE       2
#define FORM_SHADEPUFF    3   // dark lineage only

// Stage 2 — Child
#define FORM_FANGMON      4   // training dominant
#define FORM_LUMIMON      5   // happiness dominant

// Stage 3 — Adult (Fang route)
#define FORM_BLADEMON     6   // Fangmon + training dominant
#define FORM_GUARDRAMON   7   // Fangmon + health dominant

// Stage 3 — Adult (Lumi route)
#define FORM_HEXAMON      8   // Lumimon + happiness varied
#define FORM_SERENAMON    9   // Lumimon + balanced care

// Stage 4 — Ultimate (Fang route)
#define FORM_WARCLAWMON   10  // Blademon + training
#define FORM_STORMFANGMON 11  // Blademon + storm weather / events
#define FORM_BASTIONMON   12  // Guardramon + health/resilience
#define FORM_IRONHIDEMON  13  // Guardramon + training, low happiness

// Stage 4 — Ultimate (Lumi route)
#define FORM_ORACULUMON   14  // Hexamon + happiness/events
#define FORM_MOONVEILMON  15  // Hexamon + night/clean run
#define FORM_AEGISMON     16  // Serenamon + health/stability
#define FORM_SOLAPHIMON   17  // Serenamon + balanced excellence

// Hidden override forms (replace a normal evolution result)
#define FORM_CHAMPION     18  // perfect all minigames on Child stage
#define FORM_DARK         19  // corruption/death history
#define FORM_IRON         20  // high train, very low happiness
#define FORM_GLITCH       21  // anomaly / massive offline catch-up

// Prestige forms (ascend above Ultimate)
#define FORM_PERFECT      22  // all stats > 90 at Ultimate
#define FORM_ANCIENT      23  // 500+ hours alive
#define FORM_SAINT        24  // flawless run (no death, no neglect)
#define FORM_ZENITH       25  // all minigames mastered + 300h + high stats

#define FORM_COUNT        26

// ── Stage map ─────────────────────────────────────────────────────────────────
// Returns the logical stage (0-5) for a given form ID.
// Stage 5 = prestige (above Ultimate).
inline uint8_t formStage(uint8_t formId) {
  if (formId == FORM_DIGIEGG || formId == FORM_CORRUPT_EGG) return 0;
  if (formId == FORM_PUFFLE  || formId == FORM_SHADEPUFF)   return 1;
  if (formId == FORM_FANGMON || formId == FORM_LUMIMON)     return 2;
  if (formId >= FORM_BLADEMON  && formId <= FORM_SERENAMON) return 3;
  if (formId >= FORM_WARCLAWMON && formId <= FORM_SOLAPHIMON) return 4;
  if (formId >= FORM_CHAMPION && formId <= FORM_GLITCH)     return 4;  // override ultimates
  if (formId >= FORM_PERFECT  && formId <= FORM_ZENITH)     return 5;
  return 0;
}

// ── Form names ────────────────────────────────────────────────────────────────
inline const char* formName(uint8_t formId) {
  switch (formId) {
    case FORM_DIGIEGG:      return "Digi Egg";
    case FORM_CORRUPT_EGG:  return "Corrupt Egg";
    case FORM_PUFFLE:       return "Puffle";
    case FORM_SHADEPUFF:    return "Shadepuff";
    case FORM_FANGMON:      return "Fangmon";
    case FORM_LUMIMON:      return "Lumimon";
    case FORM_BLADEMON:     return "Blademon";
    case FORM_GUARDRAMON:   return "Guardramon";
    case FORM_HEXAMON:      return "Hexamon";
    case FORM_SERENAMON:    return "Serenamon";
    case FORM_WARCLAWMON:   return "Warclawmon";
    case FORM_STORMFANGMON: return "Stormfangmon";
    case FORM_BASTIONMON:   return "Bastionmon";
    case FORM_IRONHIDEMON:  return "Ironhidemon";
    case FORM_ORACULUMON:   return "Oraculumon";
    case FORM_MOONVEILMON:  return "Moonveilmon";
    case FORM_AEGISMON:     return "Aegismon";
    case FORM_SOLAPHIMON:   return "Solaphimon";
    case FORM_CHAMPION:     return "Victorumon";
    case FORM_DARK:         return "Umbraimon";
    case FORM_IRON:         return "Ferrumon";
    case FORM_GLITCH:       return "Nullamon";
    case FORM_PERFECT:      return "Perfectumon";
    case FORM_ANCIENT:      return "Ancestormon";
    case FORM_SAINT:        return "Sanctumon";
    case FORM_ZENITH:       return "Zenithmon";
    default:                return "???";
  }
}

// ── Lineage flags (stored in pet.lineage bitmask) ─────────────────────────────
#define LIN_CORRUPT     0x01   // dark chip used or deathCount >= 3
#define LIN_FANG        0x02   // currently on Fang route
#define LIN_LUMI        0x04   // currently on Lumi route
#define LIN_STORM_EVT   0x08   // storm weather evo trigger seen
#define LIN_NIGHT_EVT   0x10   // night-time evolution occurred
#define LIN_MINIGAME_ACE 0x20  // all minigames perfected on Child stage
#define LIN_CLEAN       0x40   // no death, no neglect flag yet
#define LIN_IRON_CARE   0x80   // high train / very low play history

// ── Evolution scoring helper ──────────────────────────────────────────────────
// Returns a weighted score for how "dominant" training vs happiness is.
// Positive = training dominant, negative = happiness dominant.
inline int8_t dominanceScore(uint8_t training, uint8_t happiness) {
  return (int8_t)((int)training - (int)happiness);
}

// ── Core evolution thresholds ─────────────────────────────────────────────────
// minAge is in hours. All other values are stat minimums 0-100.
struct EvoThreshold {
  uint32_t minAge;
  uint8_t  minHunger;
  uint8_t  minHappy;
  uint8_t  minHealth;
  uint8_t  minTraining;
};

// One threshold per logical stage transition (0→1, 1→2, 2→3, 3→4)
const EvoThreshold EVO_THRESHOLDS[] = {
  { 1,   0,  0,  0,  0  },   // DigiEgg  → Baby
  { 24,  40, 40, 50, 0  },   // Baby     → Child
  { 72,  50, 50, 60, 30 },   // Child    → Adult
  { 168, 60, 60, 70, 60 },   // Adult    → Ultimate
};

// ── canEvolve ─────────────────────────────────────────────────────────────────
// Returns true if the pet meets the age + stat minimum for its current stage.
// Prestige forms (stage 5) are checked separately in checkPrestige().
inline bool canEvolve(uint8_t stage, uint32_t age,
                      uint8_t hunger, uint8_t happiness,
                      uint8_t health, uint8_t training) {
  if (stage >= 4) return false;  // at/above ultimate, use prestige check
  const EvoThreshold &t = EVO_THRESHOLDS[stage];
  return (age      >= t.minAge      &&
          hunger   >= t.minHunger   &&
          happiness>= t.minHappy    &&
          health   >= t.minHealth   &&
          training >= t.minTraining);
}

// ── chooseEvolution ───────────────────────────────────────────────────────────
// Given the current form and pet state, returns the next form ID.
// Checks hidden overrides first, then picks the correct branch.
//
// Parameters:
//   currentForm  — current FORM_xxx ID
//   age          — hours alive
//   hunger/happiness/health/training — current stats
//   lineage      — LIN_xxx bitmask
//   deathCount   — total deaths across all runs
//   weather      — current weather scene (WX_xxx from weather.h)
//   minigameAces — bitmask: bit 0=Feed, 1=Play, 2=Train, 3=Heal perfected this stage
inline uint8_t chooseEvolution(uint8_t currentForm,
                               uint32_t age,
                               uint8_t hunger, uint8_t happiness,
                               uint8_t health, uint8_t training,
                               uint8_t lineage, uint8_t deathCount,
                               uint8_t weather, uint8_t minigameAces) {

  uint8_t stage = formStage(currentForm);
  int8_t dom = dominanceScore(training, happiness);

  // ── Stage 0: DigiEgg → Baby ───────────────────────────────────────────────
  if (stage == 0) {
    if ((lineage & LIN_CORRUPT) || deathCount >= 3)
      return FORM_SHADEPUFF;
    return FORM_PUFFLE;
  }

  // ── Stage 1: Baby → Child ─────────────────────────────────────────────────
  if (stage == 1) {
    // Shadepuff → dark override path
    if (currentForm == FORM_SHADEPUFF) return FORM_DARK;
    // Puffle: training dominant → Fangmon, else → Lumimon
    if (dom >= 10) return FORM_FANGMON;
    return FORM_LUMIMON;
  }

  // ── Stage 2: Child → Adult ────────────────────────────────────────────────
  if (stage == 2) {
    // ── Hidden override: Champion ──────────────────────────────────────────
    // All 4 minigames perfected while on Child stage
    if (minigameAces == 0x0F) return FORM_CHAMPION;

    // Fang route
    if (currentForm == FORM_FANGMON) {
      // Health-focused → Guardramon, else → Blademon
      if (health >= 75 && health > training) return FORM_GUARDRAMON;
      return FORM_BLADEMON;
    }
    // Lumi route
    if (currentForm == FORM_LUMIMON) {
      // Balanced care (all stats within 20 of each other) → Serenamon
      uint8_t hi = max({hunger, happiness, health, training});
      uint8_t lo = min({hunger, happiness, health, training});
      if ((uint8_t)(hi - lo) <= 20) return FORM_SERENAMON;
      return FORM_HEXAMON;
    }
    // Fallback
    return FORM_BLADEMON;
  }

  // ── Stage 3: Adult → Ultimate ─────────────────────────────────────────────
  if (stage == 3) {
    // ── Hidden override: Dark ─────────────────────────────────────────────
    if ((lineage & LIN_CORRUPT) && deathCount >= 3) return FORM_DARK;

    // ── Hidden override: Iron ─────────────────────────────────────────────
    // Very high training, happiness below 30, low play history
    if (training >= 80 && happiness < 30 && (lineage & LIN_IRON_CARE))
      return FORM_IRON;

    // ── Hidden override: Glitch ───────────────────────────────────────────
    // Massive offline catch-up or anomalous state (flagged externally)
    if (lineage & LIN_STORM_EVT && age > 300 && training < 20)
      return FORM_GLITCH;

    // Blademon branch
    if (currentForm == FORM_BLADEMON) {
      // Storm weather active + elemental condition → Stormfangmon
      if ((weather == 4 || (lineage & LIN_STORM_EVT)) && training >= 70)
        return FORM_STORMFANGMON;
      return FORM_WARCLAWMON;
    }
    // Guardramon branch
    if (currentForm == FORM_GUARDRAMON) {
      // Low happiness + high training → Ironhidemon
      if (happiness < 50 && training >= 70) return FORM_IRONHIDEMON;
      return FORM_BASTIONMON;
    }
    // Hexamon branch
    if (currentForm == FORM_HEXAMON) {
      // Night-time evolution or clean run → Moonveilmon
      if ((lineage & LIN_NIGHT_EVT) || (lineage & LIN_CLEAN))
        return FORM_MOONVEILMON;
      return FORM_ORACULUMON;
    }
    // Serenamon branch
    if (currentForm == FORM_SERENAMON) {
      // Balanced excellence (all stats >= 65) → Solaphimon
      if (hunger >= 65 && happiness >= 65 && health >= 65 && training >= 65)
        return FORM_SOLAPHIMON;
      return FORM_AEGISMON;
    }
    // Champion already at adult — keep as Champion at Ultimate
    if (currentForm == FORM_CHAMPION) return FORM_CHAMPION;

    return FORM_WARCLAWMON;  // fallback
  }

  return currentForm;  // no change if already at ultimate or above
}

// ── checkPrestige ─────────────────────────────────────────────────────────────
// Called every hour tick when formStage == 4 (Ultimate or hidden override).
// Returns a prestige form ID if conditions are met, or 0xFF if not.
// Prestige forms permanently replace the current Ultimate.
inline uint8_t checkPrestige(uint8_t currentForm,
                              uint32_t age,
                              uint8_t hunger, uint8_t happiness,
                              uint8_t health, uint8_t training,
                              uint8_t lineage, uint8_t minigameAces) {
  if (formStage(currentForm) < 4) return 0xFF;  // not at ultimate yet

  // Zenith — hardest: all minigames mastered + 300h + all stats >= 70
  if (minigameAces == 0x0F && age >= 300 &&
      hunger >= 70 && happiness >= 70 && health >= 70 && training >= 70)
    return FORM_ZENITH;

  // Perfect — all stats >= 90 simultaneously
  if (hunger >= 90 && happiness >= 90 && health >= 90 && training >= 90)
    return FORM_PERFECT;

  // Saint — no death, no neglect, all stats never below 20 (tracked via LIN_CLEAN)
  if ((lineage & LIN_CLEAN) && age >= 100)
    return FORM_SAINT;

  // Ancient — survive 500+ hours
  if (age >= 500) return FORM_ANCIENT;

  return 0xFF;  // no prestige yet
}

// ── updateLineage ─────────────────────────────────────────────────────────────
// Call every hour tick to keep lineage flags current.
// Pass current weather (WX_xxx), current hour (0-23 from NTP), stat snapshot,
// deathCount, and whether the pet has ever hit HP<20 this stage.
inline uint8_t updateLineage(uint8_t lineage,
                             uint8_t weather,
                             uint8_t hour,
                             uint8_t happiness, uint8_t training,
                             uint8_t deathCount,
                             bool everNeglected) {
  // Corrupt flag
  if (deathCount >= 3) lineage |= LIN_CORRUPT;

  // Storm weather event
  if (weather == 4) lineage |= LIN_STORM_EVT;  // WX_STORM == 4

  // Night evolution window (10pm - 7am)
  if (hour >= 22 || hour < 7) lineage |= LIN_NIGHT_EVT;

  // Iron care: training dominant AND happiness neglected
  if (training >= 70 && happiness < 40) lineage |= LIN_IRON_CARE;

  // Clean flag: clear if ever neglected
  if (everNeglected) lineage &= ~LIN_CLEAN;
  else               lineage |= LIN_CLEAN;

  return lineage;
}
