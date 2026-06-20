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

// ============================================================
//  ★ 点灯色の設定
//    光らせたい色の行だけコメントアウトを外してください
//    (複数同時に有効にすると紫など混色になります)
// ============================================================
#define COLOR_RED     // 赤を点灯する
//#define COLOR_GREEN   // 緑を点灯する
//#define COLOR_BLUE    // 青を点灯する
//#define COLOR_PURPLE  // 紫を点灯する (赤+青 同時点灯)

// ============================================================
//  ★ ピン設定
// ============================================================
#define PIN_GREEN    11   // 緑アノード (抵抗 68Ω)
#define PIN_BLUE     10   // 青アノード (抵抗 68Ω)
#define PIN_RED       9   // 赤アノード (抵抗 150Ω)
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

unsigned long lastBlink  = 0;
unsigned long lastToggle = 0;
unsigned long interval   = 0;

bool BlinkManage   = false;
bool lastSWITCH    = false;
bool nowSWITCH     = false;
bool SYSTEM_SWITCH = false;

WiFiServer webServer(80);
WiFiClient wifiClient;
HttpClient httpClient = HttpClient(wifiClient, serverAddress, webPort);

const unsigned long HEARTBEAT_INTERVAL = 10000;
unsigned long lastHeartbeat = 0;

// ============================================================
//  点灯色を決定してピンに出力する関数
// ============================================================
void applyLED(int level) {
#if defined(COLOR_PURPLE)
  analogWrite(PIN_RED,   level);
  analogWrite(PIN_GREEN, 0);
  analogWrite(PIN_BLUE,  level);
#elif defined(COLOR_RED)
  analogWrite(PIN_RED,   level);
  analogWrite(PIN_GREEN, 0);
  analogWrite(PIN_BLUE,  0);
#elif defined(COLOR_GREEN)
  analogWrite(PIN_RED,   0);
  analogWrite(PIN_GREEN, level);
  analogWrite(PIN_BLUE,  0);
#elif defined(COLOR_BLUE)
  analogWrite(PIN_RED,   0);
  analogWrite(PIN_GREEN, 0);
  analogWrite(PIN_BLUE,  level);
#else
  analogWrite(PIN_RED,   0);
  analogWrite(PIN_GREEN, 0);
  analogWrite(PIN_BLUE,  0);
#endif
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
  String path = "/register?id=" + myID + "&type=" + myType;
  hbClient.get(path);
  hbClient.responseStatusCode();
  hbClient.stop();
}

// ============================================================
//  Web連携: /data?bpm=...&volume=...&play=... を受信して反映
//  ・bpm    : ツマミが動くまで保持（Webの値を優先）
//  ・volume : 次の点灯開始まで保留（点灯中の変化を防ぐ）
//  ・play   : 即時反映
// ============================================================
void handleWebRequest() {
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
  pinMode(SWITCH_PIN,   INPUT_PULLUP);
  pinMode(ANALOG_INPUT, INPUT);
  Serial.begin(9600);

  applyLED(0);

  // WiFi 接続
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected!");

  Serial.print("Waiting for IP...");
  while (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Arduino IP: ");
  Serial.println(WiFi.localIP());

  // サーバーへ自己登録
  Serial.println("Registering to server...");
  String regPath = "/register?id=" + myID + "&type=" + myType;
  httpClient.get(regPath);
  httpClient.responseStatusCode();
  httpClient.stop();

  webServer.begin();
  Serial.println("Ready. Waiting for data...\n");
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

  // タクトスイッチ: 押した瞬間を検出 (チャタリング防止付き)
  if (!lastSWITCH && nowSWITCH && (millis() - lastToggle > CHATTARING_TIME)) {
    SYSTEM_SWITCH = !SYSTEM_SWITCH;
    lastToggle = millis();
  }

  if (SYSTEM_SWITCH) {
    interval = BlinkManage ? BLINK_TIME : (pulseBeat - BLINK_TIME);

    if (millis() - lastBlink >= interval) {
      BlinkManage = !BlinkManage;
      lastBlink   = millis();
      // OFF→ON に切り替わる瞬間だけ保留中の明るさを反映する（点灯中の変化を防ぐ）
      if (BlinkManage) {
        ledBrightness = pendingBrightness;
      }
    }
    applyLED(BlinkManage ? ledBrightness : 0);
  } else {
    applyLED(0);
    BlinkManage = false;
  }

  lastSWITCH = nowSWITCH;
  delay(1);
}
