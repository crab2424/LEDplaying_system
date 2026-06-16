import ddf.minim.*;
import ddf.minim.ugens.*;
import ddf.minim.analysis.*;

Minim       minim;
AudioOutput out;
FFT         fft;
float[]     smoothSpec;
String      pitch = "A4";

HashMap<Character, String> keyMap = new HashMap<Character, String>();

// ============================================================
// ViolinInstrument : 加算合成（SINE 波）+ ADSR + ビブラート
//
// 修正点一覧:
//   [1] 倍音・LFO の波形 SAW → SINE
//       加算合成は純音（正弦波）を重ねることで成立する．
//       SAW 波はそれ自体が多数の倍音を含むため，
//       意図しない倍音が大量に混入し harmonicAmps が無効になる．
//
//   [2] 倍音振幅比を実測に近い滑らかな単調減衰に修正
//       旧: 3倍音=0.90（基音に匹敵）, 6倍音=0.005 → 7倍音=0.20（逆転）
//       新: 2〜4倍音を比較的強め，高次ほど単調に減衰
//
//   [3] 4段階 ADSR を実装（旧: Attack のみ）
//       update() 内でフェーズ遷移を管理:
//         0=ATTACK → 1=DECAY → 2=SUSTAIN → 3=RELEASE → 4=DONE
//
//   [4] Release 後にのみ unpatch（旧: noteOff() で即 unpatch）
//       旧実装では activate(RELEASE,...) 直後に unpatch していたため
//       フェードアウトが機能していなかった（0ms で音が切れていた）．
//       修正後は update() 内で RELEASE 秒経過後に unpatch する．
//
//   [5] ビブラートを全倍音へ適用
//       旧: 基音のみ freqSum を patch（簡易版）
//       新: freqSum → Multiplier(k+1) → harmonics[k].frequency
//          により全倍音が比率的に変調される
//
//   [6] ビブラート遅延・フェードインを正しく実装
//       旧: noteOn() 全期間にわたって線形増大するだけ（遅延なし）
//       新: update() 内で VIB_DELAY 秒経過後に activate() し，
//          VIB_FADEIN 秒かけて最大深さまでフェードイン
// ============================================================
class ViolinInstrument implements Instrument
{
  // --- 倍音オシレータ（SINE 波）---
  Oscil[]      harmonics;
  Line[]       ampEnvs;
  // [5] 各倍音の周波数スケーラ: freqSum × (k+1) → harmonics[k].frequency
  Multiplier[] freqScalers;

  // --- ビブラート系 UGen ---
  Oscil      vibrato;         // [1] SINE LFO
  Constant   baseFreq;        // 基準周波数の定数 UGen
  Multiplier vibratoScaler;   // LFO 出力 × 深さ（初期 = 0）
  Line       vibratoDepthEnv; // ビブラート深さのエンベロープ
  Summer     freqSum;         // baseFreq + vibratoOffset

  float maxAmp;
  float freq;

  // --- [3] フェーズ管理 ---
  // -1=未開始, 0=ATTACK, 1=DECAY, 2=SUSTAIN, 3=RELEASE, 4=DONE
  int   phase;
  float noteOnMs;   // noteOn 時の millis()
  float noteOffMs;  // noteOff 時の millis()
  float envFactor;  // 現在エンベロープレベル [0, 1]（Release 開始時の振幅計算に使用）

  // --- [6] ビブラート管理 ---
  boolean vibFadeInStarted;
  float   vibFadeInStartMs; // フェードイン開始時の millis()

  // --- [2] 倍音振幅比（ヴァイオリン実測に基づく近似値）---
  //   ・基音が最大，高次ほど単調に減衰
  //   ・2〜4 倍音が比較的強い弦楽器特有のスペクトル
  //   ・6→7 倍音の逆転を解消
  static final int NUM_HARMONICS = 40;
   float[] harmonicAmps = {
    0.22,
    0.12,
    0.066,
    0.096,
    0.085, //5
    0.025,
    0.02, 
    0.011, 
    0.003, 
    0.0022, //10
    0.002, 
    0.003, 
    0.0025, 
    0.0012, 
    0.001, //15
    0.0005, 
    0.0007, 
    0.0001, 
    0.00009, 
    0.00008, //20
    0.0, 0.0003, 0.0004, 0.0005, 0.0006, // 21-25
    0.00016, 0.0018, 0.0019, 0.002, 0.0022, // 26-30
    0.0019, 0.0016, 0.0014, 0.0012, 0.002, // 31-35
    0.003, 0.004, 0.0008, 0.0004, 0.0003 // 36-40

  };

