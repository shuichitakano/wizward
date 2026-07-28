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

波形メモリは全音色共通で256サンプル・符号付き8bitです。32点の原波形からコンパイル時に
線形補間し、合計5,120バイトをSRAMへ配置します。ミックス時に16bit相当へ拡張します。

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
だけcore 1でLED用パレット変換テーブルを再生成します。Pico 2のオンボードLED（GPIO25）は
30フレームごとに反転し、ゲームループが動いていることを示します。USB入力は接続済みで、
音声はGPIO28（左）とGPIO27（右）のPWMへ48kHzステレオPCMをDMA転送します。

左右それぞれ512フレームのダブルバッファをDMAチェーンで交互に再生します。core 0のDMA完了
割り込みはゲーム処理を中断して解放済みバッファを生成しますが、USBとPIO-USBの1msタイマー
より低い優先度です。BGM操作は固定長SPSCコマンドキュー、SFXは既存の固定長SPSC要求キューで
ゲーム処理から音声割り込みへ渡し、同じ`AudioSystem`を同時に変更しません。

`Game`、`Controllers`、`AudioSystem`はスタックオーバーフローを避けるため静的領域に置きます。
実行中の動的メモリ確保は追加しません。実機デバッガでは、core 0のゲーム更新とマップ生成が
最大約7.1KiBのスタックを使うことを確認しました。音声DMA割り込みとUSB割り込みが
ゲーム処理へネストする余裕を加え、core 0は12KiB、LED専用core 1は2KiBを
予約します。core 1はSDKの自動予約を使わず、BSS上の独立した配列を
`multicore_launch_core1_with_stack()`へ渡します。両コアを2KiBのままにするとcore 0が
core 1のスタックを破壊します。

音声とFlashアセット分割を統合したELFでは、SRAMの`.data`が334,080バイト、
`.bss`が166,044バイトです。ヒープ2,048バイトの終端から12KiBのcore 0スタック下限まで、
17,732バイト（約17.3KiB）の未使用領域があります。これには音声DMAバッファと
LED転送バッファ、両コアのスタックを含みます。

## 次の段階

1. TinyUSBホスト入力を`ControllerSample`へ変換
2. LED転送を完全割り込み駆動へ変更し、core 1の待機時間を他処理へ開放
3. 実機でSRAM配置、最悪フレーム時間、DMAアンダーランを計測
