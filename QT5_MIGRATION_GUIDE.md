# Evan · Qt 6 → Qt 5.12 迁移与编译部署说明

> 适用版本：Evan 1.2.1
> 迁移目标：把原 Qt 6.8 + MSVC + C++20 的工程，改写为 **Qt 5.12.10 + MinGW-w64 GCC 12.1 + CMake + C++17**，功能与原始版本保持一致。
> 快速上手请见同目录 `BUILD_QT5.md`；本文档是完整的迁移记录与部署手册。

---

## 1. 迁移概览

| 维度 | 迁移前 | 迁移后 |
| --- | --- | --- |
| Qt | Qt 6.8.3 (msvc2022_64) | **Qt 5.12.10 (mingw73_64)** |
| 编译器 | MSVC 2022 | **MinGW-w64 GCC 12.1.0** |
| C++ 标准 | C++20 | **C++17** |
| 构建 | CMake + Ninja | CMake + Ninja（不变） |
| 系统 OCR | C++/WinRT (`winrt::Windows::Media::Ocr`) | **WinRT ABI 直调**（`RoGetActivationFactory` + 显式虚表派发） |
| HTTPS | 系统 Schannel 后端 | **OpenSSL 1.1 x64 运行库**（Qt5 无 Schannel 后端） |
| 可执行产物 | evan.exe (WIN32 子系统) | 不变，Release 约 2.7 MB |
| 绿色目录体积 | — | **约 37 MB**（Qt 运行库 + 插件 + OpenSSL） |

业务逻辑、界面布局、交互行为均未改动；改动集中在 API 适配层与一个 OCR 实现文件。

---

## 2. 环境与工具链

| 项目 | 路径 / 版本 |
| --- | --- |
| 编译器 | `C:\Mingws\winlibs-x86_64-posix-seh-gcc-12.1.0-llvm-14.0.6-mingw-w64msvcrt-10.0.0-r3\bin` |
| Qt | `C:\Qts\5.12\5.12.10\mingw73_64` |
| CMake | ≥ 3.16（本机 4.4.4 验证通过） |
| Ninja | 随 MinGW 提供（`bin\ninja.exe`） |
| OpenSSL | 1.1.1w x64，见 `third_party\openssl` |

**为什么 GCC 12 能配 mingw73_64 的 Qt**：Qt 这套预编译库由 GCC 7.3 构建，而 libstdc++ 的 ABI 向后兼容 —— GCC 12 编译的程序可以链接 GCC 7.3 编出的库。前提是**不要混用其它 MinGW 发行版**（例如 MSYS2 的 UCRT64），否则会出现大量未定义符号。

**为什么退到 C++17**：Qt 5.12 LTS 官方支持到 C++17。C++20 下 Qt 5.12 的部分头文件（`QChar` 隐式转换、`QString` 比较运算符、`qHash` 重载）会与编译器产生兼容问题，收益远小于风险。

---

## 3. 工程结构

### 3.1 源码树（只列与构建相关的部分）

```
evan-ocr-main/
├─ CMakeLists.txt          # 唯一的构建定义（Qt5 / MinGW / C++17）
├─ build_qt5.bat           # ① 一键构建：配置 → 编译 → 部署
├─ BUILD_QT5.md            # 一页速查（命令与依赖）
├─ QT5_MIGRATION_GUIDE.md  # 本文档
├─ scripts/
│  ├─ deploy_qt5.bat       # ② 部署：windeployqt + OpenSSL 拷贝
│  └─ （原有的打包/发布脚本，未改动）
├─ src/
│  ├─ main.cpp             # 入口：托盘、单实例、DPI、全局热键、日志
│  ├─ qt5_compat.h         # ★ 新增：Qt5 兼容层
│  ├─ core/                # 与界面无关：截屏、OCR、公式、翻译、插件、更新、主题
│  ├─ ui/                  # 覆盖层、浮动工具栏、标注、OCR 结果、设置、插件市场
│  │  └─ common/simple_markdown.h   # ★ 新增：Markdown 渲染（替代 setMarkdown）
│  └─ platform/            # 全局热键、原生事件过滤
├─ resources/              # 图标 + app.rc + resources.qrc（编译进 exe）
├─ third_party/openssl/    # ★ 新增：OpenSSL 1.1 x64 运行库（预编译，无需构建）
├─ plugins/                # 可选运行时插件（模型/配置，非编译输入）
└─ build-qt5/              # 构建输出（已在 .gitignore 中排除）
```

