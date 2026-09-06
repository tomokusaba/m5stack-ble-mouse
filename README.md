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

**画面を自分に向け、USB-C ポートを下にした縦向き**で持ちます。M5Unified の CoreS3 実装は
BMI270 の加速度・ジャイロを論理座標（`+X`: 画面右、`+Y`: 画面上、`+Z`: 画面手前）で提供します。
ジャイロの `Y` 軸回転を横移動、`X` 軸回転を縦移動へ変換するため、加速度計の重力値が安定し、
縦持ちと判定されるときだけカーソルを送信します。これにより、置く・振るなどの加速でカーソルが
動くことを抑えます。

BMM150 は M5Unified が CoreS3 向けの軸補正を適用して読み取り、加速度計で傾きを補正した方位を
画面の `MAG` と方位表示に使用します。磁場は周囲の磁石や金属で変動するため、相対ポインティング
自体はジャイロのみで行い、磁気ドリフトをカーソル操作へ持ち込みません。

空中操作を止めたいときは、CoreS3 を短く強く振ってください。`AIR:PAUSE` 表示の間は画面タッチと
ボタン操作だけが有効です。再度振ると `AIR:ON` に戻ります。

### 空中マウスの調整

`src/main.cpp` の定数を変更して再ビルドできます。

```cpp
constexpr float kAirMouseDeadzoneDps = 2.0f;
constexpr float kAirMousePixelsPerDps = 0.045f;
constexpr int kAirMouseXSign = 1;
constexpr int kAirMouseYSign = -1;
```

- 小さな揺れで動く場合は `kAirMouseDeadzoneDps` を大きくします。
- 感度は `kAirMousePixelsPerDps` で変更します。
- 左右または上下が逆なら、それぞれの `kAirMouseXSign`／`kAirMouseYSign` を `1` と `-1` の間で反転します。

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

- M5Unified: https://github.com/m5stack/M5Unified
- ESP32-BLE-Mouse: https://github.com/T-vK/ESP32-BLE-Mouse
