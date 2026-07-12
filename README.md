# m5stack-tts

M5Stack Basic（ESP32）向けのPlatformIOプロジェクトです。

## 初回セットアップ

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
```

## ビルド

```sh
.venv/bin/pio run
```

## 書き込み

M5Stack BasicをUSBで接続して実行します。

```sh
.venv/bin/pio run --target upload
```

## シリアルモニター

```sh
.venv/bin/pio device monitor
```
