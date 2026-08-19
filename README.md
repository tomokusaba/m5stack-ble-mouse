# M5StickS3 BLE HID Mouse

M5StickS3 の IMU を使って、BLE HID として Windows/macOS/Linux のマウス操作を行うサンプルです。

## 機能
- M5StickS3 の IMU を使って傾きでマウスを移動
- BLE HID として PC へマウスを公開
- 画面表示で接続状態とボタン状態を確認
- 低速な揺れを抑えるためのデッドゾーンと平滑化

## 対応デバイス
- M5StickS3
- M5StickC / CPlus などの互換環境でも概ね動作可能

## 必要なもの
- M5StickS3
- USB ケーブル
- PlatformIO
- Windows/macOS/Linux PC

## 開発環境
このリポジトリは PlatformIO の Arduino フレームワークを前提にしています。

```bash
pio run -e m5stack-sticks3
```

M5StickS3 は PlatformIO の標準ボード定義に入っていない場合があるため、リポジトリには `boards/m5stack-sticks3.json` を含めています。

## 利用方法
1. M5StickS3 を PC に接続する
2. `pio run -e m5stack-sticks3` で書き込む
3. PC の Bluetooth 設定で接続を許可する
4. M5StickS3 を傾けてマウスを動かす
5. Side のボタンで左クリックをトグルする

## 仕組み
- IMU から重力ベクトルを取得して傾き角を計算する
- 傾き角を相対移動量に変換する
- `ESP32-BLE-Mouse` ライブラリを使って BLE HID のマウスとして送出する

## 注意事項
- IMU は絶対位置ではなく「傾きの変化」を使うため、微細な揺れや重力補正が必要です
- クリックは M5StickS3 のボタン操作を簡略化した実装です
- BLE 接続は PC 側の Bluetooth 設定でペアリングを許可する必要があります

## 参考
- M5Unified: https://github.com/m5stack/M5Unified
- ESP32-BLE-Mouse: https://github.com/T-vK/ESP32-BLE-Mouse
