# M5Stack BLE HID Mouse

M5StickS3 と M5Stack CoreS3 を、Windows/macOS/Linux 向けの BLE HID マウスとして使う
PlatformIO プロジェクトです。両機種とも BMI270 のジャイロで空中マウスとして操作できます。
CoreS3 はタッチパッドも使用可能。StickS3 は物理ボタンとジャイロでクリック・ドラッグ・スクロールします。

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
| M5StickS3 | BMI270 ジャイロ | 本体を旋回／先端を上下に回して相対移動。傾けたまま静止すると停止 |
| M5StickS3 | 前面 BtnA (GPIO11) | 短押し: 左クリック。450ms 長押し後、押している間だけ左ドラッグ |
| M5StickS3 | 側面 BtnB (GPIO12) | 短押し: 右クリック。300ms 以内にもう一度押すと中クリック |
| M5StickS3 | BtnB を350ms以上押したまま先端を上下 | カーソルを止め、上げる: 上スクロール、下げる: 下スクロール |
| M5StickS3 | 電源／RESET ボタン | 工場動作を維持。単押しはリセット。一時停止には使用しない |
| M5StickS3 | 強いシェイク | 空中操作の一時停止／再開 |
| CoreS3 | BMI270 ジャイロ | 空中で本体を回すとカーソルを相対移動 |
| CoreS3 | CoreS3 を強く振る | 空中マウスの一時停止／再開 |
| CoreS3 | 画面の 1 本指操作 | なぞる: カーソル移動、タップ: 左クリック、長押し: 右クリック |
| CoreS3 | 画面の 2 本指操作 | タップ: 右クリック、上下になぞる: ホイールスクロール |
| CoreS3 | 画面下部の LEFT/MIDDLE/RIGHT | M5Unified の仮想 BtnA/BtnB/BtnC。短押し: 各クリック、長押し: 対応ボタンを押し続ける |
| CoreS3 | HOLD / DRAG TOGGLE | 左ボタンを固定してドラッグ。もう一度タップして解除 |
| CoreS3 | マイク（既定で無効） | 大きな拍手・タップで左クリック |

## StickS3 のハードウェアと持ち方