### 3.2 新增文件

| 文件 | 作用 |
| --- | --- |
| `src/qt5_compat.h` | Qt5 兼容层：快捷键组合、线程池 lambda 投递、队列化调用 |
| `src/ui/common/simple_markdown.h` | Qt 5.12 无 `setMarkdown`，提供轻量 Markdown→HTML 渲染 |
| `build_qt5.bat` | 一键构建脚本 |
| `scripts/deploy_qt5.bat` | 运行时部署脚本 |
| `third_party/openssl/*.dll` | Qt5 HTTPS 所需 OpenSSL 1.1 x64 运行库 |
| `BUILD_QT5.md` / 本文档 | 构建与迁移说明 |

### 3.3 需要改代码的文件（约 20 个）

| 文件 | 改动要点 |
| --- | --- |
| `src/core/ocr/windows_media_ocr.cpp` | **整体重写**：C++/WinRT → WinRT ABI 直调 |
| `src/main.cpp` | 高 DPI 属性、`QScreen` 头文件 |
| `src/platform/hotkey_manager.{h,cpp}` | `nativeEventFilter` 签名、键码解析 |
| `src/core/hotkey_config.cpp` | `QKeyCombination` → 打包整数键码 |
| `src/ui/pin/pin_window.{h,cpp}` | `QEnterEvent`→`QEvent`，`globalPosition()/position()`→`globalPos()/pos()` |
| `src/core/ocr/rapid_ocr_engine.cpp` | `QProcess` 启动方式、`std::round` 头文件、线程池投递 |
| `src/core/formula/formula_ocr_manager.cpp` | `setTransferTimeout` → 定时器超时 |
| `src/core/plugin/plugin_manager.cpp`、`src/core/translation/*.cpp` | 线程池投递 |
| `src/core/update/update_manager.cpp` | `QJsonValue::toInteger` → `toVariant().toLongLong()` |
| `src/core/translation/offline_plugin_translator.cpp` | 移除 `Http2AllowedAttribute` |
| `src/ui/update/update_dialog.cpp` | `setMarkdown` → 内置渲染器 |
| `src/ui/char_edit/single_char_editor.cpp` | `currentIndexChanged` 重载消歧 |
| `src/ui/formula/formula_result_dialog.cpp` | `QLabel::pixmap()` 返回指针 |
| `src/ui/magnifier/magnifier_widget.cpp` | 补 `<cmath>` |
| `src/core/theme_manager.cpp` | `QApplication::palette()`（Qt5 无 `QGuiApplication::palette()`） |
| `.gitignore` | 新增 `!third_party/openssl/*.dll` 例外，否则 DLL 会被 `*.dll` 规则忽略 |

---

## 4. 编译

### 4.1 一键构建（推荐）

```bat
cd /d X:\vscode\Evan\evan-ocr-main
build_qt5.bat
```

脚本依次执行：

1. 校验 MinGW 与 Qt 路径是否存在；
2. `cmake -S . -B build-qt5 -G Ninja -DCMAKE_BUILD_TYPE=Release ...`；
3. `cmake --build build-qt5 --parallel`；
4. 调用 `scripts\deploy_qt5.bat` 完成运行时部署。

产物：`build-qt5\evan.exe`，并且该目录已是**可直接双击运行**的绿色目录。

### 4.2 手工构建

