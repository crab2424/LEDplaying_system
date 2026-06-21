import processing.serial.*;
import ddf.minim.*;
import ddf.minim.ugens.*;
import ddf.minim.effects.*;

Serial port;
Minim minim;
AudioOutput out;

// ヴァイオリンは独自にupdate()を呼ぶ必要があるためリストで管理
ArrayList<ViolinInstrument> activeViolins = new ArrayList<ViolinInstrument>();

// 画面表示用データ
int currentPitch = 0;
int currentVolume = 0;
int currentDuration = 0;
int currentInstId = 0;

void setup() {
  size(600, 400);
  
  // 日本語文字化け対策
  PFont font = createFont("Hiragino Sans", 18);
  textFont(font);
  
  minim = new Minim(this);
  out = minim.getLineOut();
  out.setTempo(60);

  println("=== 利用可能なシリアルポート ===");
  String[] portNames = Serial.list();
  for (int i = 0; i < portNames.length; i++) {
    println("[" + i + "] " + portNames[i]);
  }

  if (portNames.length > 0) {
    try {
      // 適切なポートを指定してください (ここでは最後のポートを選択しています)
      String portName = portNames[portNames.length - 1]; 
      println("\n接続するポート: " + portName);
      // 115200 推奨 (player.ino 側と揃えること)
      // 921600 では UNO R4 WiFi で取りこぼしが発生し、4Byte パケットが
      // ズレて vol=0 等の異常値になる → 音が鳴らない原因になる
      port = new Serial(this, portName, 115200);
      port.clear();               
      println("接続成功！");
    } catch (Exception e) {
      println("エラー: " + e.getMessage());
    }
  } else {
    println("シリアルポートが見つかりません。デモモードで動作します（キーボードA〜Kで発音）。");
  }
}

void draw() {
  background(30);
  fill(255);
  textSize(18);
  text("かえるのうた - Color Sensor Music Player", 20, 30);
  
  textSize(12);
  fill(180);
  text("紫→Piano  赤→Violin  青→Flute  緑→Snare", 20, 52);
  text("光量→音量  点滅間隔→テンポ  立ち上がり→スタート", 20, 68);
  
  textSize(15);
  fill(255);
  
  // ピッチ表示 (クロマチック音名)
  String[] chromaticNames = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  String noteName = "?";
  if (currentPitch >= 0 && currentPitch <= 36) {
    int octave = 3 + currentPitch / 12;  // C3=0 なのでオクターブ3から
    int noteIdx = currentPitch % 12;
    noteName = chromaticNames[noteIdx] + octave;
  }
  text("Pitch: " + currentPitch + " (" + noteName + ")", 20, 100);
  
  // 音量表示 (← 光量ピーク)
  text("Volume: " + currentVolume + "  ← 光量(clearMax)", 20, 125);
  
  // 発音時間表示 (← 点滅間隔)
  text("Duration: " + currentDuration + " ms  ← 点滅間隔", 20, 150);
  
  // 楽器表示 (← 色)
  String instName = "";
  String colorName = "";
  switch(currentInstId) {
    case 0: instName = "Piano";  colorName = "紫"; fill(200, 100, 255); break;
    case 1: instName = "Violin"; colorName = "赤"; fill(255, 100, 100); break;
    case 2: instName = "Flute";  colorName = "青"; fill(100, 150, 255); break;
    case 3: instName = "Snare";  colorName = "緑"; fill(100, 255, 100); break;
  }
  text("Instrument: " + instName + "  ← " + colorName + "色", 20, 175);
  fill(255);

  // ヴァイオリンのupdate処理 (ADSRのフェーズ遷移やビブラート更新用)
  for (int i = activeViolins.size() - 1; i >= 0; i--) {
    ViolinInstrument inst = activeViolins.get(i);
    inst.update();
    if (inst.isDone()) {
      activeViolins.remove(i);
    }
  }
  
  // 波形表示 (ウィンドウ幅に収まるよう制限)
  stroke(255, 100);
  int waveLen = min(out.bufferSize() - 1, width - 1);
  for(int i = 0; i < waveLen; i++) {
    line(i, 280 + out.left.get(i)*50, i+1, 280 + out.left.get(i+1)*50);
    line(i, 350 + out.right.get(i)*50, i+1, 350 + out.right.get(i+1)*50);
  }
}

