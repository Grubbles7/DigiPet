#pragma once
#include <Preferences.h>
#include "evolution.h"

// ── Pet stats structure ───────────────────────────────────────────────────────
struct PetStats {
  uint8_t  hunger;       // 0=starving  100=full
  uint8_t  happiness;    // 0=sad       100=happy
  uint8_t  training;     // 0=weak      100=strong
  uint8_t  health;       // 0=sick      100=healthy (displayed as HP)

  uint8_t  stage;        // logical stage 0-5 (0=Egg … 4=Ultimate, 5=Prestige)
  uint8_t  formId;       // specific form — FORM_xxx constant from evolution.h
  uint8_t  lineage;      // LIN_xxx bitmask — route/flag history
  uint8_t  deathCount;   // total deaths across all resets (never cleared)
  uint8_t  minigameAces; // bitmask: bit0=Feed, 1=Play, 2=Train, 3=Heal perfected this stage

  uint32_t age;          // hours alive this run
  uint32_t lastSave;     // unix timestamp of last save

  uint8_t  dead;         // 1=dead, awaiting reset
  uint8_t  deathStage;   // formId at time of death
  uint8_t  deathCause;   // 0=starvation 1=neglect

  uint8_t  everNeglected; // 1 if HP ever dropped below 20 this stage (LIN_CLEAN killer)
};

// ── Form name passthrough ─────────────────────────────────────────────────────
// Convenience so main.cpp can call stageName() as before
inline const char* stageName(uint8_t formId) {
  return formName(formId);
}

// ── Stage label for status screen ────────────────────────────────────────────
inline const char* stageLabel(uint8_t stage) {
  switch (stage) {
    case 0: return "Egg";
    case 1: return "Baby";
    case 2: return "Child";
    case 3: return "Adult";
    case 4: return "Ultimate";
    case 5: return "Prestige";
    default: return "???";
  }
}

// ── Defaults ──────────────────────────────────────────────────────────────────
inline PetStats defaultPet() {
  return {
    .hunger        = 80,
    .happiness     = 80,
    .training      = 20,
    .health        = 100,
    .stage         = 0,
    .formId        = FORM_DIGIEGG,
    .lineage       = LIN_CLEAN,
    .deathCount    = 0,
    .minigameAces  = 0,
    .age           = 0,
    .lastSave      = 0,
    .dead          = 0,
    .deathStage    = FORM_DIGIEGG,
    .deathCause    = 0,
    .everNeglected = 0,
  };
}

// ── NVS ───────────────────────────────────────────────────────────────────────
#define NVS_NAMESPACE    "digiedc"
#define NVS_SCHEMA_VERSION 3   // bumped from 2 to trigger migration for new fields

inline void savePet(const PetStats &pet) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putUChar("hunger",       pet.hunger);
  prefs.putUChar("happiness",    pet.happiness);
  prefs.putUChar("training",     pet.training);
  prefs.putUChar("health",       pet.health);
  prefs.putUChar("stage",        pet.stage);
  prefs.putUChar("formId",       pet.formId);
  prefs.putUChar("lineage",      pet.lineage);
  prefs.putUChar("deathCount",   pet.deathCount);
  prefs.putUChar("mgAces",       pet.minigameAces);
  prefs.putUInt ("age",          pet.age);
  prefs.putUInt ("lastSave",     pet.lastSave);
  prefs.putUChar("dead",         pet.dead);
  prefs.putUChar("deathStage",   pet.deathStage);
  prefs.putUChar("deathCause",   pet.deathCause);
  prefs.putUChar("everNeglect",  pet.everNeglected);
  prefs.end();
}

