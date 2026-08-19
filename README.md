# M5Stack CoreS3 BLE HID Touchpad

M5Stack CoreS3 の液晶タッチパネルを、BLE HID マウス用のタッチパッドとして使うサンプルです。
IMU と地磁気センサーは使用しません。

## 機能

- 画面上で指をなぞると、指の移動量に応じて PC のカーソルを相対移動
- 画面を短くタップすると左クリック
- 画面を動かさずに長押ししてからなぞると左ドラッグ
- BLE HID として Windows/macOS/Linux のマウスを公開
- 画面に接続状態と現在のタッチ操作を表示

## 対応デバイス

- M5Stack CoreS3

## 開発環境

このリポジトリは PlatformIO の Arduino フレームワークを前提にしています。

```bash
pio run -e m5stack-cores3
```

CoreS3 は PlatformIO の `m5stack-cores3` ボード定義を使用します。

## 利用方法

1. CoreS3 を PC に接続する
2. `pio run -e m5stack-cores3 --target upload` で書き込む
3. PC の Bluetooth 設定で `M5Stack CoreS3 Touchpad` をペアリングする
4. 画面をなぞってカーソルを動かす
5. 画面をタップして左クリックする
6. 動かさずに長押ししてから指を動かしてドラッグする

## タッチパッドの調整

タップと移動を区別する最小の指移動量は 4 画素です。指の小さな揺れでクリックが
移動になってしまう場合は、`src/main.cpp` の `kTouchMovementThresholdPixels` を大きくしてください。

```cpp
constexpr int kTouchMovementThresholdPixels = 4;
```

カーソル感度は `kTouchSensitivity` で調整します。値を大きくすると、少ない指の移動で
カーソルが遠くまで移動します。

```cpp
constexpr float kTouchSensitivity = 1.8f;
```

長押しでドラッグを開始する時間は `kClickThresholdMs` で変更できます。

```cpp
constexpr uint32_t kClickThresholdMs = 220U;
```

## 注意事項

- BLE 接続は PC 側の Bluetooth 設定でペアリングを許可する必要があります。
- タッチ中に BLE 接続が切れた場合、タップ、移動、ドラッグ操作は PC に送信されません。

## 参考

- M5Unified: https://github.com/m5stack/M5Unified
- ESP32-BLE-Mouse: https://github.com/T-vK/ESP32-BLE-Mouse