```bat
set "PATH=C:\Mingws\winlibs-x86_64-posix-seh-gcc-12.1.0-llvm-14.0.6-mingw-w64msvcrt-10.0.0-r3\bin;C:\Qts\5.12\5.12.10\mingw73_64\bin;%PATH%"

cmake -S . -B build-qt5 -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_CXX_COMPILER="C:/Mingws/winlibs-x86_64-posix-seh-gcc-12.1.0-llvm-14.0.6-mingw-w64msvcrt-10.0.0-r3/bin/g++.exe" ^
      -DCMAKE_RC_COMPILER="C:/Mingws/winlibs-x86_64-posix-seh-gcc-12.1.0-llvm-14.0.6-mingw-w64msvcrt-10.0.0-r3/bin/windres.exe" ^
      -DQt5_DIR="C:/Qts/5.12/5.12.10/mingw73_64/lib/cmake/Qt5"

cmake --build build-qt5 --parallel
scripts\deploy_qt5.bat build-qt5\evan.exe
```

> **必须用正斜杠**写编译器路径。反斜杠会被 CMake 当成转义符，报 `Invalid character escape '\M'`。

### 4.3 Qt Creator

打开 `CMakeLists.txt` → 选择 mingw73_64 套件（或在 Kit 中把编译器指向上面那个 `g++.exe`）→ 直接构建。
`CMakeLists.txt` 已内置 `Qt5_DIR` 默认值，套件选对即可，无需手工填参数。

### 4.4 参数说明

| 参数 | 说明 |
| --- | --- |
| `-DQt5_DIR=.../lib/cmake/Qt5` | Qt 安装位置，换机器时唯一需要改的参数 |
| `-DCMAKE_BUILD_TYPE=Release` | 默认发布构建；`Debug` 会链接带 `d` 后缀的 Qt 库 |
| `-DCMAKE_CXX_COMPILER` / `CMAKE_RC_COMPILER` | 指定 g++ 与 windres，避免误用系统里的其它 MinGW |
| `EVAN_VERSION` | 由 `project(evan VERSION 1.2.1)` 自动注入，编译期宏 |

### 4.5 增量构建

改完代码只需重新编译，不必重复部署（Qt DLL 已就位）：

```bat
cmake --build build-qt5 --parallel
```

---

## 5. 部署

### 5.1 为什么分成两步

- **编译**只产出 `evan.exe`，它依赖 `Qt5Core/Gui/Widgets/Network`、平台插件 `qwindows.dll`、图片格式插件以及 OpenSSL。
- **部署**负责把这些运行时依赖铺到 exe 旁边，形成独立绿色目录。
- 分开的好处：日常改代码只跑第 1 步；只有换 Qt 版本、新增模块或准备发包时才需要第 2 步。

### 5.2 `scripts/deploy_qt5.bat` 做了什么

```bat
windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw build-qt5\evan.exe
copy /Y third_party\openssl\*.dll build-qt5\
```

1. `windeployqt` 依据 exe 的导入表自动推断依赖，复制 Qt DLL 与插件目录；
2. 把 `third_party\openssl` 下的两个 DLL 拷到 exe 同目录（Qt5 自身不会带）。

### 5.3 运行时目录清单

```
build-qt5\
├─ evan.exe                      2.7 MB   主程序
├─ Qt5Core.dll                   6.2 MB   Qt 核心
├─ Qt5Gui.dll                    6.3 MB   图形
├─ Qt5Widgets.dll                5.5 MB   控件
├─ Qt5Network.dll                1.7 MB   网络（更新/插件/在线公式）
├─ Qt5Svg.dll                    348 KB   SVG 图标（下拉箭头等）**必需**
├─ libgcc_s_seh-1.dll            616 KB   GCC 运行库（Qt DLL 依赖）
├─ libstdc++-6.dll               2.0 MB   C++ 运行库（Qt DLL 依赖）
├─ libwinpthread-1.dll            92 KB   线程库
├─ libEGL.dll / libGLESV2.dll    3.9 MB   ANGLE（Qt GUI 图形后端）
├─ libssl-1_1-x64.dll            788 KB   OpenSSL：TLS 协议
├─ libcrypto-1_1-x64.dll         2.9 MB   OpenSSL：加密算法
├─ platforms\qwindows.dll        1.8 MB   **缺少则程序无法启动**
├─ imageformats\                          PNG/JPEG/SVG/GIF/WebP 等图片插件
├─ iconengines\                           图标引擎
├─ styles\qwindowsvistastyle.dll          原生窗口样式
└─ bearer\                                网络会话管理插件
```

