/*=============================================================
  LightSensor_SerialSend.ino
  演奏者Arduino — カラーセンサー(TCS34725)で指揮者LEDを受光し、
  楽譜に沿ってProcessingへ音データを送信する
=============================================================*/

#include <Wire.h>
#include <Adafruit_TCS34725.h>
#include "Arduino_LED_Matrix.h"  // 内蔵 LEDマトリクス制御 (Arduino UNO R4)
#include "lyrics.h"              // 楽譜配列 (melody[], duration[], pitchFreqHz[] など) + kaeruNoUta[]

ArduinoLEDMatrix matrix;

// ============================================================
//  ★ デバッグモード設定
//  1にすると、Processingへの送信をやめて、Arduinoの
//  「シリアルモニタ」に大量のデバッグテキストを表示します。
//  ※ 本番（Processingと繋ぐ時）は必ず 0 に戻してください。
// ============================================================
#define DEBUG_MODE 0

// ============================================================
//  ★ この演奏者の設定（ここを変更して各演奏者に書き込む）
// ============================================================
// 担当する楽器ID (指揮者側と同じマッピング)
//   0 = ピアノ(紫), 1 = バイオリン(赤), 2 = フルート(青), 3 = ドラム(緑)
const uint8_t MY_INSTRUMENT_ID = 1;  

// 担当色 (MY_INSTRUMENT_IDに対応する色)
const uint8_t MY_COLOR_ID = MY_INSTRUMENT_ID;

// ============================================================
//  音データ構造体
// ============================================================
struct {
  uint8_t pitch;
  uint8_t volume;
  uint16_t duration;
  uint8_t instrumentID;
} note;

uint32_t data = 0;

Adafruit_TCS34725 tcs = Adafruit_TCS34725(
  TCS34725_INTEGRATIONTIME_2_4MS,
  TCS34725_GAIN_60X
);

// ============================================================
//  色検出閾値 (元の絶対値判定に戻しました)
// ============================================================
// --- 赤色 (バイオリン: instID=1) ---
const uint16_t RED_R_MIN = 40;
const uint16_t RED_G_MAX = 20;
const uint16_t RED_B_MAX = 20;

// --- 緑色 (ドラム: instID=3) ---
const uint16_t GREEN_R_MAX = 30;
const uint16_t GREEN_G_MIN = 80;
const uint16_t GREEN_B_MAX = 50;

// --- 青色 (フルート: instID=2) ---
const uint16_t BLUE_R_MAX  = 20;
const uint16_t BLUE_G_MAX  = 60;
const uint16_t BLUE_B_MIN  = 80;

// --- 紫色 (ピアノ: instID=0) ---
const uint16_t PURPLE_R_MIN = 40;
const uint16_t PURPLE_G_MAX = 60;
const uint16_t PURPLE_B_MIN = 80;

// ============================================================
//  点滅・ピーク検出パラメータ
// ============================================================
const uint16_t BLINK_THRESHOLD = 30;   // 点灯中とみなす clear 値
const uint8_t  PEAK_DOWN_COUNT = 1;    // ピーク確定に必要な連続ダウン数

const uint16_t CLEAR_MAX_LOW  = 30;
const uint16_t CLEAR_MAX_HIGH = 500;

// ============================================================
//  内部状態
// ============================================================
uint32_t blinkTime1    = 0;
uint32_t blinkInterval = 0;
bool     inBlink       = false;

uint16_t clearMax      = 0;
bool     peakDetecting = false;
uint8_t  peakDownCount = 0;

uint8_t noteNum = 0;    // 楽譜の現在位置
uint8_t lyricIndex = 0; // LEDマトリクス歌詞の現在位置（実音符のみカウント）

// ラッチ: 一度自色を検出したら以降は色判定をスキップし、点滅検出のみで進行
// (輪唱用: 演奏者ごとに自色の初回点火タイミングがズレ → そのまま輪唱のズレになる)
bool armed = false;

