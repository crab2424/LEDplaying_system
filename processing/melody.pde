import ddf.minim.*;
import ddf.minim.ugens.*;

Minim minim ;
AudioOutput out;
Waveform currentWaveform; // 音色格納用変数

// 各音の高さ
String [] melody = {
  "C4", "D4", "E4", "F4", "E4", "D4", "C4",
  "E4", "F4", "G4","A4", "G4", "F4", "E4",
  "C4", "C4", "C4", "C4",
  "C4", "C4", "D4", "D4", "E4", "E4", "F4", "F4",
   "E4", "D4", "C4",
};

// 各音の長さ（ 拍）
float [] duration = {
2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f,
2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f,
2.0f, 2.0f, 2.0f, 2.0f,
1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
1.0f, 1.0f, 1.0f
};

// 各音の開始位置
float [] startTime = {
  0.0f, 2.0f, 4.0f, 6.0f, 8.0f, 10.0f, 12.0f,
  16.0f, 18.0f, 20.0f, 22.0f, 24.0f, 26.0f, 28.0f,
  32.0f, 36.0f, 40.0f, 44.0f, 
  48.0f, 49.0f, 50.0f, 51.0f, 52.0f, 53.0f, 54.0f, 55.0f,
  56.0f, 58.0f, 60.0f
};

// 各音の強弱
float [] Amplitude = {
  1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
  1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
  1.0f, 1.0f, 1.0f, 1.0f,
  1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
  1.0f, 0.9f, 1.0f
};

float volume = 0.2;

// 音色を変更するためにInstrument インタフェースを実装する
class HackInstrument implements Instrument
{
  Oscil wave;
  Line ampEnv;
  float maxAmp;
  
  HackInstrument( float frequency , float maxAmp , Waveform wf )
  { // O s c i l を使って音信号を作成（ 周波数, 振幅, 音色）
    wave = new Oscil( frequency , 0, wf );
    // 引数で渡された最大振幅をクラスの変数に代入
    this.maxAmp = maxAmp;
    // 振幅変調を与える（ 初期値は1 から0 への減衰）
    ampEnv = new Line( );
    // 作成した音信号を振幅変調の出力に送る
    ampEnv.patch( wave.amplitude );
  }
  
  // コールバック関数： 再生開始
  void noteOn( float duration )
  { // 振幅変調の開始（ 長さ， 開始時の振幅， 終了時の振幅）
  ampEnv.activate(duration , this.maxAmp , 0);
  // 音の再生
  wave.patch( out );
  }

  // コールバック関数： 再生停止
  void noteOff()
  { // 再生の停止
    wave.unpatch( out );
  }
}

void setup ()
{
  size (512, 200);
  // minim のインスタンスを用意
  minim = new Minim ( this );
  // minimのgetLineOutメソッドを呼び出し，AudioOutputオブジェクトを受け取る
  out = minim . getLineOut ();
  // テンポの設定，BPM=120
  out. setTempo ( 120 * 2 );
  // 音色の初期値（ 正弦波）
  currentWaveform = Waves.SINE;
}

void playSong () {
  // 再生を停止
  out. pauseNotes ();
  // 繰り返し処理を使って異なる音を追加
  for (int i = 0; i < melody.length; i++) {
    out.playNote(startTime[i], duration[i],
      new HackInstrument(Frequency.ofPitch( melody[i] ).asHz(),
        Amplitude[i] * volume, currentWaveform));
  }
  // 再生
  out. resumeNotes ();
}

void draw ()
{
  background (0);
  stroke (255);

  // 左チャンネルと右チャンネルに入っている波形を描画---------- (c)
  for (int i = 0; i < out. bufferSize () - 1; i++)
  {
    line ( i, 50 - out. left .get(i)*50, i+1, 50 - out. left .get(i +1)*50 );
    line ( i, 150 - out. right .get(i)*50, i+1, 150 - out. right .get(i +1)*50 );
  }
}

void keyPressed() {
  switch (key)
  {
    case '1':
      // 正弦波
      currentWaveform = Waves.SINE;
      break;
    case '2':
      // 三角波
      currentWaveform = Waves.TRIANGLE;
      break;
    case '3':
      // のこぎり波
      currentWaveform = Waves.SAW;
      break;
    case '4':
      // 矩形波
      currentWaveform = Waves.SQUARE;
      break;
    case '5':
      // 16 個の奇数倍音からなる音。振幅値はランダム。
      currentWaveform = Waves.randomNOddHarms (16);
      break;
    case '6':
      // 倍音の振幅値を指定した自作の音色
      currentWaveform = WavetableGenerator.gen10(
        4096, // サンプルサイズ（2 の倍数で）
        new float[] { 1.0f, 0.45f, 0.20f, 0.10f, 0.05f } // 各倍音の振幅値（0.0˜1.0 ）
      );
      break;
  case 'p':
    // 作成した信号を出力
    playSong ();
    break;
    default: break;
  }
}