  float[] scaledAmps; // 正規化済み倍音振幅（コンストラクタで計算）

  // --- [3] ADSR パラメータ ---
  static final float ATTACK   = 0.20;  // s：弓が乗り音量が立ち上がる区間
  static final float DECAY    = 0.40;  // s：最大音量から安定音量へ落ち着く区間
  static final float SUSTAIN  = 0.90;  // peak に対する比率
  static final float RELEASE  = 0.25;  // s：弓が離れ音量が減衰する区間

  // --- [1][6] ビブラートパラメータ ---
  static final float VIB_RATE      = 5.5;   // Hz
  static final float VIB_DEPTH_MAX = 0.012; // 変調深さ（比率）
  static final float VIB_DELAY     = 0.10;  // 発音後，ビブラート開始までの遅延 (s)
  static final float VIB_FADEIN    = 0.25;  // 最大深さまでのフェードイン時間 (s)

  // ============================================================
  ViolinInstrument(float frequency, float maxAmp)
  {
    this.freq   = frequency;
    this.maxAmp = maxAmp;
    phase       = -1;
    envFactor   = 0;
    vibFadeInStarted = false;

    // 振幅の正規化（全倍音の合計が maxAmp になるよう）
    float ampSum = 0;
    for (float a : harmonicAmps) ampSum += a;
    scaledAmps = new float[NUM_HARMONICS];
    for (int k = 0; k < NUM_HARMONICS; k++) {
      scaledAmps[k] = (harmonicAmps[k] / ampSum) * maxAmp;
    }

    // ---- [1] ビブラート LFO : SINE 波で構築 ----
    vibrato         = new Oscil(VIB_RATE, 1.0, Waves.SINE); // SAW → SINE
    baseFreq        = new Constant(frequency);
    vibratoScaler   = new Multiplier(0);    // 初期深さ = 0
    vibratoDepthEnv = new Line();
    freqSum         = new Summer();

    vibrato.patch(vibratoScaler);
    vibratoDepthEnv.patch(vibratoScaler.amplitude);
    baseFreq.patch(freqSum);
    vibratoScaler.patch(freqSum);

    // ---- [1][5] 倍音オシレータ群 : SINE 波 + 全倍音にビブラート適用 ----
    harmonics   = new Oscil[NUM_HARMONICS];
    ampEnvs     = new Line[NUM_HARMONICS];
    freqScalers = new Multiplier[NUM_HARMONICS];

    for (int k = 0; k < NUM_HARMONICS; k++) {
      harmonics[k] = new Oscil(frequency * (k + 1), 0, Waves.SINE); // SAW → SINE
      ampEnvs[k]   = new Line();
      ampEnvs[k].patch(harmonics[k].amplitude);

      // [5] 倍音 k の周波数 = (k+1) × (baseFreq + vibratoOffset)
      //     freqSum → Multiplier(k+1) → harmonics[k].frequency
      freqScalers[k] = new Multiplier(k + 1);
      freqSum.patch(freqScalers[k]);
      freqScalers[k].patch(harmonics[k].frequency);
    }
  }

  // ============================================================
  // noteOn : 弓を弦に当てた瞬間
  // ============================================================
  void noteOn(float duration)
  {
    noteOnMs         = millis();
    phase            = 0; // ATTACK 開始
    envFactor        = 0;
    vibFadeInStarted = false;

    // ATTACK フェーズ: 0 → peak
    for (int k = 0; k < NUM_HARMONICS; k++) {
      ampEnvs[k].activate(ATTACK, 0, scaledAmps[k]);
      harmonics[k].patch(out);
    }
    // ビブラートは update() 内で VIB_DELAY 秒後に開始
  }

