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
