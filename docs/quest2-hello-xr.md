# Quest 2 で Khronos `hello_xr` を動かす（Windows / PowerShell）

2026-09-29 に Quest 2 実機で Vulkan 版をビルド・起動し、青い立方体が表示されることを確認した手順です。Khronos のサンプルを別ディレクトリに取得してビルドします。

## 準備

- Quest 2 の開発者モードを有効にし、USB データ通信可能なケーブルで PC に接続する。ヘッドセット内に USB デバッグの許可が出たら許可する。
- Android Studio の SDK Manager で **SDK Platform 34**、**Build Tools 34.0.0**、**Platform Tools**、**NDK 23.2.8568313**、**CMake 3.22.1** を用意する。確認時の Android Studio は Quail 4 / 2026.1.4 Patch 1、Platform Tools は 37.0.1。
- **Java 21** を使う。確認時は Rider 2025.1.3 付属の JBR 21.0.7 を使用した。この PC の Android Studio 付属 JBR 25 は、サンプルの Gradle 8.5 の[実行対応範囲](https://docs.gradle.org/current/userguide/compatibility.html#java_runtime)外。
- `git` と、Gradle の初回ダウンロード・依存解決に使うネットワーク接続が必要。

まず PowerShell で次を設定し、Quest 2 が `device` と表示されることを確認します。Java の場所や SDK の場所が異なる PC ではパスを読み替えてください。

```powershell
$env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
$env:ANDROID_NDK_HOME = Join-Path $env:ANDROID_HOME 'ndk\23.2.8568313'
$env:JAVA_HOME = 'C:\Program Files\JetBrains\JetBrains Rider 2025.1.3\jbr'
$adb = Join-Path $env:ANDROID_HOME 'platform-tools\adb.exe'
& (Join-Path $env:JAVA_HOME 'bin\java.exe') -version
& $adb devices -l
```

## ビルド・インストール・起動

Khronos `OpenXR-SDK-Source` の、実機確認に使った commit `3ed64d0f9bb680f24b80a085091e5c8fab38f7b7` を使います。

```powershell
$source = Join-Path $env:TEMP 'OpenXR-SDK-Source'
if (-not (Test-Path $source)) {
    git clone https://github.com/KhronosGroup/OpenXR-SDK-Source.git $source
}
git -C $source checkout --detach 3ed64d0f9bb680f24b80a085091e5c8fab38f7b7

Push-Location (Join-Path $source 'src\tests\hello_xr')
.\gradlew.bat assembleVulkanDebug --no-daemon --console=plain
Pop-Location

$apk = Join-Path $source 'src\tests\hello_xr\build\outputs\apk\Vulkan\debug\hello_xr-Vulkan-debug.apk'
& $adb -d install -r $apk
& $adb -d shell am start -n org.khronos.openxr.hello_xr.vulkan/android.app.NativeActivity
```

Gradle が `BUILD SUCCESSFUL`、ADB のインストールが `Success` と表示されればビルドとインストールは完了です。Quest 2 を装着し、**Hello XR (Vulkan)** の青い立方体が見えることを確認します。アプリは「提供元不明」の一覧に入る場合があります。

## つまずいたとき

| 症状 | 確認すること |
| --- | --- |
| `adb devices -l` に `device` が出ない、`unauthorized` と出る | Quest 2 内の USB デバッグ許可を確認する。データ通信対応ケーブルと別の USB ポートを試し、接続し直す。必要なら `& $adb kill-server`、`& $adb start-server` を実行する。 |
| USB 接続の通知が何度も出る | ケーブル・コネクタ・ポートの接触を確認する。ADB が安定して `device` と表示される状態で先に進む。 |
| Gradle が Java の互換性エラーを出す | `JAVA_HOME` と `java -version` を確認する。この手順は Java 21 と Gradle 8.5 で確認した。 |
| SDK / NDK / CMake が見つからない | `ANDROID_HOME` が SDK の実際の場所を指すか、上記の各バージョンが SDK Manager に入っているか確認する。 |
| `SDK XML version 4` の警告が出る | 今回の組み合わせでも出た警告。`BUILD SUCCESSFUL` ならビルドは通っている。失敗時は警告の後に続く実際のエラーを確認する。 |
| アプリが一時停止し、立方体が見えない | Quest 2 内の Guardian ダイアログを完了してから **Hello XR (Vulkan)** に戻る。今回も Guardian が前面に出て一時停止したが、その後に立方体を確認できた。 |

今回の Quest 2 の OS、OpenXR Runtime、Vulkan ドライバーと拡張一覧は [実機環境の記録](quest2-runtime-2026-09-29.md) を参照してください。