  // ============================================================
  // noteOff : 弓が弦から離れる瞬間 → Release フェーズへ移行
  // [4] ここでは unpatch しない（update() 内で RELEASE 後に行う）
  // ============================================================
  void noteOff()
  {
    if (phase >= 3) return; // 既にリリース中
    noteOffMs = millis();
    phase     = 3; // RELEASE

    float currentVibDepth = computeVibratoDepth();

    for (int k = 0; k < NUM_HARMONICS; k++) {
      // [4] 現在のエンベロープレベルからゼロへ滑らかに減衰
      ampEnvs[k].activate(RELEASE, scaledAmps[k] * envFactor, 0);
    }
    // ビブラートも RELEASE に合わせて消す
    vibratoDepthEnv.activate(RELEASE * 0.6, currentVibDepth, 0);
  }

  // ============================================================
  // update : draw() から毎フレーム呼び出す
  // [3] ADSR フェーズ遷移
  // [4] Release 完了後の unpatch
  // [6] ビブラート遅延・フェードイン
  // ============================================================
  void update()
  {
    if (phase < 0 || phase == 4) return;

    float elapsed = (millis() - noteOnMs) / 1000.0;

    // --- [3] ADSR フェーズ遷移 ---
    if (phase == 0 && elapsed >= ATTACK) {
      // ATTACK → DECAY
      phase     = 1;
      envFactor = 1.0;
      for (int k = 0; k < NUM_HARMONICS; k++) {
        ampEnvs[k].activate(DECAY, scaledAmps[k], scaledAmps[k] * SUSTAIN);
      }
    } else if (phase == 1 && elapsed >= ATTACK + DECAY) {
      // DECAY → SUSTAIN（Line は最終値を保持するので追加操作不要）
      phase     = 2;
      envFactor = SUSTAIN;
    } else if (phase == 3) {
      float releaseElapsed = (millis() - noteOffMs) / 1000.0;
      if (releaseElapsed >= RELEASE + 0.05) {
        // [4] RELEASE 完了 → unpatch して DONE へ
        phase = 4;
        doUnpatch();
        return;
      }
    }

    // --- envFactor の更新（noteOff 時に正確な振幅から Release を開始するため）---
    if (phase == 0) {
      envFactor = constrain(elapsed / ATTACK, 0.0, 1.0);
    } else if (phase == 1) {
      float t = constrain((elapsed - ATTACK) / DECAY, 0.0, 1.0);
      envFactor = 1.0 - (1.0 - SUSTAIN) * t;
    }
    // phase 2/3: envFactor は固定のため更新不要

    // --- [6] ビブラート遅延後にフェードイン ---
    if (!vibFadeInStarted && elapsed >= VIB_DELAY && phase <= 2) {
      vibFadeInStarted = true;
      vibFadeInStartMs = millis();
      vibratoDepthEnv.activate(VIB_FADEIN, 0, freq * VIB_DEPTH_MAX);
    }
  }

  // ビブラートの現在深さを推定（noteOff 時の連続性のため）
  float computeVibratoDepth()
  {
    if (!vibFadeInStarted) return 0;
    float vibElapsed = (millis() - vibFadeInStartMs) / 1000.0;
    return min(vibElapsed / VIB_FADEIN, 1.0) * freq * VIB_DEPTH_MAX;
  }

  boolean isDone() {
    return phase == 4;
  }

  void doUnpatch()
  {
    for (int k = 0; k < NUM_HARMONICS; k++) {
      harmonics[k].unpatch(out);
    }
  }
}