inline PetStats loadPet() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);
  if (!prefs.isKey("health")) { prefs.end(); return defaultPet(); }
  PetStats pet;
  pet.hunger        = prefs.getUChar("hunger",      80);
  pet.happiness     = prefs.getUChar("happiness",   80);
  pet.training      = prefs.getUChar("training",    20);
  pet.health        = prefs.getUChar("health",      100);
  pet.stage         = prefs.getUChar("stage",       0);
  pet.formId        = prefs.getUChar("formId",      FORM_DIGIEGG);
  pet.lineage       = prefs.getUChar("lineage",     LIN_CLEAN);
  pet.deathCount    = prefs.getUChar("deathCount",  0);
  pet.minigameAces  = prefs.getUChar("mgAces",      0);
  pet.age           = prefs.getUInt ("age",         0);
  pet.lastSave      = prefs.getUInt ("lastSave",    0);
  pet.dead          = prefs.getUChar("dead",        0);
  pet.deathStage    = prefs.getUChar("deathStage",  FORM_DIGIEGG);
  pet.deathCause    = prefs.getUChar("deathCause",  0);
  pet.everNeglected = prefs.getUChar("everNeglect", 0);
  prefs.end();
  return pet;
}

inline void resetPet(uint8_t keepDeathCount) {
  // Partial reset — preserves deathCount and lineage corruption flag across resets
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  uint8_t newLineage = LIN_CLEAN;
  if (keepDeathCount >= 3) newLineage |= LIN_CORRUPT;
  prefs.clear();
  prefs.putUChar("deathCount", keepDeathCount);
  prefs.putUChar("lineage",    newLineage);
  prefs.end();
}

// ── Migration v3 ──────────────────────────────────────────────────────────────
// Adds formId, lineage, deathCount, minigameAces, everNeglected to existing saves.
inline void runMigrationIfNeeded(PetStats &pet) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  int storedVersion = prefs.getInt("schema", 0);
  if (storedVersion < NVS_SCHEMA_VERSION) {
    // Infer formId from old stage if not present
    if (!prefs.isKey("formId")) {
      const uint8_t stageToForm[] = {
        FORM_DIGIEGG, FORM_PUFFLE, FORM_FANGMON,
        FORM_BLADEMON, FORM_WARCLAWMON
      };
      pet.formId = (pet.stage < 5) ? stageToForm[pet.stage] : FORM_WARCLAWMON;
      prefs.putUChar("formId", pet.formId);
    }
    if (!prefs.isKey("lineage"))    { pet.lineage = LIN_CLEAN; prefs.putUChar("lineage", pet.lineage); }
    if (!prefs.isKey("deathCount")) { pet.deathCount = 0;      prefs.putUChar("deathCount", 0); }
    if (!prefs.isKey("mgAces"))     { pet.minigameAces = 0;    prefs.putUChar("mgAces", 0); }
    if (!prefs.isKey("everNeglect")){ pet.everNeglected = 0;   prefs.putUChar("everNeglect", 0); }

    // Reset bad timestamps (carried forward from v2)
    if (pet.age > 10000) { pet.age = 0; prefs.putUInt("age", 0); }
    if (pet.lastSave < 1704067200UL) { pet.lastSave = 0; prefs.putUInt("lastSave", 0); }

    pet.stage = formStage(pet.formId);
    prefs.putUChar("stage", pet.stage);
    prefs.putInt("schema", NVS_SCHEMA_VERSION);
    Serial.println("Migration v3: branching evolution fields added.");
  }
  prefs.end();
}

