# Wizward画像アセット

本番で採用する画像と、Pixel Twinsの共通パレットへ変換した中間アセットを管理します。

```text
assets/
├── adopted_inventory.json   採用元一覧
├── manifests/               パレットセット別の変換マニフェスト
├── source/                  プロトタイプから収集した採用原画像
└── converted/               共通パレット付き8-bit PNGと減色レポート
```

## 再収集と変換

兄弟ディレクトリに`wizward-prototype`と`pixel-twins`がある構成では、次の手順で再生成できます。

```sh
python3 tools/build_assets.py
```

このコマンドは収集、共通パレット変換、スプライトパック、固定BGアトラス、整合性検証を
順に実行します。個別に実行する場合は次のコマンドを使用します。

```sh
python3 tools/collect_selected_assets.py

cd ../pixel-twins/tools/asset_converter
uv run pixel-twins-assets ../../../wizward/assets/manifests/gameplay.json \
  -o ../../../wizward/assets/converted/gameplay --clean
uv run pixel-twins-assets ../../../wizward/assets/manifests/title.json \
  -o ../../../wizward/assets/converted/title --clean
uv run pixel-twins-sprites ../../../wizward/assets/converted/gameplay/intermediate.json \
  -o ../../../wizward/assets/converted/gameplay/sprites.bin --namespace wizward::assets
uv run pixel-twins-background ../../../wizward/assets/manifests/background.json \
  -o ../../../wizward/assets/converted/gameplay/background.bin
uv run --with pillow python ../../../wizward/tools/verify_converted_assets.py
```

`gameplay`は通常プレイ中に同時使用し得る全アセットを1つのパレットへ量子化します。
インデックス2〜17はラインや演出用、18〜31はフォント、プレイヤーマーカー、UI用の固定色、
32〜254は画像最適化用です。タイトル画面は独立した`title`パレットを使用します。

`source`と`manifests`は後段のバイナリ形式が変わっても再利用できます。`converted`の
`intermediate.json`がPixel Twins用バイナリ生成モジュールとの境界です。

`gameplay/palette.bin`と`title/palette.bin`はRGB888順の256色、各768バイトです。
`gameplay/background.bin`は現行ランタイム生成器で必要な129論理パターンを、画素が完全一致する
パターンを共有して123物理パターンに格納します。生成された`background.hpp`と`sprites.hpp`は
ゲームコードから安定したenumでアセットを参照するために使用します。

タイトルセットは`screen.bin`へ160×120の8-bit生画素、`logo.bin`へ透明外周を除いたロゴを
生成します。いずれもタイトル専用`palette.bin`とともにFlashへ配置します。

`converted/memory_layout.json`と`memory_layout.md`には、ゲームプレイ画像のSRAM使用量、Flashに
残す低頻度データ、ダブルフレームバッファとマップを含むRP2350の残容量を出力します。