// ============================================================
// setup
// ============================================================
void setup()
{
  size(800, 400);
  minim = new Minim(this);
  out   = minim.getLineOut();

  // FFT の初期化
  fft = new FFT(out.bufferSize(), out.sampleRate());
  fft.window(FFT.HANN);
  smoothSpec = new float[fft.specSize()];

  keyMap.put('a', "Bb2");
  keyMap.put('z', "C3");
  keyMap.put('s', "C#3");
  keyMap.put('x', "D3");
  keyMap.put('d', "Eb3");
  keyMap.put('c', "E3");
  keyMap.put('v', "F3");
  keyMap.put('g', "F#3");
  keyMap.put('b', "G3");
  keyMap.put('h', "Ab3");
  keyMap.put('n', "A3");
  keyMap.put('j', "Bb3");
  keyMap.put('m', "B3");
  keyMap.put(',', "C4");
  keyMap.put('l', "Db4");
  keyMap.put('.', "D4");
  keyMap.put(';', "Eb4");
  keyMap.put('/', "E4");
  keyMap.put('q', "F4");
  keyMap.put('2', "F#4");
  keyMap.put('w', "G4");
  keyMap.put('3', "Ab4");
  keyMap.put('e', "A4");
  keyMap.put('4', "Bb4");
  keyMap.put('r', "B4");
  keyMap.put('t', "C5");
  keyMap.put('6', "C#5");
  keyMap.put('y', "D5");
  keyMap.put('7', "Eb5");
  keyMap.put('u', "E5");
  keyMap.put('i', "F5");
  keyMap.put('9', "F#5");
  keyMap.put('o', "G5");
  keyMap.put('0', "Ab5");
  keyMap.put('p', "A5");
  keyMap.put('-', "Bb5");
  keyMap.put('@', "B5");
  keyMap.put('[', "C6");
}

// ============================================================
// 演奏状態管理
// ============================================================
ViolinInstrument              currentInstrument    = null;
char                          playingKey           = 0;
// [4] Release フェーズ中のインスタンスを保持するリスト
//     update() でフェーズ完了を監視し，DONE になったら削除する
ArrayList<ViolinInstrument>   releasingInstruments = new ArrayList<ViolinInstrument>();

// ============================================================
// keyPressed : キーを押した瞬間 → 発音開始
// ============================================================
void keyPressed()
{
  if (key == playingKey) return;

  if (keyMap.containsKey(key)) {
    if (currentInstrument != null) {
      currentInstrument.noteOff();
      releasingInstruments.add(currentInstrument); // Release 完了を update() で監視
      currentInstrument = null;
    }

    playingKey = key;
    pitch      = keyMap.get(key);

    currentInstrument = new ViolinInstrument(
      Frequency.ofPitch(pitch).asHz(),
      0.1f
      );
    currentInstrument.noteOn(9999.0);
  }
}

// ============================================================
// keyReleased : キーを離した瞬間 → Release フェーズへ
// ============================================================
void keyReleased()
{
  if (key == playingKey && currentInstrument != null) {
    currentInstrument.noteOff();
    releasingInstruments.add(currentInstrument); // Release 完了を update() で監視
    currentInstrument = null;
    playingKey        = 0;
  }
}

