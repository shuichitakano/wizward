# RP2350移植設計

## ゲーム共通境界

`game::Game`はシーン状態、マップ、カメラ、フレームバッファを所有し、macOS版と
RP2350版が共通で呼び出す次のAPIを提供します。

- `processInput()`：フレームごとの押下入力とシーン遷移
- `tick()`：60Hz固定のゲーム状態更新
- `render()`：Pixel Twinsの共通描画とフレームバッファのフリップ

SDL、Pico SDK、USB、PIO、DMAの型は`Game`に含めません。BGM開始と停止は`UpdateResult`の
`AudioEvent`として接続層へ返します。これにより、macOS版はSDL音声ストリームのロック下で、
RP2350版は割り込みとの排他方式の確定後に同じイベントを処理できます。

## ビルド

```sh
PICO_SDK_PATH=/path/to/pico-sdk cmake -S rp2350 -B build-rp2350 \
  -DPICO_BOARD=pico2 -DCMAKE_BUILD_TYPE=Release
cmake --build build-rp2350 --parallel
```

`rp2350/main.cpp`は共通ゲームコア、全アセット、Pixel TwinsをRP2350用にリンクし、
初期化と1フレームの共通描画までをビルド対象にします。ボード固有I/Oは未接続です。

`Game`、`Controllers`、`AudioSystem`はスタックオーバーフローを避けるため静的領域に置きます。
実行中の動的メモリ確保は追加しません。

初期スモークビルドのMAPでは、SRAMの`.data`が316,176バイト、`.bss`が
93,216バイトです。ヒープ2,048バイトを含めた汎用SRAMの未使用領域は約110KiBです。
これにはLED転送バッファとPCM DMAバッファを含みません。それらの方式は、この実測値を
基準にトレードオフを比較してから決定します。

## 次の段階

1. 実機ボードとLEDパネルの電気仕様を確定
2. Pixel TwinsにLED転送データ生成とPIO/DMA転送を追加
3. TinyUSBホスト入力を`ControllerSample`へ変換
4. PCMブロックのDMA方式とバッファ数を決定後、音声出力を接続
5. 実機でSRAM配置、最悪フレーム時間、DMAアンダーランを計測