// デモ用キーボード操作 (Arduinoがない場合用)
void keyPressed() {
  int p = -1;
  if (key == 'a' || key == 'A') p = 0;
  if (key == 's' || key == 'S') p = 1;
  if (key == 'd' || key == 'D') p = 2;
  if (key == 'f' || key == 'F') p = 3;
  if (key == 'g' || key == 'G') p = 4;
  if (key == 'h' || key == 'H') p = 5;
  if (key == 'j' || key == 'J') p = 6;
  if (key == 'k' || key == 'K') p = 7;
  
  if (p != -1) {
    // ランダムな楽器で発音デモ
    playNoteFromData(p, 80, 500, (int)random(4));
  }
}

void serialEvent(Serial p) {
  // データが溜まりすぎている（遅延している）場合はバッファをリセットしてズレを直す
  if (p.available() > 32) {
    p.clear();
    return;
  }

  // 4バイト（1パケット）揃っている間は全て処理する
  while (p.available() >= 4) {
    byte[] inBuf = new byte[4];
    int len = p.readBytes(inBuf);
    if(len != 4) continue;

    // デバッグ: 受信バイト列を表示
    println(
      "受信buf: " +
      binary(inBuf[0] & 0xFF, 8) + " " +
      binary(inBuf[1] & 0xFF, 8) + " " +
      binary(inBuf[2] & 0xFF, 8) + " " +
      binary(inBuf[3] & 0xFF, 8)
    );

  long data =
    ((long)(inBuf[0] & 0xFF) << 24) |
    ((long)(inBuf[1] & 0xFF) << 16) |
    ((long)(inBuf[2] & 0xFF) << 8 ) |
    (long)(inBuf[3] & 0xFF);
    
  int pi = (int)(data & 0x3F);
  int vol = (int)((data >> 6) & 0x7F);
  int dur = (int)((data >> 13) & 0x0FFF);
  int inst = (int)((data >> 25) & 0x03);

  // デバッグ: デコード結果を表示
  println("pitch:" + pi + " volume:" + vol + " duration:" + dur + " instrumentID:" + inst);

  // ピッチが範囲外(37〜)の場合は36に制限 (pitchFreqHz は 0..36 の 37要素)
  if (pi > 36) {
    println("警告: pitch=" + pi + " は範囲外のため36に制限");
    pi = 36;
  }
  // 音量がゼロの場合は無視
  if (vol <= 0) continue;

  playNoteFromData(pi, vol, dur, inst);
  } // end while
}

void playNoteFromData(int pi, int vol, int dur, int inst) {
  currentPitch = pi;
  currentVolume = vol;
  currentDuration = dur;
  currentInstId = inst;
  
  // クロマチック周波数テーブル (C3=0 〜 C6=36)
  float[] pitchFreqHz = {
    130.81, 138.59, 146.83, 155.56, 164.81, 174.61, 185.00, 196.00, 207.65, 220.00, 233.08, 246.94,
    261.63, 277.18, 293.66, 311.13, 329.63, 349.23, 369.99, 392.00, 415.30, 440.00, 466.16, 493.88,
    523.25, 554.37, 587.33, 622.25, 659.25, 698.46, 739.99, 783.99, 830.61, 880.00, 932.33, 987.77,
    1046.50
  };
  float freq = pitchFreqHz[min(pi, 36)];

  float amp = vol / 100.0;
  float durSec = dur / 1000.0;
  if(durSec <= 0) durSec = 0.5; // fallback
  
  switch(inst) {
    case 0: // Piano (紫)
      out.playNote(0.0, durSec, new PianoInst(freq, amp));
      break;
    case 1: // Violin (赤)
      ViolinInstrument v = new ViolinInstrument(freq, amp);
      out.playNote(0.0, durSec, v);
      activeViolins.add(v);
      break;
    case 2: // Flute (青)
      out.playNote(0.0, durSec, new FluteInst(freq, amp));
      break;
    case 3: // Snare (緑)
      out.playNote(0.0, durSec, new SnareInstrument());
      break;
  }
}

// シリアルポートのクリーンアップ
void stop() {
  if (port != null) {
    port.clear();
    port.stop();
    println("Serial port Closed");
  }
  super.stop();
}

// =========================================================================
// 各楽器のクラス定義
// =========================================================================

class PianoInst implements Instrument {
  Summer finalSum;
  ADSR masterAdsr;
  Line filterEnv1, filterEnv2, filterEnv3;
  ADSR adsr1, adsr2, adsr3, hammerAdsr;