// ============================================================
//  LEDマトリクス表示の非ブロッキング管理
//  sendNote() 直後に loadFrame() し、note.duration 経過で clear()
// ============================================================
bool     lyricShowing = false;
uint32_t lyricClearAt = 0;

// ============================================================
//  色検出 (元のロジック)
// ============================================================
int detectColorID(uint16_t r, uint16_t g, uint16_t b) {
  if (r >= PURPLE_R_MIN && g <= PURPLE_G_MAX && b >= PURPLE_B_MIN) return 0; // 紫
  if (r >= RED_R_MIN    && g <= RED_G_MAX    && b <= RED_B_MAX)    return 1; // 赤
  if (r <= BLUE_R_MAX   && g <= BLUE_G_MAX   && b >= BLUE_B_MIN)   return 2; // 青
  if (r <= GREEN_R_MAX  && g >= GREEN_G_MIN  && b <= GREEN_B_MAX)  return 3; // 緑
  return -1;
}

// ============================================================
//  送信関数
// ============================================================
void sendNote() {
  data = 0;
  data |= ((uint32_t)(note.pitch)        & 0x3F);
  data |= ((uint32_t)(note.volume)       & 0x7F) << 6;
  data |= ((uint32_t)(note.duration)     & 0x0FFF) << 13;
  data |= ((uint32_t)(note.instrumentID) & 0x03) << 25;

  uint8_t buf[4];
  buf[0] = (data >> 24) & 0xFF;
  buf[1] = (data >> 16) & 0xFF;
  buf[2] = (data >> 8)  & 0xFF;
  buf[3] = data & 0xFF;

  Serial.write(buf, 4);
}

// ============================================================
//  setup
// ============================================================
void setup() {
  Serial.begin(921600);

#if DEBUG_MODE
  // デバッグ時は USB シリアル接続が確立するまで待つ（最大3秒）
  uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 3000) { delay(10); }
  Serial.println("\n[BOOT] player.ino start");
#endif

  // LEDマトリクス初期化
  matrix.begin();
  matrix.clear();

  if (!tcs.begin()) {
#if DEBUG_MODE
    Serial.println("[ERR] TCS34725 が見つかりません！配線を確認してください。");
    Serial.flush();
#endif
    // エラー表示中も LEDマトリクスで分かるよう × 表示
    matrix.loadFrame((const uint32_t[]){0x80402010, 0x08040201, 0x00000000});
    while (1) { delay(1000); }
  }

#if DEBUG_MODE
  Serial.println("\n=== デバッグモード起動 ===");
  Serial.println("設定: MY_INSTRUMENT_ID = " + String(MY_INSTRUMENT_ID));
#endif

  delay(200);
}

