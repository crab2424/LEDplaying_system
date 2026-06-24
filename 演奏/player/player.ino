/*=============================================================
  player.ino
  演奏者Arduino — カラーセンサー(TCS34725)で指揮者LEDを受光し、
  楽譜に沿ってProcessingへ音データを送信する
  + Web連携（待機中のみ楽器・音量変更を受け付ける）
=============================================================*/

#include <Wire.h>
#include <Adafruit_TCS34725.h>
#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include "Arduino_LED_Matrix.h"
#include "lyrics.h"

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
//   0 = ピアノ(紫), 1 = バイオリン(赤), 2 = フルート(青), 3 = ドラム(緑)
uint8_t MY_INSTRUMENT_ID = 0;   // Webから変更可能（初期値）
uint8_t MY_COLOR_ID      = 0;   // MY_INSTRUMENT_IDと連動

// ============================================================
//  ★ 楽譜選択（ここを変更してローカルでも楽譜を切り替えられる）
//   0 = リズム楽譜, 1 = メロディー楽譜
// ============================================================
uint8_t MY_SCORE_ID = 1;  // Webから変更可能（初期値）

// ============================================================
//  ★ Web連携設定
// ============================================================
const char* ssid          = "hackathon006-WPA2";
const char* pass          = "hackathon006";
const char* serverAddress = "192.168.11.5";
int         webPort       = 3000;
String      myID          = "performer1";  // ← 演奏者ごとに変更 (performer1, performer2, ...)
String      myType        = "performer";

// ============================================================
//  音データ構造体
// ============================================================
struct {
  uint8_t  pitch;
  uint8_t  volume;
  uint16_t duration;
  uint8_t  instrumentID;
} note;

uint32_t data = 0;

Adafruit_TCS34725 tcs = Adafruit_TCS34725(
  TCS34725_INTEGRATIONTIME_2_4MS,
  TCS34725_GAIN_60X
);

// ============================================================
//  色検出閾値
// ============================================================
const uint16_t RED_R_MIN    = 40;
const uint16_t RED_G_MAX    = 20;
const uint16_t RED_B_MAX    = 20;

const uint16_t GREEN_R_MAX  = 30;
const uint16_t GREEN_G_MIN  = 80;
const uint16_t GREEN_B_MAX  = 50;

const uint16_t BLUE_R_MAX   = 20;
const uint16_t BLUE_G_MAX   = 60;
const uint16_t BLUE_B_MIN   = 80;

const uint16_t PURPLE_R_MIN = 40;
const uint16_t PURPLE_G_MAX = 60;
const uint16_t PURPLE_B_MIN = 80;

// ============================================================
//  点滅・ピーク検出パラメータ
// ============================================================
const uint16_t BLINK_THRESHOLD = 30;
const uint8_t  PEAK_DOWN_COUNT = 1;

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

uint8_t noteNum    = 0;
uint8_t lyricIndex = 0;

bool armed = false;

bool     lyricShowing = false;
uint32_t lyricClearAt = 0;

// ============================================================
//  Web連携用変数
// ============================================================
uint8_t webVolume = 100;  // Webから受信した個別音量 0〜100

bool wifiReady = false;
const unsigned long WIFI_CONNECT_TIMEOUT  = 10000;
const unsigned long SERVER_CONNECT_TIMEOUT = 2000;

WiFiServer webServer(80);
WiFiClient wifiClient;
HttpClient httpClient = HttpClient(wifiClient, serverAddress, webPort);

const unsigned long HEARTBEAT_INTERVAL = 10000;
unsigned long lastHeartbeat = 0;

