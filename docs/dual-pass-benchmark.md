# Quest 2 Dual Pass 基準性能

Issue #6 の Multiview 比較用ベースライン。左右を別々の Vulkan render pass、command buffer submission、fence wait で描く。

## 固定した描画条件

| 項目 | 値 |
| --- | --- |
| シーン | 40 cm の立方体を 5 × 5 × 4 の格子に配置（100 個）。`LOCAL` 空間に固定 |
| 立方体の中心 | X/Y: -1.10、-0.55、0、0.55、1.10 m。Z: -2.00、-2.65、-3.30、-3.95 m |
| 片目の解像度 | 1024 × 1024、色 Swapchain は左右別々 |
| サンプル数 | 1（MSAA なし） |
| 描画 | 各眼で clear、depth test/write、有効なシーンを 100 draw call で描く。両面描画、ブレンドなし |
| 色・深度形式 | 実機が対応する形式を起動ログの `Benchmark config` に記録 |
| Shader | `app/src/main/cpp/cube.vert`、`cube.frag` |
| 計測区間 | 300 描画フレームをウォームアップし、次の 1800 描画フレームを測る |

GPU 時間は `vkCmdWriteTimestamp` の TOP_OF_PIPE と BOTTOM_OF_PIPE の差を、GPU の `timestampPeriod` でミリ秒に変換する。左右それぞれを測り、同じフレームの左右の和を `total_gpu` とする。左右の間の CPU 処理、`xrWaitFrame`、OpenXR compositor はこの GPU 時間に含まれない。

`frame_interval` はアプリが `RunFrame` に入る時刻の差で、OpenXR の待機や CPU/GPU 同期を含むペーシングの目安である。描画されないフレームがあれば測定をリセットする。各指標を昇順に並べ、線形補間した p10、中央値、p90 を logcat に出す。p90 − p10 をばらつきの幅として比較する。

## 再測定手順

1. Quest 2 を十分に冷まし、充電・省電力設定と室温を揃える。開発者モードと USB デバッグを許可し、`adb devices -l` が `device` を示すことを確認する。Quest 2 を装着して Guardian を完了し、アプリが描画できる状態にする。測定中は頭部の位置と視線方向をなるべく一定にする。
2. README の Java 21 と Android SDK 設定で `./gradlew.bat :app:assembleDebug --no-daemon --console=plain` を実行する。
3. リポジトリのルートで `./tools/measure_dual_pass.ps1` を実行する。スクリプトは APK をインストールし、logcat をクリアしてアプリを起動する。`Benchmark complete` まで待ち、OS/build、開始・終了時刻、起動ログ、測定値、測定前後の `dumpsys thermalservice` を `docs/dual-pass-YYYYMMDD-HHMMSS.txt` に保存する。
4. 出力の `OpenXR display refresh rate`、`Benchmark config`、`Benchmark complete`、`Benchmark ... ms` を確認する。`complete=False` や描画フレーム不足の場合は結果として採用しない。サーマル状態が測定中に変化した場合もその旨を記録して再測定する。

OpenXR の `XR_FB_display_refresh_rate` から実際のリフレッシュレートを取得する。測定前後の thermalservice ダンプには thermal status の変化があるか確認する。比較対象の Multiview も、**同じシーン、描画設定、解像度、リフレッシュレート、ウォームアップ、計測フレーム数**で測る。

## 測定結果

2026-10-04 12:59 JST に Quest 2 実機で測定した。Android 14、build `52242990035800150`、OpenXR Runtime `Oculus`、GPU `Adreno (TM) 650`。OpenXR が報告したリフレッシュレートは 72.00 Hz。左右とも 1024 × 1024、1 sample、色形式 `VK_FORMAT_R8G8B8A8_SRGB`（43）、深度形式 `VK_FORMAT_D32_SFLOAT`（126）。300 フレーム（約 4.2 秒）をウォームアップし、1800 フレーム（約 25 秒）を採用した。測定前後の Android Thermal Status はどちらも 0。描画フレームは 2100、全てログ上 `shouldRender=1` で進行した。

| 指標 | p10 | 中央値 | p90 | p90 − p10 |
| --- | ---: | ---: | ---: | ---: |
| フレーム間隔 (ms) | 13.7710 | 13.9179 | 14.0787 | 0.3077 |
| 左眼 GPU (ms) | 0.2807 | 0.2817 | 0.2827 | 0.0020 |
| 右眼 GPU (ms) | 0.2603 | 0.2621 | 0.2653 | 0.0050 |
| 左右合計 GPU (ms) | 0.5417 | 0.5438 | 0.5469 | 0.0052 |

端末情報、サーマル状態、起動から終了までのログは [最終コードの測定ログ](dual-pass-20261004-125904.txt) に保存した。直前の同条件測定では左右合計 GPU 時間の中央値が 0.5396 ms だった（[初回ログ](dual-pass-20261004-125353.txt)）。この数値は Dual Pass の基準値であり、Multiview との速度差はまだ測っていない。

2026-10-08 に Quest 2 へ APK を再インストールして起動し、装着したユーザーから表示に問題がないとの確認を得た。ADB ログでも `shouldRender=1` のフレーム進行を確認した。ADB の通常の画面キャプチャは黒一色となったため、画像による記録はない。