  PianoInst(float frequency, float amplitude) {
    finalSum = new Summer();
    
    float detuneUp = 1.0018;
    float detuneDown = 0.9984;
    float f1Start = frequency * 8.0;
    float f2Start = frequency * 7.0;
    float f3Start = frequency * 6.0;
    
    if (abs(frequency - 784.0) < 1.0 || abs(frequency - 987.76) < 1.0) {
      detuneUp = 1.0003;
      detuneDown = 0.9997;
      f1Start = frequency * 5.0;
      f2Start = frequency * 4.5;
      f3Start = frequency * 4.0;
    }

    Oscil osc1 = new Oscil(frequency, amplitude * 0.4, Waves.TRIANGLE);
    filterEnv1 = new Line(0.15, f1Start, frequency * 1.5);
    MoogFilter filter1 = new MoogFilter(f1Start, 0.1);
    filterEnv1.patch(filter1.frequency);
    osc1.patch(filter1);
    adsr1 = new ADSR(1.0, 0.005, 2.0, 0.0, 0.15);
    filter1.patch(adsr1);
    adsr1.patch(finalSum);
    
    Oscil osc2 = new Oscil(frequency * detuneUp, amplitude * 0.4, Waves.TRIANGLE);
    filterEnv2 = new Line(0.2, f2Start, frequency * 1.2);
    MoogFilter filter2 = new MoogFilter(f2Start, 0.1);
    filterEnv2.patch(filter2.frequency);
    osc2.patch(filter2);
    adsr2 = new ADSR(1.0, 0.008, 1.8, 0.0, 0.15);
    filter2.patch(adsr2);
    adsr2.patch(finalSum);

    Oscil osc3 = new Oscil(frequency * detuneDown, amplitude * 0.2, Waves.SAW);
    filterEnv3 = new Line(0.1, f3Start, frequency * 1.0);
    MoogFilter filter3 = new MoogFilter(f3Start, 0.1);
    filterEnv3.patch(filter3.frequency);
    osc3.patch(filter3);
    adsr3 = new ADSR(1.0, 0.004, 2.2, 0.0, 0.15);
    filter3.patch(adsr3);
    adsr3.patch(finalSum);

    Noise thump = new Noise(amplitude * 0.35);
    MoogFilter thumpFilter = new MoogFilter(frequency * 3.0, 0.2); 
    thump.patch(thumpFilter);
    hammerAdsr = new ADSR(1.0, 0.001, 0.04, 0.0, 0.01);
    thumpFilter.patch(hammerAdsr);
    hammerAdsr.patch(finalSum);
    
    masterAdsr = new ADSR(1.0, 0.001, 2.5, 0.0, 0.15);
    finalSum.patch(masterAdsr);
  }

  void noteOn(float dur) {
    filterEnv1.activate();
    filterEnv2.activate();
    filterEnv3.activate();
    adsr1.noteOn();
    adsr2.noteOn();
    adsr3.noteOn();
    hammerAdsr.noteOn();
    masterAdsr.noteOn();
    masterAdsr.patch(out);
  }

  void noteOff() {
    adsr1.noteOff();
    adsr2.noteOff();
    adsr3.noteOff();
    hammerAdsr.noteOff();
    masterAdsr.noteOff();
    masterAdsr.unpatchAfterRelease(out);
  }
}

class FluteInst implements Instrument {
  Summer finalSum;
  Summer sourceSum;
  Oscil fluteWave;
  Noise breathNoise;
  Noise chiffNoise;
  Line chiffLine;
  MoogFilter lp;
  ADSR adsr;

  Constant baseFreq;
  Oscil fmLfo;
  Line vibEnv;
  Summer freqSum;

  Constant baseAmp;
  Oscil amLfo;
  Summer ampSum;

  Constant filterBase;
  Line filterEnv;
  Oscil filterLfo;
  Summer filterSum;

