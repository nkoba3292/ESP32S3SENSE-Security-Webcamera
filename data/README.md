# Data Directory

このディレクトリには、SPIFFS（SPI Flash File System）に保存するWebファイルやその他のデータファイルを配置します。

## ファイル構成

- **index.html** - メインのWebインターフェース
- **style.css** - スタイルシート
- **script.js** - JavaScript
- **config.json** - システム設定ファイル

## SPIFFS の使用方法

1. このディレクトリにファイルを配置
2. PlatformIO で "Upload File System image" を実行
3. ESP32のフラッシュメモリにファイルがアップロードされます

## アクセス方法

SPIFFS にアップロードされたファイルは、ESP32 のコード内で以下のようにアクセスできます：

```cpp
#include "SPIFFS.h"

void setup() {
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        return;
    }
    
    File file = SPIFFS.open("/index.html", "r");
    if (file) {
        // ファイルの読み取り処理
        file.close();
    }
}
```

## 容量制限

- SPIFFS 領域: 約1.5MB
- 推奨ファイルサイズ: 個別ファイル500KB以下
- 合計使用量: 1MB以下を推奨