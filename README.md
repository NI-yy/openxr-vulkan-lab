# openxr-vulkan-lab

Meta Quest 2 向けの OpenXR / Vulkan ネイティブアプリの実験用リポジトリです。現段階のアプリは Vulkan を使って OpenXR セッションと LOCAL 参照空間を作り、セッション状態に従ってフレームループを実行します。左右の目それぞれに OpenXR Swapchain を作り、青いクリア色を Projection Layer で表示します。

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

`OpenXR extension XR_KHR_vulkan_enable2: available`、`OpenXR runtime: Oculus`、`OpenXR session created`、`LOCAL reference space created`、`Eye 0 swapchain`、`Eye 1 swapchain`、`OpenXR session started`、`OpenXR frame 1 completed; shouldRender=1` を確認します。以降は 120 フレームごとに進行を記録します。Quest 2 を装着し、左右の目に青いクリア色が表示されることも確認してください。

2026-10-03 に Quest 2 で取得した[左右の目の画面キャプチャ](docs/quest2-clear-color.png)もあります。

セッションの中断・再開は、アプリ起動中にヘッドセットをスリープ・復帰させて確認できます。ログの `OpenXR session state: 6`（STOPPING）と `OpenXR session stopped` の後、`OpenXR session state: 2`（READY）と `OpenXR session started`、フレーム番号の進行を確認してください。端末側がアクティビティを破棄した場合は再起動になり、`native app started` と Swapchain 作成ログが再度出ます。アクティビティが保持された場合は同じプロセス・同じセッションが再開します。どちらの場合も青いクリア色が再表示されることを確認してください。

終了を確認するには、Quest 2 でアプリを閉じるか、次のコマンドで戻るキーを送って再度 `logcat` を取得します。`back pressed; finishing activity`、`native app exited` が出ます。Android または Runtime の終了要求でも同様にリソースを破棄します。

```powershell
& $adb -d shell input keyevent 4
& $adb -d logcat -d -s OpenXRVulkanLab:I '*:S'
```

通常の終了操作が難しい場合は `& $adb -d shell am force-stop dev.niyy.openxrvulkanlab` で停止できます。ただし `force-stop` はプロセスを強制終了するため、`native app exited` が残るとは限りません。

初回の OpenXR SDK 取得を省略して手元の同じ commit を使う場合は、ビルド前に `OPENXR_SDK_SOURCE_DIR` をそのチェックアウトの絶対パスに設定できます。通常のビルドでは不要です。
