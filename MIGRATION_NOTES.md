# LINE Notify から LINE Messaging API への移行完了

## 🔄 変更内容

### 1. 設定ファイルの更新 (`include/config.h`)
```cpp
// 変更前
const char* line_notify_token = "YOUR_LINE_NOTIFY_TOKEN";
const bool enable_line_notify = true;

// 変更後
const char* line_channel_access_token = "YOUR_CHANNEL_ACCESS_TOKEN";
const char* line_user_id = "YOUR_USER_ID";
const bool enable_line_messaging = true;
```

### 2. APIエンドポイントの変更
```cpp
// 変更前: LINE Notify API
POST https://notify-api.line.me/api/notify
Content-Type: application/x-www-form-urlencoded
Authorization: Bearer {token}

// 変更後: LINE Messaging API
POST https://api.line.me/v2/bot/message/push
Content-Type: application/json
Authorization: Bearer {channel_access_token}
```

### 3. メッセージ形式の変更
```cpp
// 変更前: URL-encoded form data
"message=" + message

// 変更後: JSON format
{
  "to": "USER_ID",
  "messages": [
    {
      "type": "text",
      "text": "message_content"
    }
  ]
}
```

## 🚀 利点

### LINE Messaging API の利点
1. **将来性**: LINE NotifyのサポートおよびAPI廃止への対応
2. **豊富な機能**: 
   - リッチメッセージ（Flex Message）
   - 画像・動画・音声メッセージ
   - クイックリプライ
   - プッシュメッセージ
3. **統合性**: LINE公式アカウントとの連携
4. **柔軟性**: より詳細な設定とカスタマイズが可能

### フリープラン制限
- 月1,000通まで無料（防犯カメラ用途には十分）
- レート制限: 500リクエスト/秒

## 📝 セットアップ手順

### 1. LINE Developers Console での設定
1. [LINE Developers Console](https://developers.line.biz/console/) でプロバイダー作成
2. Messaging APIチャンネル作成
3. Channel Access Token取得
4. ボットと友達登録してUser ID取得

### 2. ESP32での設定
```cpp
// config.h の設定
const char* line_channel_access_token = "YOUR_ACTUAL_TOKEN_HERE";
const char* line_user_id = "U1234567890abcdef...";  // Uから始まるID
```

## 🔧 動作確認

### 正常動作時のシリアル出力
```
LINE message sent successfully
```

### エラー時の対処
```
LINE message failed with code: 401
→ Channel Access Tokenを確認

LINE message failed with code: 400
→ JSONフォーマットとUser IDを確認

LINE message failed with code: 403
→ User IDとBot設定を確認
```

## 📚 詳細ドキュメント

詳細なセットアップ手順は `docs/LINE_MESSAGING_API_SETUP.md` を参照してください。

## ✅ 移行作業完了項目

- [x] config.h の設定変更
- [x] メイン関数の更新
- [x] API呼び出し関数の実装
- [x] エラーハンドリングの更新
- [x] READMEの更新
- [x] セットアップガイドの作成
- [x] 開発ノートの更新

## 🎯 次のステップ

1. LINE Developers Console でチャンネル作成
2. Channel Access Token の取得と設定
3. テストメッセージ送信での動作確認
4. 実際の人物検知での通知テスト