合计约 **37 MB**（不含 `CMakeCache.txt`、`CMakeFiles`、`*.ninja` 等构建缓存）。

### 5.4 关于 OpenSSL（不需要编译）

`third_party\openssl` 里是两个**预编译好的 Windows 运行库**，不参与 CMake 构建，没有头文件或 `.lib` 依赖：

| 文件 | 说明 |
| --- | --- |
| `libssl-1_1-x64.dll` | TLS 协议实现（OpenSSL 1.1.1w x64） |
| `libcrypto-1_1-x64.dll` | 加密算法库 |
| `LICENSE.txt` | OpenSSL 许可（Apache 1.0） |

- **为什么必须带**：Qt 5.12 只有 OpenSSL 这一种 TLS 后端（Schannel 后端是 Qt 6 才有的插件）。缺了它，「检查更新 / 插件市场下载 / 在线公式识别」会全部报 `TLS initialization failed`。
- **版本限制**：只能 1.1.x，**不能用 OpenSSL 3.x**（Qt5 不识别）；必须是 **64 位**（Qt Creator 自带的那份是 32 位，不能用）。
- 若完全不需要联网功能，删掉也能启动，只是相关功能会失败。

### 5.5 打包分发

从 `build-qt5` 中只拷以下内容即可：

```
evan.exe
Qt5Core.dll  Qt5Gui.dll  Qt5Widgets.dll  Qt5Network.dll  Qt5Svg.dll
libgcc_s_seh-1.dll  libstdc++-6.dll  libwinpthread-1.dll
libEGL.dll  libGLESv2.dll
libssl-1_1-x64.dll  libcrypto-1_1-x64.dll
platforms\  imageformats\  styles\  iconengines\  bearer\
```

