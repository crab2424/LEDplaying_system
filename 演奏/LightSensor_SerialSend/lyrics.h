#ifndef LYRICS_H
#define LYRICS_H
#define MELODY_LENGTH 62

#include <stdint.h>

// extern const uint32_t a[3];

// extern const uint32_t i[3];

// extern const uint32_t u[3];

// extern const uint32_t e[3];

// extern const uint32_t o[3];

// extern const uint32_t ka[3];

// extern const uint32_t ki[3];

// extern const uint32_t ku[3];

// extern const uint32_t ke[3];

// extern const uint32_t ko[3];

// extern const uint32_t sa[3];

// extern const uint32_t si[3];

// extern const uint32_t su[3];

// extern const uint32_t se[3];

// extern const uint32_t so[3];

// extern const uint32_t ta[3];

// extern const uint32_t ti[3];

// extern const uint32_t tu[3];

// extern const uint32_t te[3];

// extern const uint32_t to[3];

// extern const uint32_t na[3];

// extern const uint32_t ni[3];

// extern const uint32_t nu[3];

// extern const uint32_t ne[3];

// extern const uint32_t no[3];

// extern const uint32_t ha[3];

// extern const uint32_t hi[3];

// extern const uint32_t hu[3];

// extern const uint32_t he[3];

// extern const uint32_t ho[3];

// extern const uint32_t ma[3];

// extern const uint32_t mi[3];

// extern const uint32_t mu[3];

// extern const uint32_t me[3];

// extern const uint32_t mo[3];

// extern const uint32_t ya[3];

// extern const uint32_t yu[3];

// extern const uint32_t yo[3];

// extern const uint32_t ra[3];

// extern const uint32_t ri[3];

// extern const uint32_t ru[3];

// extern const uint32_t re[3];

// extern const uint32_t ro[3];

// extern const uint32_t wa[3];

// extern const uint32_t wo[3];

// extern const uint32_t n[3];

// extern const uint32_t ga[3];

extern const uint32_t *kaeruNoUta[36];

// ============================================================
// 楽譜配列（melody.pde より移植）
// ============================================================
// 音高インデックス＝ C3 からの半音数（クロマチック方式・オクターブ拡張）
//   C3=0, C4=12, C5=24
//   例) G3=7, Ab3=8, A3=9, Bb3=10, D4=14, E4=16, F4=17, G4=19, A4=21
// → 低音域（伴奏）や半音（♭/♯）も表現できるよう拡張した．



// 各音の高さ（音名 → ピッチインデックス: C3 からの半音数）
static const uint8_t melody[MELODY_LENGTH] = {
  24, 0, 26, 0, 28, 0, 29, 0, 28, 0, 26, 0, 24, 0, 0, 0,// C5 D5 E5 F5 E5 D5 C5
  28, 0, 29, 0, 31, 0, 33, 0, 31, 0, 29, 0, 28, 0, 0, 0, // E5 F5 G5 A5 G5 F5 E5
  24, 0,  0, 0, 24, 0, 0, 0, 24, 0, 0, 0, 24,0, 0,  0,             // C5 C5 C5 C5
  24, 24, 26, 26, 28, 28, 29, 29, // C5 C5 D5 D5 E5 E5 F5 F5
  28, 0,  26,  0, 24,  0                  // E5 D5 C5
};

// 各音の長さ（拍）
static const float duration[MELODY_LENGTH] = {
  2.0f, 0.0f, 2.0f, 0.0f, 2.0f, 0.0f, 2.0f, 0.0f, 2.0f, 0.0f, 2.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f,
  2.0f, 0.0f, 2.0f, 0.0f, 2.0f, 0.0f, 2.0f, 0.0f, 2.0f, 0.0f, 2.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f,
  2.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 
  1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
  1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f
};


// 各音の強弱（音量倍率: 0.0〜1.0）
static const float noteAmplitude[MELODY_LENGTH] = {
  1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f,
  1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f,
  1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f,
  1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 
  1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
};

