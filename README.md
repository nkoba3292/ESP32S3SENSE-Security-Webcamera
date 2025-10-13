# XIAO ESP32S3 Sense 防犯監視システム

🎯 **目的**  
人物をAI推論で検知し、即座に警告・通知・記録を行う防犯監視システム

## 🧩 機能仕様

| 機能 | 内容 |
|------|------|
| 人物検知 | AI推論モデル（TFLite Micro）により人物を検出 |
| 警告出力 | 検知後、電子ブザーを10秒間鳴動 |
| LED点滅 | 2個のLEDを0.5秒周期で交互に点滅（逆位相）、10秒間継続 |
| LINE通知 | 検知時にLINE Messaging APIでメッセージを送信 |
| 映像記録 | 検知後の30秒間の映像をSDカードに保存（動画形式） |
| ストリーミングリンク | LINE通知にストリーミングURLを含めて送信 |
| 電源 | USB給電またはソーラ給電（自動切替または手動選択） |

## 🔧 ハードウェア構成

| デバイス | 接続先 | 備考 |
|----------|--------|------|
| XIAO ESP32S3 Sense | 中核マイコン | カメラ・IMU・マイク内蔵 |
| 電子ブザー | GPIO D2 | 10秒間鳴動 |
| LED ×2 | GPIO D3, D4 | 0.5秒周期で逆位相点滅 |
| microSDカード | SPI接続 | 映像保存用 |
| 電源 | USB-C / ソーラ | 5V入力、電源切替対応 |

## 🧠 ソフトウェア構成

- **esp32-camera**: OV2640制御、JPEG/動画取得
- **esp-tflite-micro**: 人物検出モデル（int8量子化）
- **SD_MMC**: SDカードへの動画保存
- **HTTPClient**: LINE Messaging API送信
- **ESPAsyncWebServer**: ストリーミングURL提供
- **FreeRTOS**: LED点滅・ブザー・録画・通知を並列処理

## 🔄 処理フロー（人物検知イベント）

```
[起動]
  └─ Wi-Fi接続 → AIモデル初期化 → カメラ準備完了

[人物検知]
  └─ AI推論で人物を検出（推論周期：1秒）

[検知後の処理（並列）]
  ├─ 電子ブザー ON（10秒）
  ├─ LED1: ON/OFF 0.5秒周期（10秒）
  ├─ LED2: OFF/ON 0.5秒周期（10秒） ← LED1と逆位相
  ├─ LINE Notify送信（テキスト＋ストリーミングURL）
  └─ 映像録画開始（30秒） → SDカード保存

[待機]
  └─ 次の検知までAI推論ループ継続
```

## 📡 通知内容（LINE）

```
【防犯通知】人物を検知しました
時刻: 2025-10-13 14:30:25
映像ストリーミング: http://192.168.1.100/stream
```

## 🛠️ セットアップ手順

### 1. ハードウェア接続

```
XIAO ESP32S3 Sense:
├─ ブザー → D2 (GPIO 2)
├─ LED1 → D3 (GPIO 3)  
├─ LED2 → D4 (GPIO 4)
└─ microSD → 内蔵スロット
```

### 2. ソフトウェア設定

1. **PlatformIO** でプロジェクトを開く
2. `include/config.h` を編集:
   ```cpp
   const char* wifi_ssid = "あなたのWiFi名";
   const char* wifi_password = "あなたのWiFiパスワード";
   const char* line_channel_access_token = "あなたのチャンネルアクセストークン";
   const char* line_user_id = "送信先のユーザーID";
   ```
3. LINE Messaging APIの設定:
   - [LINE Developers Console](https://developers.line.biz/console/) でチャンネル作成
   - Messaging API設定でChannel Access Tokenを取得
   - 送信先ユーザーのUser IDを取得
4. ビルド＆アップロード

### 3. 動作確認

1. シリアルモニター（115200bps）で起動ログを確認
2. WiFi接続とIPアドレスを確認
3. ブラウザで `http://[IPアドレス]/` にアクセス
4. `/stream` でライブストリーミング確認
5. 人物検知でアラート動作確認

## 📁 プロジェクト構造

```
XiaoESP32S3_SecurityCamera/
├── platformio.ini          # PlatformIO設定
├── include/
│   ├── security_camera.h   # メインヘッダー
│   └── config.h           # 設定ファイル
├── src/
│   └── main.cpp           # メインプログラム
├── lib/                   # カスタムライブラリ
├── data/                  # SPIFFS用データ
├── models/                # AIモデルファイル
└── web/                   # Webインターフェース
```

## 🔧 カスタマイズ

### 検知感度調整
```cpp
// config.h
const float detection_threshold = 0.7;  // 0.0-1.0（高いほど厳しい）
const int detection_cooldown_ms = 5000; // 連続検知防止時間
```

### アラート設定
```cpp
// config.h
const bool enable_buzzer = true;        // ブザーON/OFF
const bool enable_led_alerts = true;    // LED警告ON/OFF
const bool enable_line_notify = true;   // LINE通知ON/OFF
const bool enable_recording = true;     // 録画ON/OFF
```

### カメラ設定
```cpp
// config.h
const int camera_frame_size = FRAMESIZE_SVGA;  // 解像度
const int camera_jpeg_quality = 12;            // 画質(0-63)
```

## 🚀 今後の展開候補

- [ ] Web UIで録画映像の再生・履歴管理
- [ ] Firebase連携によるクラウド保存
- [ ] 音声警告（I2S DAC）やIMU連動アラート
- [ ] 電源切替の自動判定（USB vs ソーラ）
- [ ] 顔認識による人物識別
- [ ] モーション検知との組み合わせ
- [ ] 夜間赤外線撮影対応

## 📊 システム要件

- **RAM使用量**: 約100KB（PSRAM使用）
- **Flash使用量**: 約1.5MB
- **消費電力**: 約200mA（WiFi通信時）
- **SD容量**: 最低8GB推奨
- **WiFi**: 2.4GHz 802.11b/g/n

## 🔍 トラブルシューティング

### カメラが起動しない
```cpp
// カメラピン設定を確認
// XIAO ESP32S3 Senseの場合、既定義のピン配置を使用
```

### WiFi接続できない
```cpp
// config.h でSSID/パスワードを確認
// 2.4GHz帯のWiFiを使用しているか確認
```

### LINE通知が届かない
```cpp
// チャンネルアクセストークンが正しいか確認
// ユーザーIDが正しいか確認
// Messaging APIが有効になっているか確認
// Bot設定でWebhookが正しく設定されているか確認
```

### 録画ファイルが作成されない
```cpp
// SDカードがマウントされているか確認
// ファイル名の生成が正しいか確認
```

## 📝 ライセンス

このプロジェクトはMITライセンスの下で公開されています。

## 👤 作成者

Security Camera System Project  
Date: 2025-10-13