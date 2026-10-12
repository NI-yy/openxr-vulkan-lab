# 連続測定（Issue #12）

`tools/measure_suite.ps1` は既存の `measure_dual_pass.ps1` を各条件で呼び出し、最初の条件だけで APK をインストールします。既定の順序は Dual Pass / FFR off、Multiview / FFR off、Multiview / FFR low、Multiview / FFR high です。各条件でアプリを再起動し、ログを消去してから、従来と同じ 300 フレームのウォームアップと 1800 フレームの計測完了を待ちます。選択された描画方式・FFR レベルが要求と異なる場合やタイムアウトした場合は失敗し、後続条件は実行しません。

## Quest 2 での実行

`./tools/verify.ps1` で APK をビルドし、Quest 2 を装着可能な状態で USB 接続します。`adb devices -l` で `device` を確認し、端末を冷ましてから実行してください。計測中は頭部の位置と向きをなるべく一定にします。

```powershell
./tools/measure_suite.ps1
```

順序と対象を変える場合は、`mode:foveation` の組を指定します。反復測定したい条件は複数回指定できます。Dual Pass では FFR は `off` のみ指定できます。

```powershell
./tools/measure_suite.ps1 -Conditions 'multiview:off','multiview:low','multiview:high' -OutputDirectory 'docs/measurements/ffr-repeat-01'
```

複数のADB端末が接続されている場合は `-Serial <adb-serial>` を指定します。`-TimeoutSeconds` の既定値は各条件180秒です。指定した出力ディレクトリが既に存在すると停止し、過去の測定結果は上書きしません。

出力先は既定で `docs/measurements/YYYYMMDD-HHMMSS/` です。`manifest.json` には実行時刻、Git commit と作業ツリー状態、APK の SHA-256、タイムアウト、条件順序、端末のシリアル・型番・Android バージョン・build fingerprint・security patch、システム全体のFFR上書き値、各条件の成否とログ名を記録します。`01-dual-pass-off.txt` などの条件別ログには、従来どおり端末情報、設定一致の判定、サーマル状態、アプリの計測ログを保存します。失敗した条件は `manifest.json` に理由を残します。

FFR の low/high が端末に実装されていない場合は、アプリが `off` を選ぶため、その条件は失敗となります。ログを確認し、比較可能な成功条件だけを使用してください。測定完了と画質の目視確認は別です。各条件の両眼表示、画面周辺の品質、頭部移動時の空間固定は Quest 2 の装着が必要です。

## PC での検証範囲

`./tools/verify.ps1` は `tests/measure_suite_test.ps1` を実行します。Java 21 やAndroid SDKがないPCでは、`./tests/measure_suite_test.ps1` を単独で実行できます。模擬ADBを使って4条件の順序、APKのインストール回数が1回であること、条件別ログとmanifestの保存、要求と選択結果の不一致による失敗を確認します。実際のOpenXRセッション、GPU時間、サーマル状態、FFRの切り替え、両眼表示と空間固定の確認結果は [Quest 2 実機記録](quest2-issues-10-13-2026-10-12.md) を参照してください。
