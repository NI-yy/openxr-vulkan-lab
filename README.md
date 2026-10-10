# openxr-vulkan-lab

Meta Quest 2 向けの OpenXR / Vulkan ネイティブアプリの実験用リポジトリです。現段階のアプリは Vulkan を使って OpenXR セッションと LOCAL 参照空間を作り、セッション状態に従ってフレームループを実行します。青い背景の前に空間固定の立方体 100 個を Projection Layer で表示・計測します。Vulkan Multiview に対応する端末では 2 層の Swapchain にまとめて描き、非対応なら左右別々の Dual Pass で描きます。

## 環境

- Windows / PowerShell、Git、Java 21
- Android SDK Platform 34、Build Tools 34.0.0、Platform Tools、NDK 23.2.8568313、CMake 3.22.1
- 開発者モードを有効にした Quest 2 と USB データ通信ケーブル
- 初回ビルド時に Gradle、Android Gradle Plugin、OpenXR SDK ソースを取得できるネットワーク接続

この組み合わせは [Issue #1 の実機手順](docs/quest2-hello-xr.md) と同じです。OpenXR SDK は同手順で使用した commit `3ed64d0f9bb680f24b80a085091e5c8fab38f7b7` に固定しています。Gradle 8.5 は `gradle/wrapper/gradle-wrapper.properties`、Android Gradle Plugin 8.1.4 は `build.gradle`、Android の各バージョンと `arm64-v8a` は `app/build.gradle` に固定しています。Quest 2 の実測環境は [ランタイム記録](docs/quest2-runtime-2026-09-29.md) を参照してください。

Manifest の Quest 2 宣言は [Meta の Android Manifest 指針](https://developers.meta.com/horizon/documentation/native/android/mobile-native-manifest/) に従っています。

## ビルドと実機確認

Quest 2 を接続せずに環境確認、ホスト側のシーンテスト、Debug APK ビルドを一度に実行できます。Java 21、Android SDK（Platform 34、Build Tools 34.0.0、NDK 23.2.8568313、CMake 3.22.1）と `g++` または `clang++` を用意し、リポジトリのルートで実行してください。失敗した段階で非ゼロの終了コードになります。CI の `Verify` ワークフローも同じコマンドを実行します。

```powershell
$env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
# Java 21 は PATH に追加するか、JAVA_HOME にインストール先を設定
./tools/verify.ps1
```

Linux でも PowerShell (`pwsh`) から `./tools/verify.ps1` を実行できます。SDK の場所は `ANDROID_HOME`（未設定なら `ANDROID_SDK_ROOT`）で指定します。テスト実行ファイルは `build/host-tests/`、APK は `app/build/outputs/apk/debug/app-debug.apk` に出力されます。初回の OpenXR SDK 取得にはネットワーク接続が必要です。同じ commit のローカルソースがある場合は、後述の `OPENXR_SDK_SOURCE_DIR` を指定できます。

実機確認では、検証コマンドで APK をビルドした後に Quest 2 を接続します。

```powershell
$env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
$adb = Join-Path $env:ANDROID_HOME 'platform-tools\adb.exe'
& $adb devices -l
```

ビルド成果物は `app/build/outputs/apk/debug/app-debug.apk` です。`adb devices -l` に `device` と表示された Quest 2 にインストールして起動します。

```powershell
& $adb -d install -r 'app/build/outputs/apk/debug/app-debug.apk'
& $adb -d logcat -c
& $adb -d shell am start -n dev.niyy.openxrvulkanlab/android.app.NativeActivity
& $adb -d logcat -d -s OpenXRVulkanLab:I '*:S'
```

`OpenXR extension XR_KHR_vulkan_enable2: available`、`OpenXR runtime: Oculus`、`OpenXR session created`、`Vulkan multiview: supported=... requested=... selected=...`、`LOCAL reference space created`、`Cube geometry and graphics pipeline ready`、`Swapchain 0`、`OpenXR session started`、`OpenXR frame 1 completed; shouldRender=1` を確認します。Dual Pass の場合は `Swapchain 1` も表示されます。以降は 120 フレームごとに進行を記録します。Quest 2 を装着し、青い背景の前に色分けされた立方体が左右の目に見えることを確認してください。

立方体は一辺 40 cm で、`LOCAL` 空間の固定した 5 × 5 × 4 格子に配置します。描画時には `predictedDisplayTime` の左右の View 姿勢と FOV から、それぞれ View / Projection 行列を作ります。頭を左右や上下に動かしても格子が頭についてこず、同じ空間位置に見えることを実機で確認します。再センタリングで `LOCAL` の原点が変わる場合は `Reference space change pending` をログに記録します。

Issue #6 の Dual Pass 基準性能を測る条件、測定方法、実機結果は [ベンチマーク記録](docs/dual-pass-benchmark.md) を参照してください。`./tools/measure_dual_pass.ps1` で端末情報と計測ログを保存できます。Issue #7 の Multiview 実装、切り替え、比較手順は [Multiview 比較](docs/multiview-benchmark.md) を参照してください。

Issue #8 の固定 Foveated Rendering は、Multiview 時に `debug.openxrvulkanlab.foveation` を `off`（既定）・`low`・`high` にして比較できます。対応拡張や Vulkan 機能が使えない場合は `off` に戻ります。実機測定の条件と結果は [FFR 比較](docs/foveation-benchmark.md) を参照してください。

## シーン切り替え

Issue #9 では OpenXR のフレーム進行を `main.cpp`、Vulkan デバイスと Swapchain を含む描画器を `renderer.cpp`、シーン定義を `scene.cpp` に分けました。描画器のリソースは `Renderer` が所有し、`main.cpp` は描画器の公開操作を呼びます。`debug.openxrvulkanlab.scene` で `clear`（背景のみ）、`single-cube`（正面に立方体1個）、`grid-100`（従来の基準シーン）を選べます。未設定時は `grid-100` です。

```powershell
& $adb -d shell setprop debug.openxrvulkanlab.scene single-cube
& $adb -d shell setprop debug.openxrvulkanlab.scene clear
& $adb -d shell setprop debug.openxrvulkanlab.scene grid-100
```

アプリの起動中でも次のフレームから反映されます。切り替えるとウォームアップと計測サンプルをリセットし、`Scene selected: ...; benchmark reset` をログに出します。`Benchmark complete` にもシーン名を記録します。既存の計測スクリプトは測定前に `grid-100` を指定するため、基準条件は従来どおりです。未知の名前は警告を出し、現在のシーンを維持します。

Quest 2 を接続しなくても、上記の `./tools/verify.ps1` で APK のビルドとシーン名、立方体数、従来の格子配置を検証できます。実機での両眼表示と GPU 時間の測定は、Quest を接続した日にまとめて行います。

2026-10-03 に Quest 2 で取得した[左右の目の画面キャプチャ](docs/quest2-clear-color.png)は、Issue #4 のクリア色実装時の記録です。

Issue #5 の立方体実装でも、2026-10-03 に Quest 2 で[左右の目の画面キャプチャ](docs/quest2-cube.png)を取得し、両目に赤い前面が描かれることとフレーム進行を確認しました。頭部移動時の空間固定と奥行きの見え方は、ヘッドセットを装着しての確認が必要です。

セッションの中断・再開は、アプリ起動中にヘッドセットをスリープ・復帰させて確認できます。ログの `OpenXR session state: 6`（STOPPING）と `OpenXR session stopped` の後、`OpenXR session state: 2`（READY）と `OpenXR session started`、フレーム番号の進行を確認してください。端末側がアクティビティを破棄した場合は再起動になり、`native app started` と Swapchain 作成ログが再度出ます。アクティビティが保持された場合は同じプロセス・同じセッションが再開します。どちらの場合も青いクリア色が再表示されることを確認してください。

終了を確認するには、Quest 2 でアプリを閉じるか、次のコマンドで戻るキーを送って再度 `logcat` を取得します。`back pressed; finishing activity`、`native app exited` が出ます。Android または Runtime の終了要求でも同様にリソースを破棄します。

```powershell
& $adb -d shell input keyevent 4
& $adb -d logcat -d -s OpenXRVulkanLab:I '*:S'
```

通常の終了操作が難しい場合は `& $adb -d shell am force-stop dev.niyy.openxrvulkanlab` で停止できます。ただし `force-stop` はプロセスを強制終了するため、`native app exited` が残るとは限りません。

初回の OpenXR SDK 取得を省略して手元の同じ commit を使う場合は、ビルド前に `OPENXR_SDK_SOURCE_DIR` をそのチェックアウトの絶対パスに設定できます。通常のビルドでは不要です。

Shader のソースは `app/src/main/cpp/cube.vert`、`cube_multiview.vert`、`cube.frag` です。いずれかを変更したら、Vulkan SDK の `glslc` を PATH に置き、リポジトリのルートで次を実行して `cube_shaders.h` を再生成し、GLSL と一緒にコミットしてください。

```powershell
./tools/generate_cube_shaders.ps1
./gradlew.bat :app:assembleDebug --no-daemon --console=plain
```

生成スクリプトは各 GLSL の SHA-256 をヘッダーに記録します。Gradle の `checkCubeShaders` タスクは APK ビルド前に 3 ファイルのハッシュを照合し、不一致なら再生成を促してビルドを失敗させます。`./tools/verify.ps1` と CI の `Verify` ワークフローでも APK ビルドを通じて同じ検査を実行します。検査と通常の APK ビルドに `glslc` は不要です。
