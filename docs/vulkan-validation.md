# Vulkan Validation Layer 確認記録（Issue #13）

## PCで確認済み

- Debug APK の C++ コンパイルと APK 生成
- `measure_suite_test.ps1` の模擬ADB検証: 各測定で `off` を設定・記録し、アプリログが `on` の場合、グローバルレイヤーまたは対象アプリのGPUデバッグレイヤーが有効な場合は失敗すること
- `stage_validation_layer.ps1` は arm64 ELF のみ受け付ける構成

## Quest 2で確認済み（2026-10-12）

Khronos の Android `arm64-v8a` レイヤーを Debug APK に同梱し、`validation=on` で `requested=on selected=on` と Debug Utils messenger の有効化を確認した。初回に発見したインスタンス拡張の依存関係エラーを修正後、取得したログに Validation Error / Warning はなかった。

`validation=off` は `requested=off selected=off layer=none` と描画を確認した。レイヤー非同梱 APK では `validation=on` の要求に対して `not found` の理由と `selected=off` が記録され、通常起動した。一括計測の4条件すべてで `validation_requested=off` と `validation_off_confirmed=True` を確認した。詳細とログは [Issue #10〜#13 の実機記録](quest2-issues-10-13-2026-10-12.md) を参照。
