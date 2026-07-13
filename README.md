# m5stack-tts

M5Stack Basic（ESP32）向けのPlatformIOプロジェクトです。

## 初回セットアップ

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
```

## Wi-Fi・天気設定

サンプルをコピーして、接続先のSSIDとパスワードを設定します。

```sh
cp include/secrets.example.h include/secrets.h
```

`include/secrets.h` にWi-FiのSSIDとパスワード、OpenWeather APIキー、天気を取得する地点の緯度・経度を設定してください。このファイルはGitの管理対象外です。

起動時にWi-Fiへ接続した後、NTPサーバーから時刻を取得します。時刻は日本標準時（JST）で液晶とシリアルモニターに表示されます。

現在の天気、気温、湿度、気圧、直近1時間の雨量は起動時と10分ごとにOpenWeatherから取得します。ボタンAを押すと手動更新できます。

## SDカード

FAT32でフォーマットしたmicroSDカードを電源投入前に挿入してください。カードが認識されると、天気情報の取得成功時に `/weather.csv` へ次の形式で追記します。

```csv
datetime,weather,temp_c,humidity_pct,pressure_hpa,rain_1h_mm
2026-07-13 12:00:00,Clouds,28.4,72,1008,0.0
```

SDカードがない場合や初期化に失敗した場合も、時計と天気表示は継続します。

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