  FluteInst(float frequency, float amplitude) {
    finalSum = new Summer();
    sourceSum = new Summer();
    baseFreq = new Constant(frequency);
    fmLfo = new Oscil(5.2, 1.0, Waves.SINE);
    vibEnv = new Line(0.25, 0.0, frequency * 0.006);
    vibEnv.patch(fmLfo.amplitude);
    freqSum = new Summer();
    baseFreq.patch(freqSum);
    fmLfo.patch(freqSum);
    
    baseAmp = new Constant(amplitude * 0.85);
    amLfo = new Oscil(5.2, amplitude * 0.06, Waves.SINE);
    ampSum = new Summer();
    baseAmp.patch(ampSum);
    amLfo.patch(ampSum);
    
    fluteWave = new Oscil(frequency, 1.0, Waves.TRIANGLE);
    freqSum.patch(fluteWave.frequency);
    ampSum.patch(fluteWave.amplitude);
    
    breathNoise = new Noise(amplitude * 0.15);
    fluteWave.patch(sourceSum);
    breathNoise.patch(sourceSum);
    
    filterBase = new Constant(frequency * 1.6);
    filterEnv = new Line(0.12, frequency * 1.3, 0.0);
    filterLfo = new Oscil(5.2, frequency * 0.12, Waves.SINE);
    
    filterSum = new Summer();
    filterBase.patch(filterSum);
    filterEnv.patch(filterSum);
    filterLfo.patch(filterSum);
    lp = new MoogFilter(frequency * 1.6, 0.15);
    filterSum.patch(lp.frequency);
    
    sourceSum.patch(lp);
    
    chiffNoise = new Noise(1.0);
    chiffLine = new Line(0.06, amplitude * 0.38, 0.0);
    chiffLine.patch(chiffNoise.amplitude);
    
    lp.patch(finalSum);
    chiffNoise.patch(finalSum);
    
    adsr = new ADSR(0.4, 0.12, 0.1, 0.9, 0.16);
    finalSum.patch(adsr);
  }

  void noteOn(float dur) {
    vibEnv.activate();
    filterEnv.activate();
    chiffLine.activate();
    adsr.noteOn();
    adsr.patch(out);
  }

  void noteOff() {
    adsr.noteOff();
    adsr.unpatchAfterRelease(out);
  }
}

class SnareInstrument implements Instrument {
  Noise noise;
  Noise noise2;
  Oscil body;
  LowPassSP lp;
  LowPassSP lp2;
  ADSR noiseADSR;
  ADSR noise2ADSR;
  ADSR bodyADSR;

  SnareInstrument() {
    noise = new Noise(0.35, Noise.Tint.PINK);
    noise2 = new Noise(0.1, Noise.Tint.WHITE);
    lp = new LowPassSP(5500, out.sampleRate());
    lp2 = new LowPassSP(3000, out.sampleRate());
    body = new Oscil(200, 0.3, Waves.SINE);
    noiseADSR  = new ADSR(1.0, 0.001, 0.18, 0.0, 0.12); 
    noise2ADSR = new ADSR(0.3, 0.001, 0.15, 0.0, 0.10); 
    bodyADSR   = new ADSR(0.8, 0.001, 0.18, 0.0, 0.12); 

    noise.patch(lp);
    lp.patch(noiseADSR);
    noiseADSR.patch(out);
    noise2.patch(lp2);
    lp2.patch(noise2ADSR);
    noise2ADSR.patch(out);
    body.patch(bodyADSR);
    bodyADSR.patch(out);
  }

  void noteOn(float duration) {
    noiseADSR.noteOn();
    noise2ADSR.noteOn();
    bodyADSR.noteOn();
  }
  
  void noteOff() {
    noiseADSR.noteOff();
    noise2ADSR.noteOff();
    bodyADSR.noteOff();
    // リリース後にUnpatchするよう修正
    noiseADSR.unpatchAfterRelease(out);
    noise2ADSR.unpatchAfterRelease(out);
    bodyADSR.unpatchAfterRelease(out);
  }
}

class ViolinInstrument implements Instrument {
  Oscil[]      harmonics;
  Line[]       ampEnvs;
  Multiplier[] freqScalers;

  Oscil      vibrato;         
  Constant   baseFreq;        
  Multiplier vibratoScaler;   
  Line       vibratoDepthEnv; 
  Summer     freqSum;         

  float maxAmp;
  float freq;

  int   phase;
  float noteOnMs;   
  float noteOffMs;  
  float envFactor;  

  boolean vibFadeInStarted;
  float   vibFadeInStartMs; 

  static final int NUM_HARMONICS = 40;
  float[] harmonicAmps = {
    0.22, 0.12, 0.066, 0.096, 0.085, 0.025, 0.02, 0.011, 0.003, 0.0022,
    0.002, 0.003, 0.0025, 0.0012, 0.001, 0.0005, 0.0007, 0.0001, 0.00009, 0.00008,
    0.0, 0.0003, 0.0004, 0.0005, 0.0006, 0.00016, 0.0018, 0.0019, 0.002, 0.0022,
    0.0019, 0.0016, 0.0014, 0.0012, 0.002, 0.003, 0.004, 0.0008, 0.0004, 0.0003
  };

  float[] scaledAmps; 

  static final float ATTACK   = 0.20;  
  static final float DECAY    = 0.40;  
  static final float SUSTAIN  = 0.90;  
  static final float RELEASE  = 0.25;  