[StickS3 公式仕様](https://docs.m5stack.com/en/core/StickS3) とインストール済み
M5Unified 0.2.20 / M5GFX の実装を照合しています。StickC / StickC Plus の仕様とは異なります。

| 項目 | StickS3 の確認結果 |
|---|---|
| IMU | BMI270、6軸（3軸加速度＋3軸ジャイロ）、I2C 0x68、SDA=47 / SCL=48 |
| 地磁気・タッチ | 内蔵地磁気センサーなし、タッチパネルなし。CoreS3 の BMM150 / タッチ処理は使わない |
| マイク | **搭載あり**。MEMS マイク＋ES8311。M5Unified に StickS3 用の `M5.Mic` 設定あり。本実装では無効 |
| 音声出力 | AW8737＋8Ω/1W スピーカー。`M5.Speaker` 対応で、単なるブザーではない。本実装では無効 |
| プログラム用ボタン | 前面 BtnA=GPIO11、側面 BtnB=GPIO12、両方 active-low。BtnC は使わない |
| 電源／RESET | M5PM1 管理。標準操作は単押し=電源オン／リセット、2回=電源オフ、長押し=書き込みモード |
| 表示 | ST7789P3、135×240、タッチなし。M5GFX の `offset_rotation=0`、本実装は `setRotation(0)` の縦表示 |
| LED | PMIC管理の緑LEDあり。`M5.Power.M5pm1.setLedEnLevel()` で制御可能だが自動点滅もある。`M5.Power.setLed()` は非対応。状態表示には LCD を使用 |

ライブラリ側の根拠:
[BtnA/B GPIO入力](https://github.com/m5stack/M5Unified/blob/774d920cd6851a5231748b56ece1b073645f313f/src/M5Unified.cpp#L3256-L3260)、
[StickS3スピーカー設定](https://github.com/m5stack/M5Unified/blob/774d920cd6851a5231748b56ece1b073645f313f/src/M5Unified.cpp#L2332-L2348)、
[StickS3マイク設定](https://github.com/m5stack/M5Unified/blob/774d920cd6851a5231748b56ece1b073645f313f/src/M5Unified.cpp#L2534-L2544)、
[BMI270の機種別軸補正](https://github.com/m5stack/M5Unified/blob/774d920cd6851a5231748b56ece1b073645f313f/src/utility/IMU_Class.cpp#L74-L120)。
LCD寸法とタッチドライバを設定しないことは、インストール済み
`M5GFX/src/M5GFX.cpp` の `board_M5StickS3` 分岐でも確認できます。

**画面を上向きにして、USB-C と反対側の Hat2-Bus（16ピン）端を PC へ向けます。**
Grove/HY2.0-4P は先端ではなく **USB-C と同じ端**です。
[公式外形図](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/K150-sticks3_page_01.png) と
[公式 IMU 軸図](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/IMU-StickS3.jpg) を参照してください。
画面の文字は Hat 側を上、USB 側を下にして読める縦向きにしています。表示回転は IMU 軸を変えません。

### StickS3 の軸変換

CoreS3 と **ネイティブ軸が異なります**。StickS3 の `+X` は USB-C 向き、`+Y` は画面右、
`+Z` は画面から外向きです。M5Unified の `IMU_Class.cpp` は `board_M5StickS3` に軸回転を追加しません。
ライブラリのユーザー軸順を identity に設定し、`AirMouse.h` の `stickToPointerFrame()` で
加速度・ジャイロの **両方**を共通座標へ変換してから校正・投影します。

```text
共通 X（右） = ネイティブ Y
共通 Y（先端） = -ネイティブ X
共通 Z（画面外） = ネイティブ Z
```

| 動作（水平な画面） | StickS3 ネイティブジャイロ | 変換後／結果 |
|---|---|---|
| 先端を右へ旋回 | `gz < 0` | yaw<0、`kCursorXSign=-1` なので X+ |
| 先端を上げる | `gy > 0` | pitch>0、`kCursorYSign=-1` なので HID Y− |
| 先端方向を軸にひねるだけ | `gx` のみ | yaw/pitch は0、移動しない |
| 傾けたまま静止 | 補正後の全ジャイロが0 | 移動・スクロールしない |

右手の法則で `+Z` 回転は先端を左へ、ネイティブ `+Y` 回転は先端 `-X` を上へ動かすため、この符号です。
ロール補償・加速度異常時の本体軸フォールバックも **変換後の座標**で行います。
従来の加速度傾き差分・Y感度1.75倍・傾き平滑化は削除しました。共通の基本感度は両軸とも22です。

### StickS3 の操作上の注意

起動後は机などで約1秒静止し、`Calibrating...` が消えるのを待ってください。
動いている間は校正を再試行します。画面には BLE、校正／PAUSED／SCROLL／DRAG、実取得Hz、
一時的な LEFT／RIGHT／MIDDLE クリック表示を出します。

BtnA のドラッグは **押している間だけ**です。押し始めはカーソルを止め、450msでドラッグへ移行し、
さらに150msの押下振動抑制後に移動できます。離すと左ボタンを解放し、150ms停止します。
BtnB の単押しは、二度押し判定のため離してから300ms遅延します。二度押しでは右クリックを混ぜず、
中クリックを一度だけ送ります。短押し後に続けて長押しした場合、保留中の右クリックを取り消して
スクロールします。接続が切れた場合は保留クリック・ドラッグを解除し、再接続後は押し直してください。

BtnB 長押し中は横移動も縦移動も送らず、バイアス補正／ロール補償済み pitch をホイールへ積分します。
先端を上げると正のホイール（通常は上スクロール）。感度は **0.35ステップ/度**、
ポインタと同じ静止域・軽い平滑化を使い、ホイールには速度加速を掛けません。
小数を保持して8msごとに整数分を送り、モード切替時は残量を破棄します。
スクロール開始・終了およびすべてのクリック後に150ms停止します。

### 電源ボタンは変更しません

工場動作（単押し=電源オン／リセット、二度押し=電源オフ、長押し=書き込みモード）を維持します。
追加のPMICレジスタ書込み・電源ボタン設定・BtnPWR IRQ処理はありません。
M5Unified の通常のボード初期化は使用しますが、`config.pmic_button=false` として
電源キーのイベントポーリングを無効にしています。一時停止／再開には **3g以上を連続2サンプル**
検出するシェイクを使い、画面に `PAUSED` を表示します。再受付は静かな状態400msと
1.2秒のクールダウン後です。

### StickS3 専用定数

共通の全ジャイロ定数は後述の表にあります。以下は StickS3 のみの設定です。

| ファイル | 定数 | 既定値 |
|---|---|---|
| `include/StickButtons.h` | `kDragHoldMs` | 450ms |
| `include/StickButtons.h` | `kScrollHoldMs` | 350ms |
| `include/StickButtons.h` | `kDoubleClickMs` | 300ms（最初の解放から次の押下まで） |
| `include/StickButtons.h` | `kDebounceMs` | 10ms |
| `src/StickMouse.cpp` | `kButtonPollUs` | 5000µs |
| `src/StickMouse.cpp` | `kStatusIntervalMs` | 200ms |
| `src/StickMouse.cpp` | `kClickIndicatorMs` | 300ms |
| `src/StickMouse.cpp` | `kDisplayRotation` | 0（縦135×240） |
| `src/StickMouse.cpp` | `kDisplayBrightness` | 90 |
| `include/AirMouse.h` | `kScrollStepsPerDegree` | 0.35 |
| `include/AirMouse.h` | `kScrollSign` | +1 |

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

## 両機種共通の相対移動の計算

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
画面描画とは分離し、`M5.update()`（タッチ／PMIC）と IMU の共用 I2C アクセスは mutex で排他します。

空中マウスの HID 移動レポートは約 8ms ごとです。浮動小数で移動量を蓄積し、整数部分だけを
±127 に制限して送信します。小数と制限を超えた分は次回へ残すため、低速移動や高速移動量を捨てません。
一時停止・接続切断・入力欠落時には蓄積分を破棄し、復帰時の飛びを防ぎます。
BLE 通信と OS を含む実際の遅延・実サンプルレートは実機で確認してください。

起動時のジャイロオフセットは M5Unified の保存値／単発校正に頼らず、新しい平均値で置き換えます。
この校正値は RAM のみに保持し、毎回の起動で取得し直します。加速度計・磁気センサーの保存校正は保持します。
以降のバイアス追従は角速度が 1.5°/秒未満で加速度が安定した状態が 0.5 秒続いた場合だけ、
30 秒の時定数でゆっくり行います。低速の一定回転とバイアスは完全には区別できないため、
校正中は必ず本体を静止させてください。

### CoreS3 のタッチ優先と両機種のシェイク

画面に触れている間と仮想ボタン押下中は **空中マウスだけを停止**し、離してからも 150ms 停止します
（`AIR:FREEZE`）。その間もタッチ移動、クリック、スクロールは有効です。空中でドラッグしたい場合は
HOLD / DRAG をオンにして指を離してください。保持中のクリックでドラッグ固定を解除しないようにしています。

空中操作を止めたいときは強い短いシェイクで `AIR:PAUSE` にします。再度のシェイクで再開します。
3g 以上を連続 2 サンプル検出した場合にだけ切り替え、0.4 秒の静かな状態と 1.2 秒のクールダウンを
満たすまで再受付しません。通常のポインティングや長く振り続ける動作で連続切替しない設計です。
再開シェイク後も 0.4 秒は移動を凍結します。

CoreS3 の BMM150 の補正済み磁気方位は引き続き `MAG` に表示しますが、カーソルの計算には一切使いません。
磁石や金属による磁場の変化は空中マウスの方向を変えません。

### 共通ジャイロの全調整定数

`include/AirMouse.h` を変更して再ビルドします。角速度は度/秒、移動感度は HID カウント/度です。
両機種が同じ計算・既定値を使用します。ここで感度や符号を変更すると両方に適用されます。
CoreS3 の既定のポインタ計算・タッチ操作は変更していません。

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

CoreS3 の `src/main.cpp` の `kTouchPollUs=5000` はタッチ更新周期、
`kStatusIntervalMs=200` は画面の状態更新周期です。
BMI270 の設定は `ACC_CONF=0xAA`, `ACC_RANGE=0x02`, `GYR_CONF=0xEA`, `GYR_RANGE=0x00`
（400Hz、通常帯域／性能優先、±8g、±2000°/秒）です。

タッチ操作の既定値は従来どおり `kTrackpadSensitivity=1.55`、
`kScrollSensitivity=0.18`、`kClickThresholdMs=220`、`kVirtualButtonHoldMs=450`、
`kTouchMovementThresholdPixels=4`、`kScrollMovementThresholdPixels=2`。
画面下部の高さは `kVirtualButtonBarHeight=30`、`kHoldToggleHeight=30` 画素です。
マイクは `kEnableClapClick=false`、`kClapPeakThreshold=9000`、
`kClapCooldownMs=500`、`kClapSampleCount=64` です。

## CoreS3 のタッチパッドとクリックバー

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
- [StickS3 仕様・IMU 軸図・ボタン標準操作](https://docs.m5stack.com/en/core/StickS3)
- [StickS3 M5PM1・音声・電源構成](https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1)
- [Bosch BMI270 のレジスタ定義](https://github.com/boschsensortec/BMI270_SensorAPI/blob/master/bmi2_defs.h)
- M5Unified: https://github.com/m5stack/M5Unified
- ESP32-BLE-Mouse: https://github.com/T-vK/ESP32-BLE-Mouse

M5Unified 0.2.20 の `src/utility/IMU_Class.cpp` では CoreS3 の BMI270 の加速度／ジャイロ軸は
変更せず、BMM150 の Y/Z だけ反転します。本ファームは `setAxisOrder(X+,Y+,Z+)` を明示し、
LCD 表示の回転に依存しません。`src/utility/imu/BMI270_Class.cpp` のデータ準備完了マスクと
`IMU_Base.hpp` の 8g／2000°/秒の換算係数も使用しています。

## 数値回帰テスト

`tests/air_mouse_test.cpp` は外部テストライブラリを使わないホスト用 C++ テストです。
実ファームと同じ `AirMouse.h` に、両機種の方向・ロール・同等移動、静止校正の再試行、静止傾き、
バイアス追従、ソフトニー、可変 dt、小数・±127 超過分の保持、クリック凍結、切断、シェイク、
時刻周回、ホイール積分を入力します。`StickButtons.h` も単押し・二度押し・長押し・
切断中のクリック破棄・ボタン保持中の再接続・時計周回を同じ実装で検査します。

```powershell
g++ -std=c++14 -Wall -Wextra -Werror -pedantic tests\air_mouse_test.cpp -o air_mouse_test.exe
.\air_mouse_test.exe
Remove-Item air_mouse_test.exe
```
