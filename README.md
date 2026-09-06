# M5Stack BLE HID Mouse

M5StickS3 と M5Stack CoreS3 を、Windows/macOS/Linux 向けの BLE HID マウスとして使う
PlatformIO プロジェクトです。CoreS3 は BMI270、BMM150、タッチパネル、仮想ボタン、マイクを
組み合わせ、用途に応じて操作方法を使い分けられます。

## ビルド

```bash
pio run -e m5stack-sticks3
pio run -e m5stack-cores3
```

CoreS3 は PlatformIO 標準の `m5stack-cores3` 定義（ESP32-S3、16 MB フラッシュ）を使用します。
M5StickS3 用の 8 MB ボード定義は `boards/m5stack-sticks3.json` に含めています。

書き込み後、PC の Bluetooth 設定でデバイスをペアリングしてください。

## コントロール

| デバイス | 入力 | 操作 |
|---|---|---|
| M5StickS3 | IMU | 本体の傾きの変化に応じてカーソルを相対移動 |
| M5StickS3 | Button A | 短押し: 左クリック、長押し: 左ドラッグ |
| CoreS3 | BMI270 ジャイロ | 空中で本体を回すとカーソルを相対移動 |
| CoreS3 | CoreS3 を強く振る | 空中マウスの一時停止／再開 |
| CoreS3 | 画面の 1 本指操作 | なぞる: カーソル移動、タップ: 左クリック、長押し: 右クリック |
| CoreS3 | 画面の 2 本指操作 | タップ: 右クリック、上下になぞる: ホイールスクロール |
| CoreS3 | 画面下部の LEFT/MIDDLE/RIGHT | M5Unified の仮想 BtnA/BtnB/BtnC。短押し: 各クリック、長押し: 対応ボタンを押し続ける |
| CoreS3 | HOLD / DRAG TOGGLE | 左ボタンを固定してドラッグ。もう一度タップして解除 |
| CoreS3 | マイク（既定で無効） | 大きな拍手・タップで左クリック |

## CoreS3 の空中マウス

**画面を上向きにして、先端（画面上辺の +Y 方向）を PC へ向ける、テレビのリモコンの持ち方**です。
CoreS3 の USB-C は側面にあるため、「USB-C と反対側」ではなく **画面上辺**を基準にしてください。
軸の基準はコネクタや LCD の表示回転ではなく、
[公式 IMU 軸図](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/490/IMU-CoreS3.jpg) の
`+X`: 画面右、`+Y`: 画面上辺、`+Z`: 画面から外へ、という右手系です。
起動時は机上などで約 1 秒静止させ、`Calibrating... Keep still!` が消えてから操作してください。
動き・分散が大きい場合は自動で校正をやり直します。校正中もタッチパッドは利用できます。

| 動作（画面が水平の場合） | ジャイロの符号 | カーソル |
|---|---|---|
| 先端を右へ旋回 | `gz < 0` | 右 (`X+`) |
| 先端を左へ旋回 | `gz > 0` | 左 (`X-`) |
| 先端を上げる | `gx > 0` | 上 (`Y-`) |
| 先端を下げる | `gx < 0` | 下 (`Y+`) |
| 先端方向を軸にひねるだけ | `gy` のみ | 移動なし |
| 傾けたまま静止 | バイアス補正後の角速度ゼロ | 停止 |

右手の法則では `+Z` 回転は先端 `+Y` を左へ、`+X` 回転は先端を上へ動かします。
したがって X・Y 両方の既定符号は `-1` です。持ち方・個体差で逆になる軸だけ符号定数を反転できます。

### 相対移動の計算

カーソル位置・速度を加速度計の傾きから作ることはありません。加速度計は姿勢の基準と静止判定に
だけ使います。静止時の加速度は「下向きの重力」ではなく上向きの支持力なので、正規化した
低域通過後の値を `up` とします。バイアスを引いたジャイロ `omega` に対して次の投影を行います。

```text
forward = (0, 1, 0)
horizontal = normalize(forward - dot(forward, up) * up)
right = cross(horizontal, up)
yaw = dot(omega, up)
pitch = dot(omega, right)
dx = kCursorXSign * gain * filtered_yaw * dt_seconds
dy = kCursorYSign * gain * filtered_pitch * dt_seconds
```

これにより手首をロールさせても旋回は左右、先端の上下動は上下になります。
急なロールでは姿勢基準の低域通過（時定数 150ms）の追従遅れがあり、瞬時の完全補償ではありません。
加速度が 0.8～1.2g から外れる場合はその値で姿勢を更新せず、`yaw=gz` / `pitch=gx` の本体軸へ
一時的に戻します。先端がほぼ鉛直で水平投影を定義できない場合も同じです（`BODY AXES` 表示）。
縦持ち以外を禁止する旧判定や連続傾きモードはありません。

