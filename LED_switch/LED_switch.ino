#define TESTLED_PIN 6
#define SWITCH_PIN 2
#define ANALOG_INPUT A0
#define CHATTARING_TIME 50
#define BLINK_TIME 20
int LEDvalue = 0;
int bpm;
int pulseBeat;
int SYSTEM_LED;

unsigned long lastBlink = 0;   // 前回点滅を切り替えた時刻
unsigned long lastToggle = 0;  // チャタリング防止用変数
unsigned long interval;
bool BlinkManage = false;  //点滅切り替え用変数

bool lastSWITCH = false;  // タクトスイッチの変化管理変数
bool nowSWITCH = false;   // タクトスイッチの変化管理変数
bool SYSTEM_SWITCH = false;


void setup() {
  pinMode(TESTLED_PIN, OUTPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);  //　ノイズ対策
  pinMode(ANALOG_INPUT, INPUT);
  Serial.begin(9600);
}

void loop() {
  LEDvalue = analogRead(ANALOG_INPUT);   // 可変抵抗はBPMに反映
  nowSWITCH = !digitalRead(SWITCH_PIN);  // スイッチは別回路なら別ピンで読む

  bpm = map(LEDvalue, 0, 1023, 30, 240);  //BPMを30~240に設定
  Serial.println(bpm);
  pulseBeat = 60000 / bpm / 2;            //8分間隔(1/2)で点滅

  if (!lastSWITCH && nowSWITCH && (millis() - lastToggle > CHATTARING_TIME)) {  //タクトスイッチ回路のOFFがONになった，つまり押した瞬間を判定，チャタリングの分岐も行う
    SYSTEM_SWITCH = !SYSTEM_SWITCH;                                             //システム全体のONOFF切り替え
    lastToggle = millis();
  }

  if (SYSTEM_SWITCH) {  // ONの時のみ点滅
    if (BlinkManage) {
      interval = BLINK_TIME;  // 点灯中は短く（例：30ms 固定）
    } else {
      interval = pulseBeat - BLINK_TIME;  // 消灯はテンポの残り時間
    }

    if (millis() - lastBlink >= interval) {
      BlinkManage = !BlinkManage;
      lastBlink = millis();
    }
    analogWrite(TESTLED_PIN, BlinkManage ? 32 : 0);  //ここでLEDの強さを変更できる
  } else {
    analogWrite(TESTLED_PIN, 0);  // OFF時は消灯
    BlinkManage = false;
  }
  lastSWITCH = nowSWITCH;
  delay(1);  //スイッチ切り替えを常時判定するため高速
}