# Evan · Qt 5.12 / MinGW 构建说明

本工程已从 **Qt 6 / MSVC** 迁移为 **Qt 5.12 + CMake + MinGW-w64 (GCC)**，功能与原始版本保持一致。

## 1. 工具链

| 项目 | 路径 |
| --- | --- |
| 编译器 | `C:\Mingws\winlibs-x86_64-posix-seh-gcc-12.1.0-llvm-14.0.6-mingw-w64msvcrt-10.0.0-r3\bin` |
| Qt | `C:\Qts\5.12\5.12.10\mingw73_64` |
| 构建系统 | CMake ≥ 3.16 + Ninja |
| C++ 标准 | C++17 |

> 说明：Qt 5.12 LTS 官方支持到 C++17。工程不再使用 C++20 特性，以避免与 Qt 5.12 头文件的兼容问题。

## 2. 一键构建

```bat
build_qt5.bat
```

脚本依次完成：CMake 配置 → 编译 → `windeployqt` 部署 → 拷贝 OpenSSL 运行库，
产物位于 `build-qt5\evan.exe`。

## 3. 手工构建

```bat
set PATH=C:\Mingws\winlibs-x86_64-posix-seh-gcc-12.1.0-llvm-14.0.6-mingw-w64msvcrt-10.0.0-r3\bin;C:\Qts\5.12\5.12.10\mingw73_64\bin;%PATH%

cmake -S . -B build-qt5 -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_CXX_COMPILER="C:/Mingws/winlibs-x86_64-posix-seh-gcc-12.1.0-llvm-14.0.6-mingw-w64msvcrt-10.0.0-r3/bin/g++.exe" ^
      -DCMAKE_RC_COMPILER="C:/Mingws/winlibs-x86_64-posix-seh-gcc-12.1.0-llvm-14.0.6-mingw-w64msvcrt-10.0.0-r3/bin/windres.exe" ^
      -DQt5_DIR="C:/Qts/5.12/5.12.10/mingw73_64/lib/cmake/Qt5"

cmake --build build-qt5 --parallel

scripts\deploy_qt5.bat build-qt5\evan.exe
```

若 Qt 装在其它位置，只需覆盖 `-DQt5_DIR=.../lib/cmake/Qt5` 即可，CMakeLists 中已内置上述默认路径。

## 4. 运行时依赖

- `windeployqt` 会部署 `Qt5Core/Gui/Widgets/Network`、`platforms\qwindows.dll`、图片格式插件等。
- **OpenSSL 1.1 x64**（`libssl-1_1-x64.dll`、`libcrypto-1_1-x64.dll`）必须与 `evan.exe` 同目录，
  否则「检查更新 / 插件市场下载 / 在线公式识别」等 HTTPS 功能会报 `TLS initialization failed`。
  Qt 5.12 不支持 OpenSSL 3.x，请使用 1.1.1 系列（工程已内置在 `third_party\openssl`）。

## 5. Qt 6 → Qt 5.12 迁移要点

所有差异集中处理，业务逻辑未改动：

| 位置 | 迁移内容 |
| --- | --- |
| `src/qt5_compat.h` | 新增兼容层：`QKeyCombination` → 打包整数键码；`QThreadPool::start(lambda)` → `QRunnable` 封装；`QMetaObject::invokeMethod` 队列化调用 |
| `src/core/ocr/windows_media_ocr.cpp` | C++/WinRT（仅 MSVC 可用）改写为 **WinRT ABI 直调**（`RoGetActivationFactory` + 显式虚表派发），MinGW 下可正常调用系统 `Windows.Media.Ocr` |
| `main.cpp` | `Qt::AA_EnableHighDpiScaling` / `AA_UseHighDpiPixmaps`；`setHighDpiScaleFactorRoundingPolicy` 按 Qt 版本条件编译 |
| 事件接口 | `QEnterEvent` → `QEvent`；`event->globalPosition()/position()` → `globalPos()/pos()` |
| `QAbstractNativeEventFilter` | `qintptr*` → `long*` |
| 网络 | `QNetworkRequest::setTransferTimeout` → 定时器 + `reply->abort()`；移除 HTTP/2 属性开关 |
| `QProcess` | `setProgram/setArguments/start()` → `start(program, args)` |
| JSON | `QJsonValue::toInteger()` → `toVariant().toLongLong()` |
| 控件 API | `QLabel::pixmap()` 返回指针；`QComboBox::currentIndexChanged` 显式 `QOverload<int>`；`QTextBrowser::setMarkdown` → 内置 `SimpleMarkdown` 渲染器 |
| 构建 | MSVC 专用 `windowsapp` 库移除；新增 `ole32/oleaut32/runtimeobject/dwmapi/shcore`；`.rc` 以 `--codepage=65001` 编译，保证中文版本信息不乱码 |

## 6. 目录约定

```
build_qt5.bat            # 一键构建脚本
scripts/deploy_qt5.bat   # 运行时部署脚本
third_party/openssl/     # Qt 5.12 HTTPS 所需的 OpenSSL 1.1 x64 运行库
build-qt5/               # 构建输出与已部署的可执行文件
```