角速度の 1°/秒以下は静止域とし、その外側 1°/秒は連続的な二次曲線で立ち上げます。
以降は線形、レート平滑化は軽い 1 ポール（alpha=0.65）だけです。静止域へ入るとフィルタの余韻を
切ります。基本感度は **22 HID カウント/度**（PC 側の設定次第で概ね画素相当）、
速度による倍率は `1 + 0.6 * min(rate/200, 1)` です。実際の画面上の移動量は OS のポインタ速度・
加速設定にも依存します。

BMI270 を **加速度・ジャイロとも 400Hz** に設定して読み戻し確認し、専用タスクが 2ms ごとに
`M5.Imu.update()` を呼びます。新しいジャイロデータのマスクを確認してから同一バッチの
`getImuData()` を読み、取得時の `micros()` 差分で積分します。`getAccelData()` / `getGyroData()`
を別々に呼んで二重更新することはありません。画面には直近 1 秒の実取得 Hz も表示します。
画面描画とは分離し、タッチと IMU の共用 I2C アクセスは mutex で排他します。

空中マウスの HID 移動レポートは約 8ms ごとです。浮動小数で移動量を蓄積し、整数部分だけを
±127 に制限して送信します。小数と制限を超えた分は次回へ残すため、低速移動や高速移動量を捨てません。
一時停止・接続切断・入力欠落時には蓄積分を破棄し、復帰時の飛びを防ぎます。
BLE 通信と OS を含む実際の遅延・実サンプルレートは実機で確認してください。

起動時のジャイロオフセットは M5Unified の保存値／単発校正に頼らず、新しい平均値で置き換えます。
この校正値は RAM のみに保持し、毎回の起動で取得し直します。加速度計・磁気センサーの保存校正は保持します。
以降のバイアス追従は角速度が 1.5°/秒未満で加速度が安定した状態が 0.5 秒続いた場合だけ、
30 秒の時定数でゆっくり行います。低速の一定回転とバイアスは完全には区別できないため、
校正中は必ず本体を静止させてください。

### タッチ・クリック・振る操作との優先順位

画面に触れている間と仮想ボタン押下中は **空中マウスだけを停止**し、離してからも 150ms 停止します
（`AIR:FREEZE`）。その間もタッチ移動、クリック、スクロールは有効です。空中でドラッグしたい場合は
HOLD / DRAG をオンにして指を離してください。保持中のクリックでドラッグ固定を解除しないようにしています。

空中操作を止めたいときは強い短いシェイクで `AIR:PAUSE` にします。再度のシェイクで再開します。
3g 以上を連続 2 サンプル検出した場合にだけ切り替え、0.4 秒の静かな状態と 1.2 秒のクールダウンを
満たすまで再受付しません。通常のポインティングや長く振り続ける動作で連続切替しない設計です。
再開シェイク後も 0.4 秒は移動を凍結します。

BMM150 の補正済み磁気方位は引き続き `MAG` に表示しますが、カーソルの計算には一切使いません。
磁石や金属による磁場の変化は空中マウスの方向を変えません。

### 空中マウスの全調整定数

`include/CoreAirMouse.h` を変更して再ビルドします。角速度は度/秒、移動感度は HID カウント/度です。
StickS3 の処理・定数はこのヘッダを使用せず、従来のままです。

| 定数 | 既定値 | 用途 |
|---|---:|---|
| `kPixelsPerDegree` | 22.0 | 両軸の基本感度 |
| `kCursorXSign`, `kCursorYSign` | -1, -1 | yaw→X、pitch→Y の符号 |
| `kRateDeadzoneDps` | 1.0 | 静止域 |
| `kRateKneeDps` | 1.0 | 静止域の外側のソフトニー幅 |
| `kRateFilterAlpha` | 0.65 | 軽い平滑化（0.5～1.0） |
| `kAcceleration` | 0.6 | 最大追加倍率（上限 1.6 倍） |
| `kAccelerationFullScaleDps` | 200.0 | 加速が上限に達するレート |
| `kGravityTimeConstantS` | 0.15秒 | 姿勢基準の低域通過時定数 |
| `kGravityToleranceG` | 0.20g | 重力方向を信頼する 1g からの許容差 |
| `kMinHorizontalProjection` | 0.10 | 鉛直付近の投影の下限 |
| `kCalibrationDurationUs` | 1000000 | 起動時校正の最短時間 |
| `kCalibrationSamples` | 400 | 校正の最小サンプル数 |
| `kCalibrationMaxRateDps` | 5.0 | 校正中の最大角速度ノルム |
| `kCalibrationGyroStdDevDps` | 0.25 | 校正時の各軸標準偏差上限 |
| `kCalibrationAccelStdDevG` | 0.015g | 校正時の加速度各軸標準偏差上限 |
| `kSteadyGravityToleranceG` | 0.06g | 校正・バイアス追従の静止判定 |
| `kSteadyAccelDeltaG` | 0.025g | 追従時の加速度サンプル間差の上限 |
| `kBiasTrackingMaxRateDps` | 1.5 | 追従を許す補正後角速度ノルム |
| `kBiasStillTimeUs` | 500000 | 追従前の静止継続時間 |
| `kBiasTimeConstantS` | 30.0秒 | バイアス追従時定数 |
| `kImuPollMs` | 2ms | 専用タスクのポーリング周期（ODR は 400Hz 固定） |
| `kReportIntervalUs` | 8000 | 空中マウスの HID 移動送信間隔 |
| `kMaxSampleGapUs` | 30000 | これ以上の入力欠落は移動破棄／エラー表示 |
| `kTouchFreezeUs` | 150000 | 画面操作／クリック後の移動凍結時間 |
| `kShakeThresholdG` | 3.0g | シェイクしきい値 |
| `kShakeSamples` | 2 | しきい値以上の連続サンプル数 |
| `kShakeCooldownUs` | 1200000 | 次のシェイクまでの最短時間 |
| `kShakeQuietUs` | 400000 | 再受付に必要な静かな時間／シェイク後の凍結 |

