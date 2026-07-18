# Wizward

Wizward is the production C++ game built on the Pixel Twins system layer.

The game logic is shared between the macOS and microcontroller targets.
Pixel Twins is included as a Git submodule under `external/pixel-twins`.

## Build

```sh
python3 tools/build_assets.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## アセット動作デモ

SDL3を使用して、タイトル画面、生成地形、ゲーム中スプライト、フォントを実データから表示できます。

```sh
cmake -S . -B build-demo -DCMAKE_BUILD_TYPE=Release -DWIZWARD_BUILD_DEMO=ON
cmake --build build-demo --parallel
./build-demo/wizward_asset_demo
```

タイトルは約2.5秒後、またはSpace/Startでゲーム画面へ切り替わります。ゲーム画面では左を
WASD、右を矢印キーでスクロールできます。Space/Startでタイトルへ戻ります。

採用バイナリはビルド時に読み取り専用C++配列へ変換され、BG、スプライト、パレットをFlashから
直接参照します。マップはゲーム開始時にシードから生成し、100×100の8-bitタイルマップ
10,000バイトと、生成時だけ使用する4-bit地形ワーク5,000バイトを使用します。

採用画像、共通パレット変換、減色レポートについては、
[画像アセット](assets/README.md)を参照してください。

Flash/RAM配置、ランタイムマップ生成、BG描画、アニメーション参照については、
[ランタイムアセットとマップ生成](docs/runtime_assets.md)を参照してください。

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
