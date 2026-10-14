# 夕日の海: 画質改善の記録

Issue #17 の改善前後を比較するための記録。Quest 2 を最後に一度だけ接続して確認する。

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

## 最後に行う実機確認

- [ ] 同じシーンの両眼キャプチャを保存し、横縞、ちらつき、反射帯を改善前と比較する。
- [ ] 頭部を動かし、水面と水平線の空間固定、左右の視差を確認する。
- [ ] Multiview、Dual Pass、FFR high で表示を確認する。
- [ ] Multiview / FFR off で GPU 時間とフレーム間隔を再測定し、72 Hz を維持できるか確認する。
- [ ] Vulkan Validation Layer が使える場合は、描画由来のエラーを確認する。