**不要拷**：`CMakeCache.txt`、`CMakeFiles\`、`build.ninja`、`cmake_install.cmake`、`evan_autogen\`（均为构建缓存）。

可选内容（非必需）：`plugins\` 下的离线插件、`resources\`（图标已内嵌在 exe 的 qrc 中）。

---

## 6. 迁移技术细节

### 6.1 Qt 6 → Qt 5.12 API 对照

| 位置 | Qt 6 写法 | Qt 5.12 写法 | 原因 |
| --- | --- | --- | --- |
| 快捷键解析 | `QKeyCombination(mods, key)` | 打包整数 `int(mods) \| key` | `QKeyCombination` 是 Qt6 新增 |
| 取组合键 | `seq[0].key()` / `.keyboardModifiers()` | `seq[0]` 按位取键值与修饰键 | Qt5 的 `operator[]` 返回 `int` |
| 线程池 | `QThreadPool::start(lambda)` | `QRunnable` 封装后 `start()` | Qt5 无 lambda 重载 |
| 原生事件 | `nativeEventFilter(..., qintptr*)` | `nativeEventFilter(..., long*)` | 影响全局热键，签名不符则不触发 |
| 进入事件 | `enterEvent(QEnterEvent*)` | `enterEvent(QEvent*)` | `QEnterEvent` 是 Qt6 引入 |
| 鼠标坐标 | `event->globalPosition().toPoint()` | `event->globalPos()` | 同上 |
| 滚轮坐标 | `event->position().toPoint()` | `event->pos()` | 同上 |
| 网络超时 | `request.setTransferTimeout(ms)` | `QTimer::singleShot` + `reply->abort()` | Qt 5.15 才有该接口 |
| HTTP/2 开关 | `Http2AllowedAttribute` | 移除（默认 HTTP/1.1） | Qt 5.12 无此属性 |
| 进程启动 | `setProgram/setArguments/start()` | `start(program, args)` | 分离式 API 是 Qt 5.15+ |
| JSON 整数 | `QJsonValue::toInteger()` | `toVariant().toLongLong()` | Qt 5.15 才有 `toInteger` |
| 富文本 | `QTextBrowser::setMarkdown()` | 内置 `SimpleMarkdown::toHtml()` | Qt 5.14 才有 Markdown |
| 标签图片 | `label->pixmap().isNull()` | `label->pixmap()` 返回 `const QPixmap*` | 返回类型不同 |
| 下拉信号 | `&QComboBox::currentIndexChanged` | `QOverload<int>::of(...)` | Qt5 有 int/QString 两个重载 |
| 调色板 | `QGuiApplication::palette()` | `QApplication::palette()` | Qt5 该静态方法在 QApplication |
| 高 DPI | `setHighDpiScaleFactorRoundingPolicy` | 加 `AA_EnableHighDpiScaling` + 版本条件编译 | 前者是 Qt 5.14+ |
| 构建 | 链接 `windowsapp` / `runtimeobject` | 移除 `windowsapp`，补 `ole32/oleaut32/runtimeobject/dwmapi/shcore` | MSVC 专用导入库 |

### 6.2 Windows 系统 OCR 的重写

**问题**：原实现使用 C++/WinRT（`winrt/Windows.Media.Ocr.h`），这些头文件**只支持 MSVC**，MinGW-GCC 无法编译。

**方案**：改为直接调用 WinRT 的 COM 二进制接口，与 C++/WinRT 投影层完全等价：

```
RoGetActivationFactory("Windows.Media.Ocr.OcrEngine", IOcrEngineStatics)
  → 枚举 AvailableRecognizerLanguages（优先 zh-*）
  → TryCreateFromLanguage / TryCreateFromUserProfileLanguages
  → Windows.Storage.Streams.Buffer (IBufferFactory::Create) + IBufferByteAccess 写入 BGRA 像素
  → ISoftwareBitmapStatics::CreateCopyFromBuffer
  → IOcrEngine::RecognizeAsync → IAsyncOperation<OcrResult> → IOcrResult
