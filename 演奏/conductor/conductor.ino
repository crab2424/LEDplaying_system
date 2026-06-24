// ============================================================
//  light_send.ino
//  カラーLED 点滅制御 + Web連携（指揮者Arduino用）
//  - 可変抵抗でBPM(30〜240)を調整 → 8分音符間隔で点滅
//  - タクトスイッチで点滅の開始/停止を切り替え
//  - Webから bpm / volume / play を受信して反映
//  - 赤・緑・青・紫を点灯できる
//    緑: D11 (抵抗 68Ω)
//    青: D10 (抵抗 68Ω)
//    赤: D9  (抵抗 150Ω)
// ============================================================

#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include "Arduino_LED_Matrix.h"

// ============================================================
//  ★ 輪唱設定
//    PART_COLORS の順番で 1パートずつ点火していく
//    - 先頭のパートが一番早く入る
//    - 末尾のパートが一番最後に入る = 以降の継続点滅もこの色に固定
//    NUM_PARTS = 1 にすれば単独演奏（従来動作）
// ============================================================
ArduinoLEDMatrix matrix;

enum LightColor : uint8_t { LC_PURPLE = 0, LC_RED = 1, LC_BLUE = 2, LC_GREEN = 3 };

const uint8_t     NUM_PARTS              = 4;   // 1〜4
const LightColor  PART_COLORS[4]         = { LC_PURPLE, LC_RED, LC_BLUE, LC_GREEN };
// オフセット = 8拍 = 16パルス (pulseBeat は 8分音符単位)
const uint16_t    OFFSET_PULSES_PER_PART = 16;
// mode確認を行うパルス数 = NUM_PARTS * OFFSET = 4*16 = 64
const uint32_t    MODE_CHECK_PULSES = (uint32_t)NUM_PARTS * OFFSET_PULSES_PER_PART;
// 一度演奏モード時に全員が弾き終わるまでの総パルス数 = 3*16+64 = 112
// MELODY_LENGTH=64 (lyrics.h参照)
const uint32_t    TOTAL_PULSES = (uint32_t)(NUM_PARTS - 1) * OFFSET_PULSES_PER_PART + 64;

// ============================================================
//  ★ ピン設定
// ============================================================
#define PIN_GREEN    11   // 緑アノード 箱内 (抵抗 68Ω)
#define PIN_BLUE     10   // 青アノード 箱内 (抵抗 68Ω)
#define PIN_RED       9   // 赤アノード 箱内 (抵抗 150Ω)
#define PIN_GREEN2    7   // 緑アノード 外部 (抵抗 68Ω)
#define PIN_BLUE2     6   // 青アノード 外部 (抵抗 68Ω)
#define PIN_RED2      5   // 赤アノード 外部 (抵抗 150Ω)
#define SWITCH_PIN    2   // タクトスイッチ (INPUT_PULLUP)
#define ANALOG_INPUT A0   // 可変抵抗

// ============================================================
//  ★ 点灯時間の設定
// ============================================================
#define CHATTARING_TIME 50   // チャタリング防止時間 [ms]
#define BLINK_TIME      20   // 1回の点灯時間 [ms] (固定)

// ============================================================
//  ★ LED 輝度設定 (0〜255)
//    全体音量100%時の上限
// ============================================================
const int LED_BRIGHTNESS_MAX = 255;

// ============================================================
//  ★ Web連携設定
// ============================================================
const char* ssid          = "hackathon006-WPA2";
const char* pass          = "hackathon006";
const char* serverAddress = "192.168.11.3";
int         webPort       = 3000;
String      myID          = "conductor";
String      myType        = "conductor";

// ============================================================
//  グローバル変数
// ============================================================
int LEDvalue  = 0;
int bpm       = 120;
int pulseBeat = 0;

int currentVolume     = 100;                // Webから受信した全体音量 0〜100
int ledBrightness     = LED_BRIGHTNESS_MAX; // 実際の点灯強度（点灯パルス開始時のみ更新）
int pendingBrightness = LED_BRIGHTNESS_MAX; // 次の点灯開始まで保留する強度

// ツマミが実際に動かされた時だけ bpm へ反映する（Webから来た bpm を上書きしないため）
bool useWebBpm = false;       // true の間はWebのbpmを優先
int webBpmKnobSnapshot = 0;   // Web受信時のツマミ値を記録
const int KNOB_CHANGE_THRESHOLD = 15;