// ============================================================
//  loop
// ============================================================
void loop() {
  uint32_t now = millis();

  // 表示中の歌詞を音長経過後に消す（非ブロッキング）
  if (lyricShowing && (int32_t)(now - lyricClearAt) >= 0) {
    matrix.clear();
    lyricShowing = false;
  }

  // 楽譜を全て演奏し終えたか
  if (noteNum >= MELODY_LENGTH) {
#if DEBUG_MODE
    static bool printedDone = false;
    if (!printedDone) {
      Serial.println("[STOP] 楽譜を全て演奏し終えました (noteNum = " + String(noteNum) + ")");
      printedDone = true;
    }
#endif
    return;
  }

  uint16_t r, g, b, c;
  tcs.getRawData(&r, &g, &b, &c);

#if DEBUG_MODE
  // 値を垂れ流しすぎないように、1秒に1回だけ定期状況をプリント
  static uint32_t lastPrint = 0;
  if (now - lastPrint > 1000) {
    Serial.print("[STATUS] R:"); Serial.print(r);
    Serial.print(" G:"); Serial.print(g);
    Serial.print(" B:"); Serial.print(b);
    Serial.print(" C:"); Serial.print(c);
    Serial.print(" | litNow:"); Serial.print(c >= BLINK_THRESHOLD);
    Serial.print(" peakDet:"); Serial.println(peakDetecting);
    lastPrint = now;
  }
#endif

  bool litNow = (c >= BLINK_THRESHOLD);

  if (litNow && !inBlink) {
    // 立ち上がりエッジ
    if (blinkTime1 != 0) {
      blinkInterval = now - blinkTime1;
    }
    blinkTime1    = now;
    inBlink       = true;
    clearMax      = 0;
    peakDetecting = true;
    peakDownCount = 0;

#if DEBUG_MODE
    Serial.print("\n[EDGE] 立ち上がりを検出！ 点滅間隔: "); 
    Serial.println(blinkInterval);
#endif
  }

  if (!litNow && inBlink) {
    inBlink = false;
#if DEBUG_MODE
    Serial.println("[EDGE] 消灯を検出。");
#endif
  }

  if (peakDetecting && litNow) {
    if (c > clearMax) {
      clearMax      = c;
      peakDownCount = 0;
    } else {
      peakDownCount++;
      if (peakDownCount >= PEAK_DOWN_COUNT) {
        peakDetecting = false;

#if DEBUG_MODE
        Serial.print("[PEAK] ピーク確定! clearMax = "); 
        Serial.println(clearMax);
#endif

        // ラッチ仕様: armed まで色判定 / armed 後は点滅のみで進行
        if (!armed) {
          int colorID = detectColorID(r, g, b);

#if DEBUG_MODE
          Serial.print("[COLOR] 色判定結果 ID: ");
          Serial.println(colorID);
          if (colorID == -1) {
            Serial.println("  -> 失敗: 色の条件を満たしていません。(R=" + String(r) + " G=" + String(g) + " B=" + String(b) + ")");
          } else if (colorID != MY_COLOR_ID) {
            Serial.println("  -> 無視: 自分の色(" + String(MY_COLOR_ID) + ")ではありませんでした。点火待機継続。");
          }
#endif

          if (colorID != MY_COLOR_ID) return;

          armed = true;
#if DEBUG_MODE
          Serial.println("[ARM] 自色を検出。以降は色判定をスキップして点滅追従します。");
#endif
        }

        // 音データ生成
        note.instrumentID = MY_INSTRUMENT_ID;
        uint8_t pitchVal = melody[noteNum];
        note.pitch = pitchVal;

        uint16_t baseVol = map(clearMax, CLEAR_MAX_LOW, CLEAR_MAX_HIGH, 20, 100);
        baseVol = constrain(baseVol, 1, 100);
        note.volume = (uint8_t)(baseVol * noteAmplitude[noteNum]);

        if (blinkInterval > 0) {
          uint16_t durMs = (uint16_t)(blinkInterval * duration[noteNum]);
          note.duration = constrain(durMs, 100, 4095);
        } else {
          note.duration = 500;
        }

#if DEBUG_MODE
        Serial.println("[SEND] 送信します！ 音符番号: " + String(noteNum) + " Pitch:" + String(note.pitch) + " Vol:" + String(note.volume));
#else
        // デバッグモードではない時だけ実際にシリアル送信する
        sendNote();
#endif

        // lyricTiming[] が非ゼロのスロットで次の文字を表示する
        // （melody の休符スロットも歌詞表示に使えるので「1音=2文字」が表現可能）
        // 初回点滅時は blinkInterval=0 なので、note.duration をフォールバックに使う
        if (lyricTiming[noteNum] > 0.0f && lyricIndex < LYRICS_LENGTH) {
          uint32_t lyricMs;
          if (blinkInterval > 0) {
            lyricMs = (uint32_t)(blinkInterval * lyricTiming[noteNum]);
          } else {
            lyricMs = note.duration;
          }
          matrix.loadFrame(kaeruNoUta[lyricIndex]);
          lyricShowing = true;
          lyricClearAt = now + lyricMs;
          lyricIndex++;
        }

        noteNum++;
      }
    }
  }
}