`src/main.cpp` の `kTouchPollUs=5000` はタッチ更新周期、
`kStatusIntervalMs=200` は画面の状態更新周期です。
BMI270 の設定は `ACC_CONF=0xAA`, `ACC_RANGE=0x02`, `GYR_CONF=0xEA`, `GYR_RANGE=0x00`
（400Hz、通常帯域／性能優先、±8g、±2000°/秒）です。

タッチ操作の既定値は従来どおり `kTrackpadSensitivity=1.55`、
`kScrollSensitivity=0.18`、`kClickThresholdMs=220`、`kVirtualButtonHoldMs=450`、
`kTouchMovementThresholdPixels=4`、`kScrollMovementThresholdPixels=2`。
画面下部の高さは `kVirtualButtonBarHeight=30`、`kHoldToggleHeight=30` 画素です。
マイクは `kEnableClapClick=false`、`kClapPeakThreshold=9000`、
`kClapCooldownMs=500`、`kClapSampleCount=64` です。

### タッチパッドとクリックバー

上部の緑枠はタッチパッドです。1 本指でカーソルを動かし、2 本指で上下になぞるとスクロールします。
その下の黄色行は左ドラッグ固定の切り替え、最下段 3 分割は M5Unified の仮想 `BtnA`、`BtnB`、`BtnC`
です。仮想ボタンは M5Unified の `setTouchButtonHeight()` で有効化しているため、画面 UI とライブラリの
ボタン状態が一致します。

```cpp
constexpr float kTrackpadSensitivity = 1.55f;
constexpr int kTouchMovementThresholdPixels = 4;
constexpr float kScrollSensitivity = 0.18f;
constexpr uint32_t kVirtualButtonHoldMs = 450U;
```

### マイクの拍手クリック

誤操作を避けるため既定では無効です。有効にする場合は `kEnableClapClick` を `true` にしてください。
M5Unified の `M5.Mic.record()` で短い PCM ブロックを読み、最大振幅がしきい値以上のときだけクリックします。
マイクとスピーカーは同じ音声経路を共有するため、このファームウェアは拍手クリック有効時にスピーカーを
無効化します。

```cpp
constexpr bool kEnableClapClick = false;
constexpr int kClapPeakThreshold = 9000;
```

誤クリックがある場合は `kClapPeakThreshold` を上げてください。

## 注意事項

- BLE 接続が切れている間は PC に操作を送信しません。
- CoreS3 の磁気方位は、磁石、金属、ケーブルなどの影響を受けます。`MAG` 表示が不安定なときは、
  M5Unified の IMU 例を使って BMM150 を校正してください。
- 実機の保持癖で空中マウスの方向が合わない場合は、符号定数で軸ごとに補正してください。

## 参考

- [CoreS3 仕様・IMU 軸図](https://docs.m5stack.com/en/core/CoreS3)
- [Bosch BMI270 のレジスタ定義](https://github.com/boschsensortec/BMI270_SensorAPI/blob/master/bmi2_defs.h)
- M5Unified: https://github.com/m5stack/M5Unified
- ESP32-BLE-Mouse: https://github.com/T-vK/ESP32-BLE-Mouse

M5Unified 0.2.20 の `src/utility/IMU_Class.cpp` では CoreS3 の BMI270 の加速度／ジャイロ軸は
変更せず、BMM150 の Y/Z だけ反転します。本ファームは `setAxisOrder(X+,Y+,Z+)` を明示し、
LCD 表示の回転に依存しません。`src/utility/imu/BMI270_Class.cpp` のデータ準備完了マスクと
`IMU_Base.hpp` の 8g／2000°/秒の換算係数も使用しています。

## 数値回帰テスト

`tests/core_air_mouse_test.cpp` は外部テストライブラリを使わないホスト用 C++ テストです。
実ファームと同じ `CoreAirMouse.h` に、方向、ロール、静止校正の再試行、静止傾き、バイアス追従、
ソフトニー、可変 dt、小数・±127 超過分の保持、クリック凍結、切断、シェイク、時刻周回を入力します。

```powershell
g++ -std=c++14 -Wall -Wextra -Werror -pedantic tests\core_air_mouse_test.cpp -o core_air_mouse_test.exe
.\core_air_mouse_test.exe
Remove-Item core_air_mouse_test.exe
```