  static final float VIB_RATE      = 5.5;   
  static final float VIB_DEPTH_MAX = 0.012; 
  static final float VIB_DELAY     = 0.10;  
  static final float VIB_FADEIN    = 0.25;  

  ViolinInstrument(float frequency, float maxAmp) {
    this.freq   = frequency;
    this.maxAmp = maxAmp;
    phase       = -1;
    envFactor   = 0;
    vibFadeInStarted = false;

    float ampSum = 0;
    for (float a : harmonicAmps) ampSum += a;
    scaledAmps = new float[NUM_HARMONICS];
    for (int k = 0; k < NUM_HARMONICS; k++) {
      scaledAmps[k] = (harmonicAmps[k] / ampSum) * maxAmp;
    }

    vibrato         = new Oscil(VIB_RATE, 1.0, Waves.SINE); 
    baseFreq        = new Constant(frequency);
    vibratoScaler   = new Multiplier(0);    
    vibratoDepthEnv = new Line();
    freqSum         = new Summer();

    vibrato.patch(vibratoScaler);
    vibratoDepthEnv.patch(vibratoScaler.amplitude);
    baseFreq.patch(freqSum);
    vibratoScaler.patch(freqSum);

    harmonics   = new Oscil[NUM_HARMONICS];
    ampEnvs     = new Line[NUM_HARMONICS];
    freqScalers = new Multiplier[NUM_HARMONICS];

    for (int k = 0; k < NUM_HARMONICS; k++) {
      harmonics[k] = new Oscil(frequency * (k + 1), 0, Waves.SINE); 
      ampEnvs[k]   = new Line();
      ampEnvs[k].patch(harmonics[k].amplitude);

      freqScalers[k] = new Multiplier(k + 1);
      freqSum.patch(freqScalers[k]);
      freqScalers[k].patch(harmonics[k].frequency);
    }
  }

  void noteOn(float duration) {
    noteOnMs         = millis();
    phase            = 0; 
    envFactor        = 0;
    vibFadeInStarted = false;

    for (int k = 0; k < NUM_HARMONICS; k++) {
      ampEnvs[k].activate(ATTACK, 0, scaledAmps[k]);
      harmonics[k].patch(out);
    }
  }

  void noteOff() {
    if (phase >= 3) return; 
    noteOffMs = millis();
    phase     = 3; 

    float currentVibDepth = computeVibratoDepth();

    for (int k = 0; k < NUM_HARMONICS; k++) {
      ampEnvs[k].activate(RELEASE, scaledAmps[k] * envFactor, 0);
    }
    vibratoDepthEnv.activate(RELEASE * 0.6, currentVibDepth, 0);
  }

  void update() {
    if (phase < 0 || phase == 4) return;

    float elapsed = (millis() - noteOnMs) / 1000.0;

    if (phase == 0 && elapsed >= ATTACK) {
      phase     = 1;
      envFactor = 1.0;
      for (int k = 0; k < NUM_HARMONICS; k++) {
        ampEnvs[k].activate(DECAY, scaledAmps[k], scaledAmps[k] * SUSTAIN);
      }
    } else if (phase == 1 && elapsed >= ATTACK + DECAY) {
      phase     = 2;
      envFactor = SUSTAIN;
    } else if (phase == 3) {
      float releaseElapsed = (millis() - noteOffMs) / 1000.0;
      if (releaseElapsed >= RELEASE + 0.05) {
        phase = 4;
        doUnpatch();
        return;
      }
    }

    if (phase == 0) {
      envFactor = constrain(elapsed / ATTACK, 0.0, 1.0);
    } else if (phase == 1) {
      float t = constrain((elapsed - ATTACK) / DECAY, 0.0, 1.0);
      envFactor = 1.0 - (1.0 - SUSTAIN) * t;
    }

    if (!vibFadeInStarted && elapsed >= VIB_DELAY && phase <= 2) {
      vibFadeInStarted = true;
      vibFadeInStartMs = millis();
      vibratoDepthEnv.activate(VIB_FADEIN, 0, freq * VIB_DEPTH_MAX);
    }
  }

  float computeVibratoDepth() {
    if (!vibFadeInStarted) return 0;
    float vibElapsed = (millis() - vibFadeInStartMs) / 1000.0;
    return min(vibElapsed / VIB_FADEIN, 1.0) * freq * VIB_DEPTH_MAX;
  }

  boolean isDone() {
    return phase == 4;
  }

  void doUnpatch() {
    for (int k = 0; k < NUM_HARMONICS; k++) {
      harmonics[k].unpatch(out);
    }
  }
}
