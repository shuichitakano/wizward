# Wizward

WizwardはPixel Twinsシステム上で動作する製品版C++ゲームです。ゲームロジックはmacOS版と
マイコン版で共有し、Pixel Twinsは`external/pixel-twins`にGitサブモジュールとして組み込みます。

## Build

```sh
python3 tools/build_assets.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DWIZWARD_BUILD_APP=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## macOS版アプリ

SDL3を使用し、Pixel Twinsと共通の描画・入力・音声コードを実行します。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWIZWARD_BUILD_APP=ON
cmake --build build --parallel
./build/wizward
```

タイトルを8秒間放置すると、左右合計20位までのランキングを10秒間表示し、強化済みの2人が
30秒間自動プレイするアトラクトモードへ移ります。タイトル、ランキング、デモのどこでも入力すると
通常ゲームを開始します。ゲーム中は2台のコントローラーの左スティックで左右の画面を独立して操作します。
描画は60Hz固定更新で、1秒ごとにフレーム処理時間と音声状態を標準エラーへ表示します。

`assets/audio/sfx.json`は効果音の編集用定義です。BGMとSFXはPixel Twinsの変換ツールで
実行時パース不要のC++定数へ変換し、`src/audio/`に格納しています。

採用バイナリはビルド時に読み取り専用C++配列へ変換され、BG、スプライト、パレットをFlashから
直接参照します。マップはゲーム開始時にシードから生成し、100×100の8-bitタイルマップ
10,000バイトと、生成時だけ使用する4-bit地形ワーク5,000バイトを使用します。

採用画像、共通パレット変換、減色レポートについては、
[画像アセット](assets/README.md)を参照してください。

Flash/RAM配置、ランタイムマップ生成、BG描画、アニメーション参照については、
[ランタイムアセットとマップ生成](docs/runtime_assets.md)を参照してください。

Pico SDKによるRP2350ビルドとmacOS版との共通ゲーム境界は
[RP2350移植設計](docs/rp2350_port.md)を参照してください。

## Clone

```sh
git clone --recurse-submodules https://github.com/shuichitakano/wizward.git
```

For an existing checkout:

```sh
git submodule update --init --recursive
```

## ライセンス

ソースコードは[MIT License](LICENSE)で公開します。画像、音楽、フォントなどのアセットには、
それぞれの出典や利用条件が適用される場合があり、MIT Licenseの対象とは限りません。