// シリアル表示用
unsigned long lastSerialPrint = 0;
const unsigned long SERIAL_INTERVAL = 1000;

// LEDマトリクスBPM表示用
unsigned long lastMatrixUpdate = 0;
const unsigned long MATRIX_INTERVAL = 500;  // 0.5秒ごとに更新
int lastDisplayedBpm = -1;

unsigned long lastBlink  = 0;
unsigned long lastToggle = 0;
unsigned long interval   = 0;

bool BlinkManage   = false;
bool lastSWITCH    = false;
bool nowSWITCH     = false;
bool SYSTEM_SWITCH = false;
bool lastSystemSwitch = false;

// 演奏開始からのパルス番号 (OFF→ON 立ち上がりごとに+1)
// 輪唱のパートごとの点火タイミングを決める
uint32_t   pulseIdx     = 0;
LightColor currentColor = PART_COLORS[0];

// ループモード: true=全員終了後に最初から繰り返す / false=全員終了後に停止
bool loopMode = false;

WiFiServer webServer(80);
WiFiClient wifiClient;
HttpClient httpClient = HttpClient(wifiClient, serverAddress, webPort);

const unsigned long HEARTBEAT_INTERVAL = 10000;
unsigned long lastHeartbeat = 0;

// WiFiに接続できなかった場合でも単体動作できるようにするためのフラグ
bool wifiReady = false;
const unsigned long WIFI_CONNECT_TIMEOUT = 10000; // [ms] これを超えたらWiFi無しで起動
const unsigned long SERVER_CONNECT_TIMEOUT = 2000; // [ms] サーバ未起動時に長く待たない

// ============================================================
//  LEDマトリクスに3桁BPMを表示する
//  数字は 5行 x 3列。3桁 + 桁間1列 x 2 = 11列なので12x8に収まる。
//  '1' が点灯、'0' が消灯。
// ============================================================
const char DIGIT_BITMAPS[10][5][4] = {
  { "111", "101", "101", "101", "111" }, // 0
  { "010", "110", "010", "010", "111" }, // 1
  { "111", "001", "111", "100", "111" }, // 2
  { "111", "001", "111", "001", "111" }, // 3
  { "101", "101", "111", "001", "001" }, // 4
  { "111", "100", "111", "001", "111" }, // 5
  { "111", "100", "111", "101", "111" }, // 6
  { "111", "001", "010", "100", "100" }, // 7
  { "111", "101", "111", "101", "111" }, // 8
  { "111", "101", "111", "001", "111" }, // 9
};

void drawDigit(byte bitmap[8][12], int digit, int xOffset) {
  const int yOffset = 1;
  for (int y = 0; y < 5; y++) {
    for (int x = 0; x < 3; x++) {
      bitmap[y + yOffset][x + xOffset] = (DIGIT_BITMAPS[digit][y][x] == '1') ? 1 : 0;
    }
  }
}

void showBPM(int bpm) {
  bpm = constrain(bpm, 0, 999);
  int d100 = bpm / 100;
  int d10  = (bpm / 10) % 10;
  int d1   = bpm % 10;

  byte bitmap[8][12] = { 0 };
  drawDigit(bitmap, d100, 0);
  drawDigit(bitmap, d10,  4);
  drawDigit(bitmap, d1,   8);
  matrix.renderBitmap(bitmap, 8, 12);
}

// ============================================================
//  点灯色を決定してピンに出力する関数
// ============================================================
void applyLED(LightColor c, int level) {
  switch (c) {
    case LC_PURPLE:
      analogWrite(PIN_RED,    level); analogWrite(PIN_RED2,    level);
      analogWrite(PIN_GREEN,  0);     analogWrite(PIN_GREEN2,  0);
      analogWrite(PIN_BLUE,   level); analogWrite(PIN_BLUE2,   level);
      break;
    case LC_RED:
      analogWrite(PIN_RED,    level); analogWrite(PIN_RED2,    level);
      analogWrite(PIN_GREEN,  0);     analogWrite(PIN_GREEN2,  0);
      analogWrite(PIN_BLUE,   0);     analogWrite(PIN_BLUE2,   0);
      break;
    case LC_BLUE:
      analogWrite(PIN_RED,    0);     analogWrite(PIN_RED2,    0);
      analogWrite(PIN_GREEN,  0);     analogWrite(PIN_GREEN2,  0);
      analogWrite(PIN_BLUE,   level); analogWrite(PIN_BLUE2,   level);
      break;
    case LC_GREEN:
      analogWrite(PIN_RED,    0);     analogWrite(PIN_RED2,    0);
      analogWrite(PIN_GREEN,  level); analogWrite(PIN_GREEN2,  level);
      analogWrite(PIN_BLUE,   0);     analogWrite(PIN_BLUE2,   0);
      break;
  }
}

