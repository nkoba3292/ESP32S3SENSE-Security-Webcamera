# LINE Messaging API セットアップガイド

LINE Notifyの廃止に伴い、LINE Messaging APIを使用して通知機能を実装します。

## 1. LINE Developers Console でのセットアップ

### 1.1 プロバイダー作成
1. [LINE Developers Console](https://developers.line.biz/console/) にアクセス
2. LINEビジネスIDでログイン
3. 「新規プロバイダー作成」をクリック
4. プロバイダー名を入力（例：SecurityCamera）

### 1.2 Messaging APIチャンネル作成
1. 作成したプロバイダーを選択
2. 「Messaging API」を選択
3. 以下の情報を入力：
   - チャンネル名：XIAO ESP32S3 Security Camera
   - チャンネル説明：防犯カメラシステム
   - 大業種：その他
   - 小業種：その他

### 1.3 Channel Access Token 取得
1. 作成したチャンネルを選択
2. 「Messaging API設定」タブを開く
3. 「Channel access token」の「発行」ボタンをクリック
4. 生成されたトークンをコピーして保存

## 2. ユーザーIDの取得

### 2.1 LINEボットとの友達登録
1. Messaging API設定画面でQRコードをスキャン
2. 作成したボットと友達になる

### 2.2 ユーザーIDの取得方法

**方法A: Webhook経由で取得**
1. Messaging API設定で「Webhookの利用」を有効にする
2. Webhook URLを設定（テスト用）
3. ボットにメッセージを送信してUser IDをログから取得

**方法B: LINE公式ツールを使用**
1. [LINE公式アカウント管理画面](https://manager.line.biz/) にアクセス
2. 作成したアカウントを選択
3. 設定 > 応答設定 > 詳細設定でUser IDを確認

## 3. ESP32での設定

### 3.1 config.h の更新

```cpp
// LINE Messaging API Settings
const char* line_channel_access_token = "YOUR_CHANNEL_ACCESS_TOKEN_HERE";
const char* line_user_id = "YOUR_USER_ID_HERE";  // Uから始まるID

// 通知設定
const bool enable_line_messaging = true;
```

### 3.2 送信メッセージの例

```json
{
  "to": "USER_ID",
  "messages": [
    {
      "type": "text",
      "text": "【防犯通知】人物を検知しました\n時刻: 2025-10-13 14:30:25\n映像ストリーミング: http://192.168.1.100/stream"
    }
  ]
}
```

## 4. API使用量・制限

### 4.1 メッセージ送信制限
- **フリープラン**: 月1,000通まで無料
- **有料プラン**: 追加メッセージは従量課金

### 4.2 レート制限
- 最大 500 リクエスト/秒
- 防犯カメラ用途では十分な制限

## 5. トラブルシューティング

### 5.1 メッセージが送信されない
```
エラーコード 401: Channel Access Tokenが無効
→ トークンを再発行して設定し直す

エラーコード 400: リクエスト形式が不正
→ JSON形式とContent-Typeを確認

エラーコード 403: User IDが無効
→ 正しいUser IDを取得し直す
```

### 5.2 友達登録の確認
1. LINE公式アカウント管理画面でフォロワー数を確認
2. テストメッセージを手動送信して受信確認

## 6. セキュリティ考慮事項

### 6.1 トークン管理
- Channel Access Tokenは秘匿情報として管理
- Gitリポジトリにはコミットしない
- 定期的にトークンを再発行

### 6.2 User ID保護
- User IDも個人情報として適切に管理
- 不要になったら削除

## 7. 高度な機能

### 7.1 リッチメッセージ
```json
{
  "type": "flex",
  "altText": "防犯通知",
  "contents": {
    "type": "bubble",
    "body": {
      "type": "box",
      "layout": "vertical",
      "contents": [
        {
          "type": "text",
          "text": "人物検知アラート",
          "weight": "bold",
          "color": "#FF0000"
        }
      ]
    }
  }
}
```

### 7.2 画像付きメッセージ
- 検知時のスナップショット画像を添付
- 画像はHTTPS経由でアクセス可能な場所に配置

## 8. 参考リンク

- [LINE Messaging API Documentation](https://developers.line.biz/ja/reference/messaging-api/)
- [LINE Developers Console](https://developers.line.biz/console/)
- [LINE公式アカウント管理画面](https://manager.line.biz/)
- [Messaging API料金](https://www.linebiz.com/jp/service/line-official-account/plan/)