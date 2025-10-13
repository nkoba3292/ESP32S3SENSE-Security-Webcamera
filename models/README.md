# AI Models Directory

このディレクトリには、人物検知用のTensorFlow Lite Microモデルファイルを配置します。

## 必要なファイル

1. **person_detection_model.tflite** - 人物検知用の量子化済みモデル
2. **model_settings.h** - モデルの設定ヘッダーファイル
3. **person_detection_model_data.cc** - モデルデータのC++配列

## モデル仕様

- **入力サイズ**: 96x96x3 (RGB)
- **出力**: 人物検知の信頼度スコア (0.0-1.0)
- **量子化**: int8量子化済み
- **メモリ使用量**: 約300KB
- **推論時間**: 約200ms (ESP32S3 @ 240MHz)

## モデルの取得方法

1. TensorFlow Model Gardenから事前学習済みモデルをダウンロード
2. TensorFlow Lite Converterで量子化変換
3. xxd コマンドでC++配列に変換

```bash
# TensorFlow Liteモデルの変換例
python convert_model.py --input person_detection.pb --output person_detection.tflite

# C++配列への変換
xxd -i person_detection.tflite > person_detection_model_data.cc
```

## 使用方法

モデルファイルをこのディレクトリに配置後、main.cppの`initializeAI()`関数でモデルを読み込みます。

## 注意事項

- モデルファイルは著作権の関係でリポジトリには含まれていません
- 商用利用の場合は適切なライセンスのモデルを使用してください