// 現在のパルス番号 → そのパルスで出すべき色
//   pulseIdx = k * OFFSET_PULSES_PER_PART (k < NUM_PARTS) の瞬間に PART_COLORS[k] を出す
//   それ以外は「直近で点火済みのパート」のうち末尾の色を出す = 全パート点火後は固定色
LightColor colorForPulse(uint32_t pulseIdx) {
  uint32_t k = pulseIdx / OFFSET_PULSES_PER_PART;
  if (k >= NUM_PARTS) k = NUM_PARTS - 1;  // 全パート点火後は最後の色固定
  return PART_COLORS[k];
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

void enterStandaloneMode() {
  wifiReady        = false;
  SYSTEM_SWITCH    = false;
  lastSystemSwitch = false;
  Serial.println("Ready (standalone). Use switch & knob.\n");
}

// ============================================================
//  Web連携: /data?bpm=...&volume=...&play=... を受信して反映
//  ・bpm    : ツマミが動くまで保持（Webの値を優先）
//  ・volume : 次の点灯開始まで保留（点灯中の変化を防ぐ）
//  ・play   : 即時反映
// ============================================================
void handleWebRequest() {
  if (!wifiReady) return;  // WiFi未接続時は何もしない（単体動作モード）

  // heartbeat の HTTP リクエストは数百ms ブロックするため、演奏中は送らない。
  // 演奏中に送ると点滅パルスを取りこぼし、点滅回数が減って聞こえる原因になる。
  // (演奏停止中の方が時間に余裕があるので、その時にまとめて登録更新する)
  unsigned long now = millis();
  if (!SYSTEM_SWITCH && now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
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
    int bpmVal = getWebParam(queryString, "bpm");
    if (bpmVal != -1) {
      bpm = bpmVal;
      useWebBpm = true;
      webBpmKnobSnapshot = LEDvalue;  // 受信時のツマミ位置を記録
    }

    int volVal = getWebParam(queryString, "volume");
    if (volVal != -1) {
      currentVolume = volVal;
      // 点灯中に明るさが変化すると受光側のピーク検出・周期測定が乱れるため保留する
      pendingBrightness = map(currentVolume, 0, 100, 0, LED_BRIGHTNESS_MAX);
    }

    int playVal = getWebParam(queryString, "play");
    if (playVal == 1) SYSTEM_SWITCH = true;
    if (playVal == 0) SYSTEM_SWITCH = false;

    int loopVal = getWebParam(queryString, "loop");
    if (loopVal == 1) loopMode = true;
    if (loopVal == 0) loopMode = false;
  }

  webClient.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK");
  webClient.stop();
}

// ============================================================
//  setup
// ============================================================
void setup() {
  pinMode(PIN_RED,      OUTPUT);
  pinMode(PIN_GREEN,    OUTPUT);
  pinMode(PIN_BLUE,     OUTPUT);
  pinMode(PIN_RED2,     OUTPUT);
  pinMode(PIN_GREEN2,   OUTPUT);
  pinMode(PIN_BLUE2,    OUTPUT);
  pinMode(SWITCH_PIN,   INPUT_PULLUP);
  pinMode(ANALOG_INPUT, INPUT);
  Serial.begin(115200);
  matrix.begin();
  matrix.clear();

  applyLED(currentColor, 0);

  // WiFi 接続 (タイムアウト付き。接続できなくても単体モードで起動する)
  // タイマーは WiFi.begin() の前に起動する。WiFi.begin() 自体が数秒〜10秒程度
  // ブロックすることがあり、その分も WIFI_CONNECT_TIMEOUT に含めないと
  // 合計待ち時間が想定の倍になる。
  Serial.print("Connecting to WiFi...");
  unsigned long wifiStart = millis();
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED &&
         millis() - wifiStart < WIFI_CONNECT_TIMEOUT) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");

    Serial.print("Waiting for IP...");
    unsigned long ipStart = millis();
    while (WiFi.localIP() == IPAddress(0, 0, 0, 0) &&
           millis() - ipStart < WIFI_CONNECT_TIMEOUT) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
    Serial.print("Arduino IP: ");
    Serial.println(WiFi.localIP());

    // サーバーへ自己登録
    Serial.println("Registering to server...");
    httpClient.setTimeout(SERVER_CONNECT_TIMEOUT);
    String regPath = "/register?id=" + myID + "&type=" + myType;
    httpClient.get(regPath);
    int statusCode = httpClient.responseStatusCode();
    httpClient.stop();

    if (statusCode >= 200 && statusCode < 300) {
      webServer.begin();
      wifiReady = true;
      Serial.println("Ready. Waiting for data...\n");
    } else {
      Serial.print("Server unavailable. HTTP status: ");
      Serial.println(statusCode);
      WiFi.disconnect();
      enterStandaloneMode();
    }
  } else {
    Serial.println("\nWiFi connect failed. Standalone mode.");
    enterStandaloneMode();
  }
}