// ── Evolution tick ────────────────────────────────────────────────────────────
// Call from tickStats() after decay is applied.
// Returns true if the pet evolved (caller should trigger fanfare + screen redraw).
inline bool tickEvolution(PetStats &pet, uint8_t currentWeather) {
  uint8_t hour = 12; // default if no NTP — use struct tm if available
  struct tm t;
  if (getLocalTime(&t)) hour = t.tm_hour;

  // Update lineage flags each hour
  pet.lineage = updateLineage(
    pet.lineage, currentWeather, hour,
    pet.happiness, pet.training,
    pet.deathCount, pet.everNeglected
  );

  // Track neglect (HP below 20)
  if (pet.health < 20) pet.everNeglected = 1;

  // ── Prestige check (stage 4 pets only) ───────────────────────────────────
  if (pet.stage == 4) {
    uint8_t prestige = checkPrestige(
      pet.formId, pet.age,
      pet.hunger, pet.happiness, pet.health, pet.training,
      pet.lineage, pet.minigameAces
    );
    if (prestige != 0xFF) {
      Serial.printf("EVO: Prestige! %s → %s\n",
                    formName(pet.formId), formName(prestige));
      pet.formId = prestige;
      pet.stage  = 5;
      pet.minigameAces = 0;  // reset for potential next stage
      return true;
    }
    return false;
  }

  // ── Standard evolution check (stages 0-3) ────────────────────────────────
  if (!canEvolve(pet.stage, pet.age,
                 pet.hunger, pet.happiness,
                 pet.health, pet.training)) {
    return false;
  }

  uint8_t nextForm = chooseEvolution(
    pet.formId, pet.age,
    pet.hunger, pet.happiness, pet.health, pet.training,
    pet.lineage, pet.deathCount,
    currentWeather, pet.minigameAces
  );

  Serial.printf("EVO: %s → %s\n", formName(pet.formId), formName(nextForm));

  pet.formId        = nextForm;
  pet.stage         = formStage(nextForm);
  pet.minigameAces  = 0;   // reset per-stage ace tracking
  pet.everNeglected = 0;   // fresh start for next stage

  return true;
}

// ── Death ─────────────────────────────────────────────────────────────────────
inline void killPet(PetStats &pet, uint8_t cause) {
  pet.dead        = 1;
  pet.deathStage  = pet.formId;   // store form, not just stage
  pet.deathCause  = cause;
  pet.health      = 0;
  pet.deathCount++;               // persists across resets
  if (pet.deathCount >= 3) pet.lineage |= LIN_CORRUPT;
}

// ── Offline decay ─────────────────────────────────────────────────────────────
#define MIN_VALID_TIMESTAMP 1704067200UL

inline void applyOfflineDecay(PetStats &pet, uint32_t currentTime) {
  if (pet.lastSave == 0 || currentTime == 0)       return;
  if (pet.lastSave < MIN_VALID_TIMESTAMP)           return;
  if (currentTime  < MIN_VALID_TIMESTAMP)           return;
  if (currentTime  < pet.lastSave)                  return;

  uint32_t hoursOff = (currentTime - pet.lastSave) / 3600;
  if (hoursOff == 0)    return;
  if (hoursOff > 168)   hoursOff = 168;

  uint32_t hungerDecay    = hoursOff * 3;
  uint32_t happinessDecay = hoursOff * 2;
  uint32_t healthDecay    = hoursOff * 1;

  pet.age += hoursOff;
  pet.hunger    = (pet.hunger    > hungerDecay)    ? (uint8_t)(pet.hunger    - hungerDecay)    : 0;
  pet.happiness = (pet.happiness > happinessDecay) ? (uint8_t)(pet.happiness - happinessDecay) : 0;
  pet.health    = (pet.health    > healthDecay)    ? (uint8_t)(pet.health    - healthDecay)    : 0;

  if (pet.health < 20) pet.everNeglected = 1;

  if (pet.health == 0 && !pet.dead) {
    uint8_t cause = (pet.hunger == 0) ? 0 : 1;
    killPet(pet, cause);
  }
}

// ── Minigame ace recorder ─────────────────────────────────────────────────────
// Call after a minigame result. bit0=Feed, 1=Play, 2=Train, 3=Heal.
// Only marks ace if perfect score was achieved.
inline void recordMinigameResult(PetStats &pet, uint8_t gameIndex, bool isPerfect) {
  if (isPerfect && gameIndex < 4) {
    pet.minigameAces |= (1 << gameIndex);
  }
}
