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

## BGMドラム音源

BGMのSnare、Hat、Percussionは、8声のBGM波形メモリ音源とは別の15-bit最大長LFSRノイズ
1声を共有します。LFSRは48kHzの各出力サンプルで1回更新し、ハイパスフィルターは使用しません。
同時発音時の優先度はSnare、Percussion、Hatの順です。Snareは同じ専用チャンネル内で
180Hzから105Hzへ50msで下降する三角波を加算し、65msで消音します。最大構成は
8 BGM波形声 + 1 BGMノイズ声 + 8 SFX波形声です。

## ビルド

```sh
PICO_SDK_PATH=/path/to/pico-sdk cmake -S rp2350 -B build-rp2350 \
  -DPICO_BOARD=pico2 -DCMAKE_BUILD_TYPE=Release \
  -DWIZWARD_PIXEL_TWINS_DIR=/path/to/pixel-twins
cmake --build build-rp2350 --parallel
```

`rp2350/main.cpp`は共通ゲームコア、全アセット、Pixel TwinsをRP2350用にリンクします。
core 0はゲーム更新と描画を担当し、core 1はPIO/DMA LEDドライバを連続駆動します。core 1が
新しい表示バッファを受理して旧バッファを解放したことをackしてから、core 0が旧バッファを
次の描画へ再利用します。これによりダブルフレームバッファを競合なく共有し、ゲーム描画中も
PWM走査を止めません。

LED転送完了を1tickの基準としてタイトル、ランキング、アトラクトデモを更新し、シーン変更時
だけcore 1でLED用パレット変換テーブルを再生成します。USB入力とPCM DMAは未接続です。

音声出力が未接続の段階では`WIZWARD_BUILD_AUDIO_DATA=OFF`として、BGM/SFX定数をFlashへ
リンクしません。PCM DMA接続時にPixel Twinsの音声型と生成データを同期して有効化します。

`Game`、`Controllers`、`AudioSystem`はスタックオーバーフローを避けるため静的領域に置きます。
実行中の動的メモリ確保は追加しません。実機デバッガでは、core 0のゲーム更新とマップ生成が
最大約7.1KiBのスタックを使うことを確認したため、core 0は8KiB、LED専用core 1は2KiBを
予約します。core 1はSDKの自動予約を使わず、BSS上の独立した配列を
`multicore_launch_core1_with_stack()`へ渡します。両コアを2KiBのままにするとcore 0が
core 1のスタックを破壊します。

LED統合後の実機ELFでは、SRAMの`.data`が337,864バイト、`.bss`が157,708バイトです。
ヒープ2,048バイトの終端からcore 0スタック下限までの未使用領域は約25.8KiBです。
これにはLED転送バッファと両コアのスタックを含み、PCM DMAバッファは含みません。
PCM DMA方式はこの残容量を基準に決定します。

## 次の段階

1. TinyUSBホスト入力を`ControllerSample`へ変換
2. PCMブロックのDMA方式とバッファ数を決定後、音声出力を接続
3. LED転送を完全割り込み駆動へ変更し、core 1の待機時間を他処理へ開放
4. 実機でSRAM配置、最悪フレーム時間、DMAアンダーランを計測
