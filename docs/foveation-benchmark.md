# Quest 2 固定 Foveated Rendering 比較（Issue #8）

## 実装

[Meta の公式サンプル `XrCompositor_NativeActivity`](https://github.com/meta-quest/Meta-OpenXR-SDK/blob/main/Samples/XrSamples/XrCompositor_NativeActivity/Src/XrCompositor_NativeActivity.c)の foveation profile 作成・Swapchain 更新の流れと、[Meta の Native OpenXR/Vulkan 手順](https://developers.meta.com/horizon/documentation/native/android/os-fixed-foveated-rendering/)を参照した。公式サンプルの描画 API は OpenGL ES なので、Vulkan の Fragment Density Map 接続は後者の手順に従う。起動時に OpenXR の `XR_FB_swapchain_update_state`、`XR_FB_foveation`、`XR_FB_foveation_configuration`、`XR_FB_foveation_vulkan`、`XR_META_vulkan_swapchain_create_info` と、Vulkan の `VK_EXT_fragment_density_map`、`VK_EXT_fragment_density_map2` を列挙する。Vulkan の `fragmentDensityMap` 機能も問い合わせる。Quest 2 の今回のログでは全拡張が利用可能で、`fragmentDensityMap=1`、`fragmentDensityMapNonSubsampledImages=1`、`fragmentDensityMapDeferred=1` だった。

FFR は Multiview 描画経路でのみ有効にする。`debug.openxrvulkanlab.foveation` が `low` または `high` で、必要な機能を使える場合は、2 層の色 Swapchain を Fragment Density Map と subsampled layout 付きで作る。取得した各 Swapchain 画像の 2 層の密度マップを render pass の 3 番目の attachment に接続し、対応する固定レベルの OpenXR foveation profile を Swapchain に適用する。`off`（既定）では従来の Multiview 経路を使う。必要な機能がない端末、または Dual Pass 描画経路では要求が `low` / `high` でも `off` を選ぶ。選択結果は `Foveation: requested=... selected=...` と `Benchmark config` に記録する。

## 比較手順

1. [Issue #7 の手順](multiview-benchmark.md)と同じ Java 21 / Android SDK 設定で `./gradlew.bat :app:assembleDebug --no-daemon --console=plain` を実行する。Quest 2 を USB 接続し、`adb devices -l` で `device` を確認する。
2. ヘッドセットを冷まし、Guardian を完了して装着する。頭部の位置と視線方向をなるべく一定にする。システム全体の `debug.oculus.foveation.level` / `dynamic` 上書きが設定されていないことを確認する。
3. 同じ APK で `./tools/measure_foveation.ps1 -Foveation off`、`-Foveation low`、`-Foveation high` を順に実行する。各スクリプトは `docs/foveation-<level>-YYYYMMDD-HHMMSS.txt` に端末情報、選択結果、サーマル状態、GPU 時間、フレーム間隔を保存する。指定した FFR レベルが選ばれなければ結果を不採用にする。
4. 各条件で Quest 2 を装着し、左右の表示、画面周辺の輪郭、奥行き、頭部移動時の空間固定を目視確認する。測定スクリプトは終了時にアプリを停止するので、目視時は `adb shell setprop debug.openxrvulkanlab.mode multiview` と `adb shell setprop debug.openxrvulkanlab.foveation <level>` を設定してアプリを起動し直す。立方体が視野に入らない場合は Quest の視点を再センタリングして正面を見る。

## 実機結果（2026-10-10 19:10–19:12 JST）

同じ APK を Quest 2（Android 14、build `52242990035800150`、Adreno 650）に入れ、無効、低、高の順で測定した。3 条件とも 72 Hz、100 個の立方体、片眼 1024 × 1024、2 層 Multiview、1 sample、色形式 43、深度形式 126、ウォームアップ 300 フレーム、測定 1800 フレームが一致した。各ログの `complete=True`、FFR 選択一致、描画フレーム 2100、測定前後の Thermal Status 0 を確認した。システム全体の FFR 上書きプロパティは空だった。FFR 有効時の密度マップは各 Swapchain 画像につき 32 × 32 だった。

| FFR | フレーム間隔 p10 / 中央値 / p90 (ms) | Multiview GPU p10 / 中央値 / p90 (ms) |
| --- | ---: | ---: |
| 無効 | 13.7909 / 13.9226 / 14.0575 | 0.4091 / **0.4102** / 0.4120 |
| 低 | 13.7659 / 13.9246 / 14.0782 | 0.4874 / **0.4886** / 0.4904 |
| 高 | 13.7742 / 13.9244 / 14.0763 | 0.4461 / **0.4479** / 0.4501 |

低は無効より中央値で 0.0784 ms（約 19.1%）、高は 0.0377 ms（約 9.2%）長い。今回の立方体シーンと単純な Fragment Shader では FFR による GPU 時間の改善は見られなかった。Meta のガイドも、単純な Shader では FFR の固定費が節約量を上回り得るとしている。計測はアプリの Vulkan submission の GPU 時間で、OpenXR compositor や CPU 待機時間を含まない。フレーム間隔の中央値は 3 条件とも約 13.92 ms だった。各条件 1 回の連続測定であり、頭部姿勢は記録していないため、速度差の厳密な推定には姿勢を固定した反復測定が必要である。

全ログは [無効](foveation-off-20261010-191049.txt)、[低](foveation-low-20261010-191129.txt)、[高](foveation-high-20261010-191210.txt) に保存した。Dual Pass を強制して FFR 高を要求した追加確認では、`selected=off` と `shouldRender=1` を確認した。測定後に各条件でアプリを起動し、ユーザーが Quest 2 を装着して確認した。無効・低・高のいずれも両眼の立方体と頭部移動時の空間固定は正常で、低・高とも無効との画質差は分からなかった。これは今回のシーンでの主観的な確認であり、周辺画素の品質が完全に同等であることは意味しない。
