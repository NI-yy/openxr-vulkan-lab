# 夕日の海: 画質改善の記録

Issue #17 の改善前後を比較するための記録。PC で調整した後、Quest 2 で画質と性能を確認した。

## 改善前の基準

- 実機: Quest 2、1024 × 1024 / 眼、Multiview、FFR off、72 Hz。
- 2026-10-12 の連続測定: 300 フレームのウォームアップ後、1800 フレームで GPU 時間中央値 1.5117 ms、フレーム間隔中央値 13.9241 ms。
- [両眼キャプチャ](sunset-ocean-before.png)。水平線付近に規則的な横縞があり、太陽の反射が離れた明るい斑点に見える。
- 視点の数値は当時保存していない。後の実機画像とは構図をおおむね合わせて比較する。

## PC 上での変更

- 波の進行方向と位相を変え、直線的な繰り返しを減らした。
- 画素に対して細かすぎる波の法線を弱め、遠景のエイリアシングを抑えた。
- 水面の反射率を視線角に応じて調整し、太陽の鏡像を粗い水面のハイライトに置き換えた。
- 空の色に太陽との角度、視線の高度、地平線付近の霞を反映した。レイリー散乱とミー散乱の位相特性を軽量に近似し、輝度を使ったトーンマッピングで夕日の色を保つ。厳密な大気散乱積分や LUT は使っていない。
- シェーダーのコンパイル、SPIR-V 検証、`./tools/verify.ps1` によるホストテストと Debug APK ビルドは PC 上で成功した。[概算プレビュー](sunset-ocean-host-preview.png) は Quest 2 の実際の描画結果ではない。

## PC プレビューの再生成

`tools/preview_sunset_ocean.py` はカメラを LOCAL 原点、正面 -Z、水平・垂直 90 度の視野角に固定し、毎回同じ構図で `docs/sunset-ocean-host-preview.png` を生成する。Python の依存関係は `tools/preview-requirements.txt` に固定した。

```powershell
python -m pip install -r tools/preview-requirements.txt
python tools/preview_sunset_ocean.py
```

これは GLSL の計算を CPU で概算したプレビューであり、Vulkan シェーダーそのものや両眼の画質・性能を検証するものではない。変更前の Quest 2 キャプチャにはカメラ姿勢が記録されていないため、厳密な同一視点比較には使えない。

## Quest 2 実機確認（2026-10-20）

Debug APK（SHA-256 `E116A82558066DE14288E7C83681ECB91D564F208A2CAC0D80AD020918525AD1`）、Quest 2、1024 × 1024 / 眼、72 Hz。各測定は 300 フレームのウォームアップ後、1800 フレームを記録した。3 条件とも要求したシーン・描画方式・FFR が選択され、Validation off、サーマル状態は前後とも 0 だった。

| 条件 | GPU 時間中央値 | フレーム間隔中央値 | 記録 |
| --- | ---: | ---: | --- |
| Multiview / FFR off | 2.1542 ms | 13.9001 ms | [ログ](measurements/20261020-sunset-ocean/multiview-off-success.txt)、[両眼画像](sunset-ocean-after.png) |
| Dual Pass / FFR off | 左右合計 2.1689 ms | 13.8896 ms | [ログ](measurements/20261020-sunset-ocean/dual-pass-off.txt)、[両眼画像](sunset-ocean-dual.png) |
| Multiview / FFR high | 1.0960 ms | 13.8970 ms | [ログ](measurements/20261020-sunset-ocean/multiview-high.txt)、[両眼画像](sunset-ocean-high.png) |

改善前の Multiview / FFR off の GPU 中央値 1.5117 ms より、今回の 2.1542 ms は約 42% 長い。測定日と端末の OS 状態が異なるため、増加分のすべてをシェーダー変更に帰属できない。いずれも 72 Hz のフレーム予算 13.89 ms の範囲内で完走した。GPU 時間はアプリの timestamp query 値であり、実際の表示遅延ではない。

[改善前](sunset-ocean-before.png)と[改善後](sunset-ocean-after.png)を比べると、遠景の細かな乱れと離れた明るい斑点は減り、太陽の反射が帯状につながった。水面の規則的な横帯はまだ残る。比較画像の視点が厳密には一致せず、静止画ではちらつきや頭部移動時の安定性を判定できない。

APK 同梱の `VK_LAYER_KHRONOS_validation` を有効にした起動では、レイヤーと Debug Utils messenger の選択、描画フレームの進行を確認した。取得した約 9 秒間の[ログ](measurements/20261020-sunset-ocean/validation-on.txt)に Validation Error / Warning はなかった。

初回の自動計測は Quest 2 の「コントローラーに切り替え」ダイアログで起動が止まり、タイムアウトした。[初回ログ](measurements/20261020-sunset-ocean/startup-dialog-timeout.txt)は結果として採用していない。ダイアログを進めた後の上記 3 回は正常終了した。ユーザーが装着して頭部を左右・上下に動かし、海と水平線の空間固定、左右の自然な奥行き、ちらつきを確認した結果は「正常に見える」だった。これは主観的な確認で、動画による定量判定ではない。

- [x] 両眼キャプチャと改善前の静止画比較。
- [x] Multiview、Dual Pass、FFR high の設定選択と描画を確認。
- [x] GPU 時間、フレーム間隔、72 Hz、サーマル状態を記録。
- [x] Validation Layer を有効にして描画ログを確認。
- [x] 装着して頭部を動かし、空間固定、左右の視差、ちらつきを確認。