// ピッチインデックス → 周波数(Hz) 変換テーブル（C3〜C5・半音単位）
// インデックス = C3 からの半音数
#define PITCH_TABLE_LEN 37
static const float pitchFreqHz[PITCH_TABLE_LEN] = {
  130.81f, // 0  C3
  138.59f, // 1  C#3
  146.83f, // 2  D3
  155.56f, // 3  D#3
  164.81f, // 4  E3
  174.61f, // 5  F3
  185.00f, // 6  F#3
  196.00f, // 7  G3
  207.65f, // 8  G#3 / Ab3
  220.00f, // 9  A3
  233.08f, // 10 A#3 / Bb3
  246.94f, // 11 B3
  261.63f, // 12 C4 (ド)
  277.18f, // 13 C#4
  293.66f, // 14 D4 (レ)
  311.13f, // 15 D#4
  329.63f, // 16 E4 (ミ)
  349.23f, // 17 F4 (ファ)
  369.99f, // 18 F#4
  392.00f, // 19 G4 (ソ)
  415.30f, // 20 G#4
  440.00f, // 21 A4 (ラ)
  466.16f, // 22 A#4
  493.88f, // 23 B4 (シ)
  523.25f, // 24 C5 (ド)
  554.37f, // 25 C#5
  587.33f, // 26 D5 (レ)
  622.25f, // 27 D#5
  659.25f, // 28 E5 (ミ)
  698.46f, // 29 F5 (ファ)
  739.99f, // 30 F#5
  783.99f, // 31 G5 (ソ)
  830.61f, // 32 G#5
  880.00f, // 33 A5 (ラ)
  932.33f, // 34 A#5
  987.77f, // 35 B5 (シ)
  1046.50f // 36 C6
};

// ============================================================
// 伴奏（和音）配列（melody.pde の chord 系より移植）
// ============================================================
// 1 和音あたり最大 CHORD_MAX_NOTES 音。実際の音数は chordNoteCount で指定。
// 音高は melody と同じ「C3 からの半音数」インデックス。
#define CHORD_COUNT 23
#define CHORD_MAX_NOTES 3

// 各和音を構成する音の数
static const uint8_t chordNoteCount[CHORD_COUNT] = {
  2, 2, 2, 2, 2, 2, 3, 2, 2,
  1, 2, 1, 2, 1, 2, 1, 2,
  1, 1, 1, 1, 1, 2
};

// 各和音の音の高さ（未使用の要素は 0 埋め）
static const uint8_t chord[CHORD_COUNT][CHORD_MAX_NOTES] = {
  {12, 19, 0}, // C4 G4
  {12, 19, 0}, // C4 G4
  {12, 19, 0}, // C4 G4
  {12, 19, 0}, // C4 G4
  {12, 19, 0}, // C4 G4
  {12, 19, 0}, // C4 G4
  { 7, 14, 17}, // G3 D4 F4
  {12, 19, 0}, // C4 G4
  {12, 19, 0}, // C4 G4
  { 7,  0, 0}, // G3
  {12, 19, 0}, // C4 G4
  { 7,  0, 0}, // G3
  {12, 19, 0}, // C4 G4
  { 7,  0, 0}, // G3
  {12, 19, 0}, // C4 G4
  { 7,  0, 0}, // G3
  {12, 19, 0}, // C4 G4
  {10,  0, 0}, // Bb3
  { 9,  0, 0}, // A3
  { 8,  0, 0}, // Ab3
  { 7,  0, 0}, // G3
  { 7,  0, 0}, // G3
  {12, 19, 0}  // C4 G4
};

// 各和音の長さ（拍）
static const float chordDuration[CHORD_COUNT] = {
  4.0f, 4.0f, 4.0f, 4.0f,
  4.0f, 4.0f, 4.0f, 4.0f,
  2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f,
  2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 4.0f
};

// 各和音の開始位置（拍）
static const float chordStartTime[CHORD_COUNT] = {
  0.0f,  4.0f,  8.0f, 12.0f,
  16.0f, 20.0f, 24.0f, 28.0f,
  32.0f, 34.0f, 36.0f, 38.0f, 40.0f, 42.0f, 44.0f, 46.0f,
  48.0f, 50.0f, 52.0f, 54.0f, 56.0f, 58.0f, 60.0f
};

// 各和音の強弱（音量倍率: 0.0〜1.0）
static const float chordAmplitude[CHORD_COUNT] = {
  1.0f, 1.0f, 1.0f, 1.0f,
  1.0f, 1.0f, 1.0f, 1.0f,
  1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
  1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
};

#endif