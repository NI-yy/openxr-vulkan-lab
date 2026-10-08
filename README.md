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

クリーンなチェックアウトのルートで、Java と SDK の場所を設定します。`JAVA_HOME` は各自の Java 21 に合わせて変更してください。

```powershell
$env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
$env:JAVA_HOME = 'C:\Program Files\JetBrains\JetBrains Rider 2025.1.3\jbr'
$adb = Join-Path $env:ANDROID_HOME 'platform-tools\adb.exe'
& $adb devices -l
.\gradlew.bat :app:assembleDebug --no-daemon --console=plain
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

Shader は `app/src/main/cpp/cube.vert` と `cube.frag` にあります。変更後は Vulkan SDK の `glslc` を PATH に置き、`./tools/generate_cube_shaders.ps1` を実行して、ビルドに埋め込む `cube_shaders.h` を再生成してください。通常の APK ビルドに Vulkan SDK は不要です。
