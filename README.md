# openxr-vulkan-lab

Meta Quest 2 向けの OpenXR / Vulkan ネイティブアプリの実験用リポジトリです。現段階のアプリは描画せず、起動時に OpenXR Runtime と Vulkan Loader を調べ、Android の終了イベントまで待ってログを残します。

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

`native app started`、`Vulkan loader: result=0`、`OpenXR runtime: Oculus`、`xrGetSystem: 0` を確認します。終了を確認するには、Quest 2 でアプリを閉じるか、次のコマンドで戻るキーを送って再度 `logcat` を取得します。`back pressed; finishing activity`、`activity destroyed`、`native app exited` が出ます。画面描画はまだ実装していません。

```powershell
& $adb -d shell input keyevent 4
& $adb -d logcat -d -s OpenXRVulkanLab:I '*:S'
```

通常の終了操作が難しい場合は `& $adb -d shell am force-stop dev.niyy.openxrvulkanlab` で停止できます。ただし `force-stop` はプロセスを強制終了するため、`native app exited` が残るとは限りません。

初回の OpenXR SDK 取得を省略して手元の同じ commit を使う場合は、ビルド前に `OPENXR_SDK_SOURCE_DIR` をそのチェックアウトの絶対パスに設定できます。通常のビルドでは不要です。