// ============================================================
//  色検出
// ============================================================
int detectColorID(uint16_t r, uint16_t g, uint16_t b) {
  if (r >= PURPLE_R_MIN && g <= PURPLE_G_MAX && b >= PURPLE_B_MIN) return 0;
  if (r >= RED_R_MIN    && g <= RED_G_MAX    && b <= RED_B_MAX)    return 1;
  if (r <= BLUE_R_MAX   && g <= BLUE_G_MAX   && b >= BLUE_B_MIN)   return 2;
  if (r <= GREEN_R_MAX  && g >= GREEN_G_MIN  && b <= GREEN_B_MAX)  return 3;
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
//  Web連携: クエリ文字列から指定キーの値を取り出す（なければ -1）
// ============================================================
int getWebParam(String query, String key) {
  int start = query.indexOf(key + "=");
  if (start == -1) return -1;
  start += key.length() + 1;
  int end = query.indexOf('&', start);
  if (end == -1) end = query.length();
  return query.substring(start, end).toInt();
}

// ============================================================
//  Web連携: ハートビート送信
// ============================================================
void sendHeartbeat() {
  WiFiClient hbWifi;
  HttpClient hbClient = HttpClient(hbWifi, serverAddress, webPort);
  hbClient.setTimeout(SERVER_CONNECT_TIMEOUT);
  String path = "/register?id=" + myID + "&type=" + myType;
  hbClient.get(path);
  hbClient.responseStatusCode();
  hbClient.stop();
}

// ============================================================
//  Web連携: /data?instrument=...&volume=...&score=... を受信して反映
//  ・armed=false（待機中）のときのみ受け付ける
//  ・instrument : 楽器ID変更
//  ・volume     : 個別音量変更
//  ・score      : 楽譜ID変更（0=リズム 1=メロディー）
// ============================================================
void handleWebRequest() {
  if (!wifiReady) return;
  if (armed) return;  // 演奏中は受け付けない

  unsigned long now = millis();
  if (now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    sendHeartbeat();
    lastHeartbeat = now;
  }

  WiFiClient webClient = webServer.available();
  if (!webClient) return;

  String requestLine = webClient.readStringUntil('\n');
  requestLine.trim();

  while (webClient.available()) {
    String line = webClient.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;
  }

  String queryString = "";
  int qStart = requestLine.indexOf('?');
  int qEnd   = requestLine.lastIndexOf(' ');
  if (qStart != -1 && qEnd > qStart) {
    queryString = requestLine.substring(qStart + 1, qEnd);
  }

  if (queryString.length() > 0) {
    int instVal = getWebParam(queryString, "instrument");
    if (instVal != -1 && instVal >= 0 && instVal <= 3) {
      MY_INSTRUMENT_ID = (uint8_t)instVal;
      MY_COLOR_ID      = (uint8_t)instVal;
    }

    int volVal = getWebParam(queryString, "volume");
    if (volVal != -1) {
      webVolume = (uint8_t)constrain(volVal, 0, 100);
    }

    int scoreVal = getWebParam(queryString, "score");
    if (scoreVal != -1 && scoreVal >= 0 && scoreVal < SCORE_COUNT) {
      MY_SCORE_ID = (uint8_t)scoreVal;
    }
  }

  webClient.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK");
  webClient.stop();
}

// ============================================================
//  setup
// ============================================================
void setup() {
  Serial.begin(921600);

#if DEBUG_MODE
  uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 3000) { delay(10); }
  Serial.println("\n[BOOT] player.ino start");
#endif

  matrix.begin();
  matrix.clear();

  if (!tcs.begin()) {
#if DEBUG_MODE
    Serial.println("[ERR] TCS34725 が見つかりません！配線を確認してください。");
    Serial.flush();
#endif
    matrix.loadFrame((const uint32_t[]){0x80402010, 0x08040201, 0x00000000});
    while (1) { delay(1000); }
  }

  // WiFi接続（タイムアウト付き。接続できなくても単体モードで起動する）
  unsigned long wifiStart = millis();
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED &&
         millis() - wifiStart < WIFI_CONNECT_TIMEOUT) {
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    httpClient.setTimeout(SERVER_CONNECT_TIMEOUT);
    String regPath = "/register?id=" + myID + "&type=" + myType;
    httpClient.get(regPath);
    int statusCode = httpClient.responseStatusCode();
    httpClient.stop();

    if (statusCode >= 200 && statusCode < 300) {
      webServer.begin();
      wifiReady = true;
    } else {
      WiFi.disconnect();
    }
  }

#if DEBUG_MODE
  Serial.println("設定: MY_INSTRUMENT_ID = " + String(MY_INSTRUMENT_ID));
#endif

  delay(200);
}

// ============================================================
//  loop
// ============================================================
void loop() {
  uint32_t now = millis();

  // 待機中のみWebからの設定変更を受け付ける
  handleWebRequest();

  // 表示中の歌詞を音長経過後に消す（非ブロッキング）
  if (lyricShowing && (int32_t)(now - lyricClearAt) >= 0) {
    matrix.clear();
    lyricShowing = false;
  }

  // 楽譜を全て演奏し終えたか
  if (noteNum >= MELODY_LENGTH) {
#if DEBUG_MODE
    Serial.println("[STOP] 楽譜を全て演奏し終えました。待機状態に戻ります。");
#endif
    noteNum    = 0;
    lyricIndex = 0;
    armed      = false;
    return;
  }

  uint16_t r, g, b, c;
  tcs.getRawData(&r, &g, &b, &c);


  bool litNow = (c >= BLINK_THRESHOLD);

  if (litNow && !inBlink) {
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
    Serial.print(blinkInterval);
    Serial.print(" ms");
    if (blinkInterval > 0) {
      int calcBpm = 30000 / blinkInterval;  // pulseBeat=60000/bpm/2 の逆算
      Serial.print("  →  受信BPM: ");
      Serial.print(calcBpm);
    }
    Serial.println();
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

        // 音データ生成（選択中の楽譜から取得）
        const ScoreData &currentScore = scoreTable[MY_SCORE_ID];

        note.instrumentID = MY_INSTRUMENT_ID;
        note.pitch        = currentScore.pitch[noteNum];

        uint16_t baseVol = map(clearMax, CLEAR_MAX_LOW, CLEAR_MAX_HIGH, 20, 100);
        baseVol = constrain(baseVol, 1, 100);
        // webVolumeを個別音量として反映（100%=そのまま、0%=無音）
        note.volume = (uint8_t)constrain(
          baseVol * currentScore.amplitude[noteNum] * webVolume / 100.0,
          0, 100
        );

        if (blinkInterval > 0) {
          uint16_t durMs = (uint16_t)(blinkInterval * currentScore.duration[noteNum]);
          note.duration = constrain(durMs, 100, 4095);
        } else {
          note.duration = 500;
        }

#if DEBUG_MODE
        const char* instName[] = {"ピアノ", "バイオリン", "フルート", "ドラム"};
        Serial.print("[SEND] 音符:" + String(noteNum));
        Serial.print(" Pitch:" + String(note.pitch));
        Serial.print(" | 楽器: " + String(instName[MY_INSTRUMENT_ID]) + "(ID=" + String(MY_INSTRUMENT_ID) + ")");
        Serial.print(" | 全体音量(LED強度→): " + String(baseVol) + "%");
        Serial.print(" × 個別音量(Web): " + String(webVolume) + "%");
        Serial.println(" = 最終Vol:" + String(note.volume));
#else
        sendNote();
#endif

        if (currentScore.lyricTiming[noteNum] > 0.0f && lyricIndex < LYRICS_LENGTH) {
          uint32_t lyricMs;
          if (blinkInterval > 0) {
            lyricMs = (uint32_t)(blinkInterval * currentScore.lyricTiming[noteNum]);
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