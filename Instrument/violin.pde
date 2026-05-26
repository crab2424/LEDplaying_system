import ddf.minim.*;
import ddf.minim.ugens.*;
import processing.serial.*;

Minim minim;
AudioOutput out;
Serial port; // シリアルポート

// 【デバッグ用設定】 合成の材料となる波形と、倍音音量設定

// 1: SINE (正弦波), 2: TRIANGLE (三角波), 3: SAW (ノコギリ波), 4: SQUARE (矩形波)
int WAVE_TYPE = 1; 

// 左から順に [1倍音(基本音), 2倍音, 3倍音, 4倍音, 5倍音...] の強さ
float[] harmonicAmplitudes = { 0.9f, 0.02f, 0.01f, 0.03f, 0.06f, 0.001f };

// 【デバッグ用設定】 ADSR（エンベロープ）の設定
float envAttack  = 0.12f;  // Attack (秒): 音が立ち上がり、最大音量になるまでの時間
float envDecay   = 0.4f;  // Decay (秒): 最大音量からSustainレベルまで減衰する時間
float envSustain = 0.7f;  // Sustain (0.0〜1.0): 最大音量に対する、保持される音量の割合
float envRelease = 0.3f;  // Release (秒): 演奏（noteOff）後、音が完全に消えるまでの余韻

Waveform currentWaveform;
String pitch = "A4";
HashMap<Character, String> keyMap = new HashMap<Character, String>();

class ViolinInstrument implements Instrument {
  Oscil wave;
  ADSR env; 
  
  ViolinInstrument( float frequency, float maxAmp, Waveform wf ) {
    // 大元のオシレータは常にフルボリューム(1.0f)で鳴らしておく
    wave = new Oscil( frequency, 1.0f, wf );
    
    // Sustain（保持レベル）は、その時の最大音量(maxAmp)に対する割合で計算する
    float sustainLevel = maxAmp * envSustain;
    
    // ADSRフィルターの生成
    env = new ADSR( maxAmp, envAttack, envDecay, sustainLevel, envRelease );
    
    // オシレータの音をADSRフィルターに繋ぐ（直列接続）
    wave.patch( env );
  }
  
  void noteOn( float duration ) { 
    env.noteOn();
    env.patch( out ); // 最終出力をスピーカーに繋ぐ
  }

  void noteOff() { 
    env.unpatchAfterRelease( out ); // リリースが終わったらスピーカーから切り離す
    env.noteOff(); // 音の減衰（リリース）を開始する
  }
}

void setup() {
  size(512, 200);
  
  minim = new Minim(this);
  out = minim.getLineOut();
  out.setTempo(120);
  
  setupWaveform();
  setupKeyMap();

  printArray(Serial.list());
  if (Serial.list().length > 0) {
    String portName = Serial.list()[0]; 
    port = new Serial(this, portName, 115200); 
    port.bufferUntil('\n'); 
  } else {
    println("シリアルポートが見つかりません。キーボードでの演奏モードのみで動作します。");
  }
}

void setupWaveform() {
  Waveform baseWf;
  
  switch (WAVE_TYPE) {
    case 1: baseWf = Waves.SINE; break;
    case 2: baseWf = Waves.TRIANGLE; break;
    case 3: baseWf = Waves.SAW; break;
    case 4: baseWf = Waves.SQUARE; break;
    default: baseWf = Waves.SINE; break;
  }
  
  float[] samples = new float[1024]; 
  float maxPeak = 0.0f; 
  
  for (int i = 0; i < 1024; i++) {
    float val = 0;
    for (int h = 0; h < harmonicAmplitudes.length; h++) {
      float amp = harmonicAmplitudes[h];
      if (amp > 0.0f) {
        float phase = (i * (h + 1) / 1024.0f) % 1.0f;
        val += amp * baseWf.value(phase);
      }
    }
    samples[i] = val;
    if (abs(val) > maxPeak) maxPeak = abs(val);
  }
  
  if (maxPeak > 0) {
    for (int i = 0; i < 1024; i++) {
      samples[i] /= maxPeak;
    }
  }
  currentWaveform = new Wavetable(samples);
}

void draw() {
  background(0);
  stroke(255);

  for (int i = 0; i < out.bufferSize() - 1; i++) {
    line( i, 50 - out.left.get(i)*50, i+1, 50 - out.left.get(i +1)*50 );
    line( i, 150 - out.right.get(i)*50, i+1, 150 - out.right.get(i +1)*50 );
  }
}

void serialEvent(Serial p) {
  String inString = p.readStringUntil('\n');
  
  if (inString != null) {
    inString = trim(inString); 
    String[] items = split(inString, ','); 
    
    if (items[0].equals("NOTE") && items.length >= 4) {
      float freq = float(items[1]);
      float durationMs = float(items[2]);
      float durationSec = durationMs / 1000.0f; 
      int velocity = int(items[3]);
      
      float maxAmp = map(velocity, 0, 127, 0.0f, 1.0f);
      out.playNote(0.0f, durationSec, new ViolinInstrument(freq, maxAmp, currentWaveform));
    }
  }
}

void keyPressed() {
  if (keyMap.containsKey(key)) {
    pitch = keyMap.get(key);
    playSong();
  }
}

void playSong() {
  out.pauseNotes();
  out.playNote(0.0f, 1.0f, new ViolinInstrument(Frequency.ofPitch( pitch ).asHz(), 0.5f, currentWaveform));
  out.resumeNotes();
}

void setupKeyMap() {
  keyMap.put('a', "Bb2"); keyMap.put('z', "C3");  keyMap.put('s', "C#3");
  keyMap.put('x', "D3");  keyMap.put('d', "Eb3"); keyMap.put('c', "E3");
  keyMap.put('v', "F3");  keyMap.put('g', "F#3"); keyMap.put('b', "G3");
  keyMap.put('h', "Ab3"); keyMap.put('n', "A3");  keyMap.put('j', "Bb3");
  keyMap.put('m', "B3");  keyMap.put(',', "C4");  keyMap.put('l', "Db4");
  keyMap.put('.', "D4");  keyMap.put(';', "Eb4"); keyMap.put('/', "E4");
  keyMap.put('q', "F4");  keyMap.put('2', "F#4"); keyMap.put('w', "G4");
  keyMap.put('3', "Ab4"); keyMap.put('e', "A4");  keyMap.put('4', "Bb4");
  keyMap.put('r', "B4");  keyMap.put('t', "C5");  keyMap.put('6', "C#5");
  keyMap.put('y', "D5");  keyMap.put('7', "Eb5"); keyMap.put('u', "E5");
  keyMap.put('i', "F5");  keyMap.put('9', "F#5"); keyMap.put('o', "G5");
  keyMap.put('0', "Ab5"); keyMap.put('p', "A5");  keyMap.put('-', "Bb5");
  keyMap.put('@', "B5");  keyMap.put('[', "C6");
}