// ============================================================
// draw : FFT スペクトラムアナライザ表示 + 演奏状態更新
// ============================================================
void draw()
{
  // ---------------------------------------------------------
  // 0. 演奏状態の更新（毎フレーム）
  // ---------------------------------------------------------
  // 現在発音中のインスタンスの ADSR 遷移・ビブラートを更新
  if (currentInstrument != null) {
    currentInstrument.update();
  }
  // [4] Release 中のインスタンスを更新し，完了したものを削除
  for (int i = releasingInstruments.size() - 1; i >= 0; i--) {
    ViolinInstrument inst = releasingInstruments.get(i);
    inst.update();
    if (inst.isDone()) {
      releasingInstruments.remove(i);
    }
  }

  background(10, 10, 20);

  // ---------------------------------------------------------
  // 1. FFT 計算（左チャンネルのバッファを使用）
  // ---------------------------------------------------------
  fft.forward(out.left);

  // ---------------------------------------------------------
  // 2. 表示パラメータ
  // ---------------------------------------------------------
  int   specSize   = fft.specSize();
  float maxFreq    = 6000.0;
  int   maxBin     = min(specSize, (int)(maxFreq / (out.sampleRate() / out.bufferSize())) + 1);

  float plotLeft   = 60;
  float plotRight  = width - 20;
  float plotBottom = height - 50;
  float plotTop    = 20;
  float plotW      = plotRight - plotLeft;
  float plotH      = plotBottom - plotTop;

  float smoothing  = 0.75;

  // ---------------------------------------------------------
  // 3. グリッド・軸の描画
  // ---------------------------------------------------------
  int[]    dbLines  = {0, -6, -12, -24, -36, -48};
  String[] dbLabels = {"0dB", "-6", "-12", "-24", "-36", "-48"};
  textSize(10);
  textAlign(RIGHT, CENTER);
  for (int di = 0; di < dbLines.length; di++) {
    float db = dbLines[di];
    float y  = map(db, 0, -60, plotTop, plotBottom);
    if (y < plotTop || y > plotBottom) continue;
    stroke(db == 0 ? color(180, 180, 180, 200) : color(60, 60, 90, 180));
    strokeWeight(1);
    line(plotLeft, y, plotRight, y);
    fill(160, 160, 200);
    noStroke();
    text(dbLabels[di], plotLeft - 4, y);
  }

  int[] freqMarkers = {100, 200, 500, 1000, 2000, 4000, 6000};
  textAlign(CENTER, TOP);
  for (int fq : freqMarkers) {
    if (fq > maxFreq) continue;
    float x = plotLeft + plotW * log(fq / 20.0) / log(maxFreq / 20.0);
    stroke(60, 60, 90, 150);
    strokeWeight(1);
    line(x, plotTop, x, plotBottom);
    fill(160, 160, 200);
    noStroke();
    String label = fq >= 1000 ? (fq / 1000) + "k" : str(fq);
    text(label + "Hz", x, plotBottom + 5);
  }

  stroke(150, 150, 200);
  strokeWeight(1);
  noFill();
  rect(plotLeft, plotTop, plotW, plotH);

  // ---------------------------------------------------------
  // 4. スペクトルバーの描画
  // ---------------------------------------------------------
  int   numBars = 120;
  float barW    = plotW / numBars;

  for (int b = 0; b < numBars; b++) {
    float freqLow  = 20.0 * pow(maxFreq / 20.0, (float) b      / numBars);
    float freqHigh = 20.0 * pow(maxFreq / 20.0, (float)(b + 1) / numBars);

    float binHz   = out.sampleRate() / out.bufferSize();
    int   binLow  = max(0, (int)(freqLow  / binHz));
    int   binHigh = min(maxBin-1, (int)(freqHigh / binHz));

    float ampSum = 0;
    int   count  = 0;
    for (int bi = binLow; bi <= binHigh; bi++) {
      ampSum += fft.getBand(bi);
      count++;
    }
    float ampAvg = (count > 0) ? ampSum / count : 0;

    float db = 20.0 * log(max(ampAvg, 1e-6)) / log(10.0);
    db = constrain(db, -60, 0);

    smoothSpec[b] = smoothing * smoothSpec[b] + (1.0 - smoothing) * db;
    db = smoothSpec[b];

    float barH = map(db, -60, 0, 0, plotH);
    float x    = plotLeft + b * barW;
    float y    = plotBottom - barH;

    float hue = map(b, 0, numBars, 200, 0);
    colorMode(HSB, 360, 100, 100, 100);
    float brightness = map(db, -60, 0, 30, 100);
    fill(hue, 80, brightness, 90);
    noStroke();
    rect(x + 1, y, barW - 2, barH);

    fill(hue, 40, 100, 80);
    rect(x + 1, y, barW - 2, 2);

    colorMode(RGB, 255);
  }

  // ---------------------------------------------------------
  // 5. 現在のピッチ名・周波数の表示
  // ---------------------------------------------------------
  float currentHz = Frequency.ofPitch(pitch).asHz();
  if (currentHz > 0 && currentHz < maxFreq) {
    float xPitch = plotLeft + plotW * log(currentHz / 20.0) / log(maxFreq / 20.0);
    stroke(255, 220, 80, 180);
    strokeWeight(1.5);
    line(xPitch, plotTop, xPitch, plotBottom);
    fill(255, 220, 80);
    noStroke();
    textAlign(LEFT, BOTTOM);
    textSize(11);
    text(pitch + "  " + nf(currentHz, 0, 1) + " Hz", xPitch + 4, plotTop + 14);
  }

  // タイトル
  fill(200, 200, 255);
  textAlign(LEFT, TOP);
  textSize(13);
  text("Violin Spectrum Analyzer", plotLeft, 3);
}
