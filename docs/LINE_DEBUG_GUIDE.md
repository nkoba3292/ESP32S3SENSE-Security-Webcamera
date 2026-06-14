# LINE通知デバッグガイド

## 🐛 デバッグ機能の使い方

### 1. シリアルモニターでの確認

起動時に以下の情報が表示されます：

```
=== LINE Messaging Configuration ===
Messaging Enabled: YES/NO
Channel Token Configured: YES/NO
Channel Token Length: XXX characters
User ID Configured: YES/NO
User ID: Uxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
User ID Length: XX characters
User ID Format Check: OK (starts with U) / ERROR
===================================
```

### 2. Webインターフェースでのテスト

デバイスのIPアドレスにアクセスして、以下の機能を使用：

#### LINE設定確認
```
http://[デバイスIP]/line-config
```
- WiFi接続状態
- トークン設定状態
- ユーザーID設定状態
- 各設定の詳細情報

#### LINE通知テスト
```
http://[デバイスIP]/test-line
```
- 手動でテスト通知を送信
- シリアルモニターに詳細なデバッグ情報を出力

### 3. 詳細デバッグ出力

LINE通知送信時、シリアルモニターに以下の情報が表示されます：

```
=== LINE Message Debug ===
✓ WiFi connected: 192.168.1.100
✓ Channel Access Token configured (length: XXX)
✓ User ID configured: Uxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
✓ LINE messaging enabled
✓ HTTP connection established
✓ Headers set
JSON Payload:
{"to":"Uxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx","messages":[{"type":"text","text":"メッセージ内容"}]}
Payload size: XXX bytes
Sending POST request...
HTTP Response Code: 200
Response body:
{}
✓✓✓ LINE message sent successfully! ✓✓✓
=== LINE Message Debug End ===
```

## 🔍 エラーコードと対処法

### HTTP 400 (Bad Request)
**原因:**
- User IDの形式が不正
- JSONペイロードの形式エラー

**対処法:**
```cpp
// config.h を確認
const char* line_user_id = "YOUR_USER_ID";  // ← これを正しいUser IDに変更

// User IDは以下の形式である必要があります：
// - 必ず 'U' で始まる
// - 33文字の長さ
// - 例: "Uxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
```

### HTTP 401 (Unauthorized)
**原因:**
- Channel Access Tokenが無効または期限切れ

**対処法:**
1. LINE Developers Consoleにアクセス
2. チャンネルを選択
3. 「Messaging API設定」タブ
4. Channel Access Tokenを再発行
5. `config.h` に新しいトークンを設定

```cpp
// config.h
const char* line_channel_access_token = "YOUR_CHANNEL_ACCESS_TOKEN";  // ← 新しいトークンに更新
```

### HTTP 403 (Forbidden)
**原因:**
- User IDが存在しない
- ボットがブロックされている
- ボットとUser IDの関連付けエラー

**対処法:**
1. ボットと友達になっているか確認
2. User IDを再取得
3. ボットのブロックを解除

### HTTP 429 (Rate Limit)
**原因:**
- API呼び出し回数制限を超えた

**対処法:**
- しばらく待ってから再試行
- `detection_cooldown_ms` を増やして通知頻度を下げる

```cpp
// config.h
const int detection_cooldown_ms = 30000;  // 5秒→30秒に変更
```

### WiFi not connected
**原因:**
- WiFi接続が切れている

**対処法:**
```cpp
// config.h でWiFi設定を確認
const char* wifi_ssid = "YOUR_WIFI_SSID";      // ← 正しいSSID
const char* wifi_password = "YOUR_WIFI_PASSWORD";  // ← 正しいパスワード
```

## 📝 設定チェックリスト

### 必須設定項目

- [ ] WiFi SSID設定済み
- [ ] WiFi パスワード設定済み
- [ ] LINE Channel Access Token設定済み（YOUR_CHANNEL_ACCESS_TOKENから変更）
- [ ] LINE User ID設定済み（YOUR_USER_IDから変更）
- [ ] User IDが'U'で始まる
- [ ] User IDの長さが30文字以上
- [ ] `enable_line_messaging` が `true`
- [ ] ボットと友達登録済み

### 設定例

```cpp
// config.h の正しい設定例

// WiFi Settings
const char* wifi_ssid = "MyHomeWiFi";
const char* wifi_password = "MySecurePassword123";

// LINE Messaging API Settings
const char* line_channel_access_token = "abcdefghijklmnopqrstuvwxyz1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ==";
const char* line_user_id = "U1234567890abcdefghijklmnopqrstuvw";

// Enable LINE messaging
const bool enable_line_messaging = true;
```

## 🧪 テスト手順

### ステップ1: 起動確認
1. デバイスをUSBに接続
2. シリアルモニター（115200 baud）を開く
3. 起動ログで設定状態を確認

### ステップ2: WiFi接続確認
```
WiFi connected! IP address: 192.168.1.100
```
が表示されることを確認

### ステップ3: LINE設定確認
ブラウザで以下にアクセス：
```
http://[デバイスIP]/line-config
```
すべての項目が「✓ 設定済み」であることを確認

### ステップ4: 手動テスト
ブラウザで以下にアクセス：
```
http://[デバイスIP]/test-line
```
LINEアプリにテストメッセージが届くことを確認

### ステップ5: 自動検出テスト
カメラの前で動いて、人物検出が作動することを確認
LINEに通知が届くことを確認

## 🔧 高度なデバッグ

### SSL/TLS証明書エラー
LINE APIはHTTPSを使用するため、証明書検証が必要です。

**対処法:**
ESP32のファームウェアを最新に更新

### メモリ不足エラー
大量の通知やストリーミングでメモリ不足になる場合：

```cpp
// config.h でフレームサイズを小さくする
const int camera_frame_size = FRAMESIZE_VGA;  // SVGAからVGAに変更
```

### タイムアウトエラー
ネットワークが遅い場合、タイムアウトを増やす：

```cpp
// main.cpp の sendLINEMessage() 関数で
http.setTimeout(20000);  // 10秒→20秒に変更
```

## 📊 ログレベルの調整

より詳細なデバッグ情報が必要な場合：

```ini
; platformio.ini に追加
build_flags = 
    -DCORE_DEBUG_LEVEL=5  ; 0=None, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Verbose
```

## 💡 トラブルシューティングFAQ

### Q: テストは成功するのに自動検出時に送信されない
A: `enable_line_messaging` が有効で、検出のクールダウン期間が経過しているか確認

### Q: 同じメッセージが複数回送られる
A: `detection_cooldown_ms` を大きくする（例: 30000 = 30秒）

### Q: User IDの取得方法がわからない
A: `docs/LINE_MESSAGING_API_SETUP.md` を参照

### Q: Channel Access Tokenの有効期限は？
A: 無期限ですが、セキュリティのため定期的に再発行を推奨

## 🎯 まとめ

デバッグ機能を活用して：
1. **起動時ログ**でざっくり確認
2. **Webインターフェース**で詳細確認
3. **手動テスト**で動作確認
4. **シリアルログ**で詳細分析

これでLINE通知の問題をすばやく特定・解決できます！