// ============================================================
//  loop
// ============================================================
void loop() {
  // Web からの bpm・volume・play を受信
  handleWebRequest();

  LEDvalue  = analogRead(ANALOG_INPUT);
  nowSWITCH = !digitalRead(SWITCH_PIN);

  // WebのbpmはツマミがKNOB_CHANGE_THRESHOLD以上動くまで優先する
  if (useWebBpm) {
    if (abs(LEDvalue - webBpmKnobSnapshot) > KNOB_CHANGE_THRESHOLD) {
      useWebBpm = false;  // ツマミが動いたのでツマミ制御に戻す
    }
  }
  if (!useWebBpm) {
    bpm = map(LEDvalue, 0, 1023, 30, 240);
  }

  // シリアルモニタに現在の状態を1秒ごとに表示
  unsigned long now = millis();
  if (now - lastSerialPrint >= SERIAL_INTERVAL) {
    lastSerialPrint = now;
    Serial.print("BPM: ");       Serial.print(bpm);
    Serial.print("  音量: ");    Serial.print(currentVolume); Serial.print("%");
    Serial.print("  演奏: ");    Serial.println(SYSTEM_SWITCH ? "再生中" : "停止中");
  }

  pulseBeat = 60000 / bpm / 2;  // 8分音符間隔 [ms]

  // BPMが変わったか0.5秒経過したら表示更新
  if (bpm != lastDisplayedBpm || now - lastMatrixUpdate >= MATRIX_INTERVAL) {
    showBPM(bpm);
    lastDisplayedBpm = bpm;
    lastMatrixUpdate = now;
  }

  // タクトスイッチ: 押した瞬間を検出 (チャタリング防止付き)
  if (!lastSWITCH && nowSWITCH && (millis() - lastToggle > CHATTARING_TIME)) {
    SYSTEM_SWITCH = !SYSTEM_SWITCH;
    lastToggle = millis();
  }

  // 演奏停止→開始の立ち上がりで輪唱シーケンスを先頭にリセット
  if (SYSTEM_SWITCH && !lastSystemSwitch) {
    pulseIdx     = 0;
    currentColor = PART_COLORS[0];
    BlinkManage  = false;
    lastBlink    = millis();
  }
  lastSystemSwitch = SYSTEM_SWITCH;

  if (SYSTEM_SWITCH) {
    interval = BlinkManage ? BLINK_TIME : (pulseBeat - BLINK_TIME);

    if (millis() - lastBlink >= interval) {
      BlinkManage = !BlinkManage;
      lastBlink   = millis();
      // OFF→ON に切り替わる瞬間だけ保留中の明るさと点灯色を更新する
      // (点灯中に色や輝度が変わると受光側のピーク検出・周期測定が乱れる)
      if (BlinkManage) {
        ledBrightness = pendingBrightness;
        currentColor  = colorForPulse(pulseIdx);
        pulseIdx++;

        // 全演奏者が弾き終わるパルス数に達したら
        // 64パルス（全パート点火完了）でmode確認
        if (pulseIdx >= MODE_CHECK_PULSES && loopMode) {
          pulseIdx     = 0;            // 紫から再スタート
          currentColor = PART_COLORS[0];
        }
        // 112パルス（全員演奏終了）で停止（mode=0のみ）
        if (pulseIdx >= TOTAL_PULSES && !loopMode) {
          SYSTEM_SWITCH = false;
        }
      }
    }
    applyLED(currentColor, BlinkManage ? ledBrightness : 0);
  } else {
    applyLED(currentColor, 0);
    BlinkManage = false;
  }

  lastSWITCH = nowSWITCH;
  delay(1);
}
