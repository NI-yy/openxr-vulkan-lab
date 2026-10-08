# Quest 2 Multiview 比較（Issue #7）

## 実装

起動時に Vulkan 1.1 の `VkPhysicalDeviceMultiviewFeatures::multiview` を問い合わせる。対応しており、Multiview が要求された場合は機能を有効化し、2 層の OpenXR 色 Swapchain と 2 層の深度画像を作る。render pass の view mask は `0b11` で、各立方体を 1 draw call で左右へ描く。頂点 Shader は `gl_ViewIndex` で左右それぞれの MVP 行列を選ぶ。OpenXR Projection Layer の左右の view は、同じ Swapchain の array layer 0 と 1 を参照する。

Multiview が使えない端末では、左右別 Swapchain と左右別 render pass の Dual Pass を選ぶ。`debug.openxrvulkanlab.mode` を `dual-pass` にすれば、対応端末でも Dual Pass を選べる。値が空または `multiview` のときは対応していれば Multiview を選ぶ。起動ログの `Vulkan multiview: supported=... requested=... selected=...` と `Benchmark config` で選択結果を確認する。

## 比較条件と手順

[Dual Pass 基準性能](dual-pass-benchmark.md)と同じ、100 個の立方体、片眼 1024 × 1024、1 sample、同じ色・深度形式、300 描画フレームのウォームアップ、1800 描画フレームの計測を使う。Shader の MVP は各眼の OpenXR View 姿勢と FOV から作る。Multiview の GPU 時間は 1 回の Vulkan submission の TOP_OF_PIPE から BOTTOM_OF_PIPE までを timestamp query で測る。Dual Pass の `total_gpu` は左右別 submission の GPU 時間の和であり、両方式とも OpenXR compositor や CPU 待機時間は含まない。`frame_interval` も併記する。

1. [Dual Pass の再測定手順](dual-pass-benchmark.md#再測定手順)と同様に Quest 2 を冷まし、Guardian を完了して頭部をなるべく一定に保つ。APK を `./gradlew.bat :app:assembleDebug --no-daemon --console=plain` でビルドする。
2. `./tools/measure_dual_pass.ps1` で同じ APK の Dual Pass を測る。
3. `./tools/measure_multiview.ps1` で Multiview を測る。実機が非対応で Dual Pass に戻った場合は、スクリプトが選択結果の不一致を報告する。そのログは Multiview の測定結果として扱わない。
4. 両ログの OS/build、リフレッシュレート、色・深度形式、解像度、サーマル状態、描画フレーム数が一致することを確認する。ヘッドセットを装着し、両眼の立方体の色、奥行き、頭を動かした際の空間固定が Dual Pass と同等であることを目視確認する。`multiview_gpu` と `total_gpu` の p10、中央値、p90、および `frame_interval` を比較する。

## 実機結果（2026-10-08 23:05 JST）

同じ APK を Quest 2 に入れ、Dual Pass、Multiview の順に連続測定した。Android 14、build `52242990035800150`、GPU `Adreno (TM) 650`。Vulkan は `multiview=1`、`maxMultiviewViewCount=6` を報告し、Multiview 経路が選択された。OpenXR のリフレッシュレートは両方とも 72.00 Hz。両方式で 100 個の立方体、片眼 1024 × 1024、1 sample、色形式 `VK_FORMAT_R8G8B8A8_SRGB`（43）、深度形式 `VK_FORMAT_D32_SFLOAT`（126）、ウォームアップ 300 フレーム、測定 1800 フレームが一致した。両ログとも描画フレームは 2100、開始前後の Android Thermal Status は 0 だった。

| 指標 | Dual Pass p10 / 中央値 / p90 (ms) | Multiview p10 / 中央値 / p90 (ms) |
| --- | ---: | ---: |
| フレーム間隔 | 13.7475 / 13.9182 / 14.1053 | 13.7740 / 13.9228 / 14.0599 |
| 描画 GPU 時間 | 0.5387 / **0.5407** / 0.5438 | 0.4044 / **0.4059** / 0.4076 |

GPU 時間の中央値は Multiview が **0.1348 ms（約 24.9%）短い**。p90 − p10 は Dual Pass 0.0051 ms、Multiview 0.0032 ms。フレーム間隔の中央値はそれぞれ 13.9182 ms と 13.9228 ms で、72 Hz のフレームペーシング自体に明確な差は見えない。GPU 時間は Dual Pass で左右別 submission の合計、Multiview で 1 submission の計測値。左右の CPU 処理や OpenXR compositor の負荷は含まない。

測定の全ログと端末・サーマル情報は [Dual Pass](dual-pass-20261008-230502.txt) と [Multiview](multiview-20261008-230546.txt) に保存した。測定後に両方式を順に Quest 2 で起動し、ユーザーがヘッドセットを装着して目視確認した。Dual Pass は左右とも正常に表示され、Multiview でも立方体の色・奥行き・頭部移動時の空間固定が同等に見えた。Multiview の再起動ログでも `selected=multiview`、2 層の Swapchain、`shouldRender=1` を確認した。