```

实现要点：

1. **所有接口调用通过显式虚表下标派发**（`vcall<Index>(obj, args...)`），不依赖 C++ 虚函数调用。实测 GCC 在 -O2 下对「继承自 `IInspectable` 的抽象接口」做虚调用会产生非法跳转（进程崩溃），改为按下标调用后稳定。
2. **WinRT 的接口继承不会摊平基接口方法**。每个接口的 vtable 都以 `IInspectable` 的 6 项开头（`QueryInterface/AddRef/Release/GetIids/GetRuntimeClassName/GetTrustLevel`），自身方法从下标 6 开始。按传统 COM 的「派生接口 = 基接口方法 + 新方法」写会偏移，直接崩。
3. **`IBufferByteAccess` 是普通 COM 接口**（只继承 `IUnknown`），其 `Buffer()` 在下标 3。
4. 接口 IID 取自官方 `.winmd` 元数据（`winmdidl` 导出 IDL 交叉核对），不是猜测值。
5. 异步等待：`IAsyncOperation` 经 `QueryInterface(IID_IAsyncInfo)` 取状态视图轮询（2 ms 间隔，30 s 上限），完成后 `GetResults`。
6. 行为一致性：语言包选择顺序、2 倍超分预处理、`dpr` 坐标还原、行/词包围盒合并逻辑，全部与 C++/WinRT 版本一一对应。

### 6.3 其他构建适配

- **资源编译**：`app.rc` 含中文版本信息，windres 需 `--codepage=65001`，否则 `FileDescription` 会乱码。
- **GCC 运行库**：`target_link_options(-static-libgcc -static-libstdc++)` 让 exe 自带运行库；但 Qt 的 DLL 仍依赖共享版 `libstdc++-6.dll`，仍需随包分发。
- **`QScreen::grabWindow` 语义**：实测 Qt 5.12 与 Qt 6 一致（子区域参数为逻辑像素，返回带 DPR 的物理位图），因此 GIF 录制 / 滚动截图的坐标换算**无需改动**。

---

## 7. 验证结果

| 验证项 | 结果 |
| --- | --- |
| 从空目录全量构建 | 通过，仅 2 个既有未用变量告警 |
| 一键脚本 `build_qt5.bat` | 通过（配置→编译→部署→OpenSSL 拷贝） |
| 程序启动 | 正常；HiDPI 识别正确（逻辑 1600×900 / 物理 3200×1800，DPR=2） |
| 全局热键 | F1/F2/F3 注册成功 |
| 系统 OCR | `Windows.Media.Ocr` 识图中英混排 2 行正确，耗时约 100 ms |
| HTTPS | 更新检查返回服务端 HTTP 状态（不再是 TLS 失败） |
| 版本信息 | exe 属性 `Evan 截贴图工具`、`1.2.1.0` 中文正常 |

---

## 8. 常见问题

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 双击无反应 / 一闪而过 | 缺 `platforms\qwindows.dll` | 重新执行 `scripts\deploy_qt5.bat` |
| 提示缺少 `Qt5Core.dll` 等 | 未部署 | 同上 |
| `TLS initialization failed` | 缺 OpenSSL 1.1 x64 | 确认两个 OpenSSL DLL 与 exe 同目录 |
| CMake 报 `Invalid character escape '\M'` | 编译器路径用了反斜杠 | 改成正斜杠 |
| `Could not find a package configuration file ... Qt5` | `Qt5_DIR` 未指到 `lib/cmake/Qt5` | 检查参数或环境变量 |
| 链接出现大量 undefined reference | 混用了其它 MinGW（如 MSYS2 UCRT64） | 只用本文档指定的工具链 |
| Debug 构建报缺 `Qt5Cored.dll` | Debug 需带 `d` 后缀的 Qt 库 | 改用 Release，或部署 Debug 版依赖 |
| 全局热键注册失败（日志 WARN） | 快捷键被其它程序占用 | 托盘 → 设置 里换一个组合 |
| 中文版本信息乱码 | windres 未按 UTF-8 解析 | 确认 `CMAKE_RC_FLAGS` 含 `--codepage=65001` |

运行日志：`%LOCALAPPDATA%\Evan\evan.log`（超过 2 MB 自动轮转为 `evan.log.1`）。
用户配置：注册表 `HKCU\Software\Evan\Evan`。

---

## 9. 运行与使用（速览）

双击 `build-qt5\evan.exe`，程序常驻托盘（单实例，重复启动会提示）。

| 操作 | 默认快捷键 |
| --- | --- |
| 截图（标注/OCR/贴图） | `F1` |
| 一键全屏截图 | `F2` |
| 贴图 | `F3` |

- 截图后：矩形/箭头/画笔/文字/马赛克/改字，`Ctrl+O` 识别文字、`Ctrl+S` 保存、`Ctrl+Z` 撤销、`Esc` 取消
- 贴图窗口：滚轮缩放、`Ctrl+滚轮` 调透明度、`Ctrl+O` 识别、`Ctrl+S` 保存、双击/`Esc` 关闭
- 托盘右键：插件中心（可安装 RapidOCR、LaTeX 公式离线插件）、检查更新、设置、退出

---

## 10. 文件索引

| 文件 | 用途 |
| --- | --- |
| `CMakeLists.txt` | 构建定义：Qt5 组件、源文件收集、Windows 链接库、编译选项 |
| `build_qt5.bat` | 一键构建 + 部署 |
| `scripts/deploy_qt5.bat` | windeployqt + OpenSSL 拷贝 |
| `src/qt5_compat.h` | Qt5 兼容层 |
| `src/core/ocr/windows_media_ocr.cpp` | WinRT ABI 直调的 OCR 实现 |
| `src/ui/common/simple_markdown.h` | Qt 5.12 的 Markdown 渲染 |
| `third_party/openssl/` | HTTPS 运行库（预编译，无需构建） |
| `BUILD_QT5.md` | 一页速查版 |

---

*本文档随工程一起维护，改动构建方式或依赖时请同步更新。*
