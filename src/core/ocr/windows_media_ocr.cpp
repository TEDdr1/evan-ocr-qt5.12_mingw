/**
 * @file windows_media_ocr.cpp
 * @brief Windows.Media.Ocr 原生离线 OCR 引擎 (MinGW / GCC 兼容实现)
 *
 * 原始 Qt 6 版本使用 C++/WinRT (winrt::Windows::Media::Ocr) 调用系统 OCR，
 * 但 C++/WinRT 头文件仅支持 MSVC 编译器，在 MinGW-GCC 工具链下无法编译。
 *
 * 因此本文件直接以「WinRT ABI 二进制接口」方式调用同一套系统 API：
 *   RoGetActivationFactory → IOcrEngineStatics → 创建 OcrEngine
 *   → SoftwareBitmap(ISoftwareBitmapStatics.CreateCopyFromBuffer)
 *   → IOcrEngine::RecognizeAsync → IAsyncOperation<OcrResult> → IOcrResult
 * 与 C++/WinRT 投影层完全等价，语言包选择策略、识别结果与坐标换算均保持一致。
 *
 * 说明：所有 WinRT 接口调用都通过「显式虚表下标派发」(vcall<N>) 完成，
 * 而非依赖 C++ 虚函数调用。下标与官方 .winmd 元数据 vtable 顺序严格一一对应，
 * 这样也避开了跨编译器（GCC/MinGW）生成虚调用时可能出现的代码生成差异。
 */

#include "windows_media_ocr.h"
#include <QThreadPool>
#include <QMetaObject>
#include <QDebug>
#include <cmath>
#include <cstring>
#include "qt5_compat.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#ifdef Q_OS_WIN
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#endif

namespace {

#ifdef Q_OS_WIN

// ===========================================================================
// WinRT ABI 常量与辅助
// ===========================================================================

// 所有 WinRT 接口对象的虚表前 6 项均来自 IInspectable：
//   0 QueryInterface  1 AddRef  2 Release  3 GetIids  4 GetRuntimeClassName  5 GetTrustLevel
// 因此「接口自身方法」从下标 6 开始。

static const GUID IID_IBufferByteAccess_ =
    {0x905a0fef, 0xbc53, 0x11df, {0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda}};

static const GUID IID_IAsyncInfo_ =
    {0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

/** 按虚表下标调用 WinRT 接口方法 */
template <int Index, typename Ret, typename... Args>
inline Ret vcall(void* obj, Args... args)
{
    using Fn = Ret(STDMETHODCALLTYPE*)(void*, Args...);
    void** vtbl = *reinterpret_cast<void***>(obj);
    return reinterpret_cast<Fn>(vtbl[Index])(obj, args...);
}

/** IUnknown::QueryInterface */
inline HRESULT comQueryInterface(void* obj, const GUID& iid, void** out)
{
    using Fn = HRESULT(STDMETHODCALLTYPE*)(void*, const GUID&, void**);
    void** vtbl = *reinterpret_cast<void***>(obj);
    return reinterpret_cast<Fn>(vtbl[0])(obj, iid, out);
}

/** IUnknown::Release */
inline ULONG comRelease(void* obj)
{
    using Fn = ULONG(STDMETHODCALLTYPE*)(void*);
    void** vtbl = *reinterpret_cast<void***>(obj);
    return reinterpret_cast<Fn>(vtbl[2])(obj);
}

// --- Windows.Storage.Streams.IBuffer ---------------------------------------
// get_Capacity(6) / get_Length(7) / put_Length(8)
inline void bufferPutLength(void* buffer, UINT32 length)
{
    vcall<8, HRESULT, UINT32>(buffer, length);
}

// --- Windows.Storage.Streams.IBufferFactory::Create ------------------------
inline HRESULT bufferFactoryCreate(void* factory, UINT32 capacity, void** out)
{
    return vcall<6, HRESULT, UINT32, void**>(factory, capacity, out);
}

// --- Windows.Globalization.ILanguage::get_LanguageTag ----------------------
inline HRESULT languageGetTag(void* language, HSTRING* out)
{
    return vcall<6, HRESULT, HSTRING*>(language, out);
}

// --- Windows.Graphics.Imaging.ISoftwareBitmapStatics ----------------------
// Copy(6) Convert(7) ConvertWithAlpha(8) CreateCopyFromBuffer(9)
inline HRESULT softwareBitmapCreateFromBuffer(void* statics, void* buffer, INT32 format, INT32 width,
                                              INT32 height, void** out)
{
    return vcall<9, HRESULT, void*, INT32, INT32, INT32, void**>(statics, buffer, format, width, height, out);
}

// --- Windows.Foundation.Collections.IVectorView<T> ------------------------
// GetAt(6) / get_Size(7) / IndexOf(8) / GetMany(9)
inline HRESULT vectorViewGetSize(void* view, UINT32* size)
{
    return vcall<7, HRESULT, UINT32*>(view, size);
}

inline HRESULT vectorViewGetAt(void* view, UINT32 index, void** item)
{
    return vcall<6, HRESULT, UINT32, void**>(view, index, item);
}

// --- Windows.Media.Ocr.IOcrEngineStatics ---------------------------------
// get_MaxImageDimension(6) get_AvailableRecognizerLanguages(7) IsLanguageSupported(8)
// TryCreateFromLanguage(9) TryCreateFromUserProfileLanguages(10)
inline HRESULT ocrStaticsAvailableLanguages(void* statics, void** out)
{
    return vcall<7, HRESULT, void**>(statics, out);
}

inline HRESULT ocrStaticsTryCreateFromLanguage(void* statics, void* language, void** out)
{
    return vcall<9, HRESULT, void*, void**>(statics, language, out);
}

inline HRESULT ocrStaticsTryCreateFromUserProfileLanguages(void* statics, void** out)
{
    return vcall<10, HRESULT, void**>(statics, out);
}

// --- Windows.Media.Ocr.IOcrEngine ----------------------------------------
// RecognizeAsync(6) / get_RecognizerLanguage(7)
inline HRESULT ocrEngineRecognizeAsync(void* engine, void* bitmap, void** out)
{
    return vcall<6, HRESULT, void*, void**>(engine, bitmap, out);
}

// --- Windows.Media.Ocr.IOcrResult ----------------------------------------
// get_Lines(6) / get_TextAngle(7) / get_Text(8)
inline HRESULT ocrResultGetLines(void* result, void** out)
{
    return vcall<6, HRESULT, void**>(result, out);
}

// --- Windows.Media.Ocr.IOcrLine ------------------------------------------
// get_Words(6) / get_Text(7)
inline HRESULT ocrLineGetWords(void* line, void** out)
{
    return vcall<6, HRESULT, void**>(line, out);
}

inline HRESULT ocrLineGetText(void* line, HSTRING* out)
{
    return vcall<7, HRESULT, HSTRING*>(line, out);
}

// --- Windows.Media.Ocr.IOcrWord ------------------------------------------
// get_BoundingRect(6) / get_Text(7)
struct WinRtRect {
    FLOAT X;
    FLOAT Y;
    FLOAT Width;
    FLOAT Height;
};

inline HRESULT ocrWordGetBoundingRect(void* word, WinRtRect* out)
{
    return vcall<6, HRESULT, WinRtRect*>(word, out);
}

inline HRESULT ocrWordGetText(void* word, HSTRING* out)
{
    return vcall<7, HRESULT, HSTRING*>(word, out);
}

// --- Windows.Foundation.IAsyncInfo ---------------------------------------
// get_Id(6) / get_Status(7) / get_ErrorCode(8) / Cancel(9) / Close(10)
enum WinRtAsyncStatus {
    WinRtAsyncStatus_Started = 0,
    WinRtAsyncStatus_Completed = 1,
    WinRtAsyncStatus_Canceled = 2,
    WinRtAsyncStatus_Error = 3
};

inline HRESULT asyncInfoGetStatus(void* info, INT32* status)
{
    return vcall<7, HRESULT, INT32*>(info, status);
}

inline HRESULT asyncInfoGetErrorCode(void* info, HRESULT* errorCode)
{
    return vcall<8, HRESULT, HRESULT*>(info, errorCode);
}

// --- Windows.Foundation.IAsyncOperation<T> -------------------------------
// put_Completed(6) / get_Completed(7) / GetResults(8)
inline HRESULT asyncOperationGetResults(void* operation, void** results)
{
    return vcall<8, HRESULT, void**>(operation, results);
}

// ===========================================================================
// 轻量 WinRT 互操作辅助
// ===========================================================================

/** 确保当前线程处于 MTA 公寓（幂等，可安全重复调用） */
void ensureApartment()
{
    static thread_local bool initialized = false;
    if (initialized) {
        return;
    }
    // 已初始化时返回 S_FALSE / RPC_E_CHANGED_MODE，均属正常情况：
    // 此时线程已处于某个公寓中，WinRT 调用依然有效。
    RoInitialize(RO_INIT_MULTITHREADED);
    initialized = true;
}

/** 取得 WinRT 运行类的激活工厂（静态方法容器） */
HRESULT getActivationFactory(const wchar_t* className, const GUID& iid, void** out)
{
    HSTRING classId = nullptr;
    HRESULT hr = WindowsCreateString(className, static_cast<UINT32>(wcslen(className)), &classId);
    if (FAILED(hr)) {
        return hr;
    }
    hr = RoGetActivationFactory(classId, iid, out);
    WindowsDeleteString(classId);
    return hr;
}

/** HSTRING → QString */
QString hstringToQString(HSTRING value)
{
    if (!value) {
        return QString();
    }
    UINT32 length = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(value, &length);
    return QString::fromWCharArray(raw, static_cast<int>(length));
}

/** COM 引用持有者（RAII） */
class ComRef
{
public:
    ComRef() = default;
    ~ComRef() { reset(); }

    ComRef(const ComRef&) = delete;
    ComRef& operator=(const ComRef&) = delete;

    void* get() const { return m_ptr; }
    void** put() { return &m_ptr; }
    bool valid() const { return m_ptr != nullptr; }

    void attach(void* ptr)
    {
        reset();
        m_ptr = ptr;
    }

    void reset()
    {
        if (m_ptr) {
            comRelease(m_ptr);
            m_ptr = nullptr;
        }
    }

private:
    void* m_ptr = nullptr;
};

/** 等待 IAsyncOperation 完成并取回结果（MTA 公寓下由系统线程池驱动） */
HRESULT awaitAsyncOperation(void* operation, void** results)
{
    if (!operation) {
        return E_POINTER;
    }

    // IAsyncOperation 以 IAsyncInfo 为基接口（独立虚表），需 QI 取得状态查询视图
    ComRef asyncInfo;
    HRESULT hr = comQueryInterface(operation, IID_IAsyncInfo_, asyncInfo.put());
    if (FAILED(hr) || !asyncInfo.valid()) {
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }

    const DWORD deadline = GetTickCount() + 30000; // 30 秒保护上限
    for (;;) {
        INT32 status = WinRtAsyncStatus_Started;
        hr = asyncInfoGetStatus(asyncInfo.get(), &status);
        if (FAILED(hr)) {
            return hr;
        }
        if (status == WinRtAsyncStatus_Completed) {
            break;
        }
        if (status == WinRtAsyncStatus_Canceled) {
            return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        }
        if (status == WinRtAsyncStatus_Error) {
            HRESULT errorCode = E_FAIL;
            asyncInfoGetErrorCode(asyncInfo.get(), &errorCode);
            return errorCode;
        }
        if (GetTickCount() > deadline) {
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        Sleep(2); // 让出 CPU，等待后台识别线程完成
    }

    return asyncOperationGetResults(operation, results);
}

/** 遍历 IVectorView<T>，逐个回调并释放元素引用 */
template <typename Callback>
void forEachInVectorView(void* view, Callback&& callback)
{
    if (!view) {
        return;
    }
    UINT32 size = 0;
    if (FAILED(vectorViewGetSize(view, &size))) {
        return;
    }
    for (UINT32 i = 0; i < size; ++i) {
        void* item = nullptr;
        if (SUCCEEDED(vectorViewGetAt(view, i, &item)) && item) {
            callback(item);
            comRelease(item);
        }
    }
}

// BitmapPixelFormat::Bgra8 / BitmapAlphaMode::Premultiplied
constexpr INT32 kBitmapPixelFormatBgra8 = 87;

// 各静态接口的 IID
static const GUID IID_IOcrEngineStatics_ =
    {0x5bffa85a, 0x3384, 0x3540, {0x99, 0x40, 0x69, 0x91, 0x20, 0xd4, 0x28, 0xa8}};

static const GUID IID_ISoftwareBitmapStatics_ =
    {0xdf0385db, 0x672f, 0x4a9d, {0x80, 0x6e, 0xc2, 0x44, 0x2f, 0x34, 0x3e, 0x86}};

static const GUID IID_IBufferFactory_ =
    {0x71af914d, 0xc10f, 0x484b, {0xbc, 0x50, 0x14, 0xbc, 0x62, 0x3b, 0x3a, 0x27}};

#endif // Q_OS_WIN

} // namespace

WindowsMediaOcrEngine::WindowsMediaOcrEngine(QObject* parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    ensureApartment();
    try {
        ComRef statics;
        if (SUCCEEDED(getActivationFactory(L"Windows.Media.Ocr.OcrEngine", IID_IOcrEngineStatics_,
                                          statics.put()))) {
            ComRef languages;
            if (SUCCEEDED(ocrStaticsAvailableLanguages(statics.get(), languages.put())) && languages.valid()) {
                UINT32 size = 0;
                vectorViewGetSize(languages.get(), &size);
                m_isAvailable = (size > 0);
            }
        }
    } catch (...) {
        m_isAvailable = false;
    }
#endif
}

bool WindowsMediaOcrEngine::isAvailable() const
{
    return m_isAvailable;
}

OcrResult WindowsMediaOcrEngine::recognize(const QImage& image, qreal dpr)
{
    OcrResult result;
    result.dpr = dpr;

#ifdef Q_OS_WIN
    ensureApartment();

    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        result.success = false;
        result.errorMessage = "待识别图像数据为空";
        return result;
    }

    try {
        // 1. 取得系统 OCR 引擎：优先中文识别包以支持中英混排
        ComRef statics;
        HRESULT hr = getActivationFactory(L"Windows.Media.Ocr.OcrEngine", IID_IOcrEngineStatics_,
                                         statics.put());
        if (FAILED(hr) || !statics.valid()) {
            result.success = false;
            result.errorMessage = "当前 Windows 系统未检测到可用的 OCR 识别语言包";
            return result;
        }

        ComRef languages;
        ocrStaticsAvailableLanguages(statics.get(), languages.put());

        ComRef engine;

        // 优先寻找 zh-* 语言包
        if (languages.valid()) {
            UINT32 count = 0;
            vectorViewGetSize(languages.get(), &count);
            for (UINT32 i = 0; i < count && !engine.valid(); ++i) {
                void* lang = nullptr;
                if (FAILED(vectorViewGetAt(languages.get(), i, &lang)) || !lang) {
                    continue;
                }

                HSTRING rawTag = nullptr;
                QString languageTag;
                if (SUCCEEDED(languageGetTag(lang, &rawTag)) && rawTag) {
                    languageTag = hstringToQString(rawTag);
                    WindowsDeleteString(rawTag);
                }

                if (languageTag.startsWith(QLatin1String("zh"))) {
                    void* created = nullptr;
                    if (SUCCEEDED(ocrStaticsTryCreateFromLanguage(statics.get(), lang, &created)) && created) {
                        engine.attach(created);
                    }
                }
                comRelease(lang);
            }
        }

        // 回退：按用户配置的语言列表创建
        if (!engine.valid()) {
            void* created = nullptr;
            if (SUCCEEDED(ocrStaticsTryCreateFromUserProfileLanguages(statics.get(), &created)) && created) {
                engine.attach(created);
            }
        }

        // 最终回退：取第一个可用语言包
        if (!engine.valid() && languages.valid()) {
            UINT32 count = 0;
            vectorViewGetSize(languages.get(), &count);
            if (count > 0) {
                void* lang = nullptr;
                if (SUCCEEDED(vectorViewGetAt(languages.get(), 0, &lang)) && lang) {
                    void* created = nullptr;
                    if (SUCCEEDED(ocrStaticsTryCreateFromLanguage(statics.get(), lang, &created)) && created) {
                        engine.attach(created);
                    }
                    comRelease(lang);
                }
            }
        }

        if (!engine.valid()) {
            result.success = false;
            result.errorMessage = "当前 Windows 系统未检测到可用的 OCR 识别语言包";
            return result;
        }

        // 2. 图像预处理：中小型截图自适应 2 倍平滑插值超分，提升小字召回率
        qreal scaleFactor = 1.0;
        if (image.width() < 1000 || image.height() < 500) {
            scaleFactor = 2.0;
        }

        QImage processedImg = image;
        if (scaleFactor > 1.01) {
            processedImg = image.scaled(
                static_cast<int>(image.width() * scaleFactor),
                static_cast<int>(image.height() * scaleFactor),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation);
        }

        QImage bgraImg = processedImg.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const int w = bgraImg.width();
        const int h = bgraImg.height();
        const UINT32 byteCount = static_cast<UINT32>(w) * static_cast<UINT32>(h) * 4u;

        // 3. 通过 Windows.Storage.Streams.Buffer 承载像素数据
        ComRef bufferFactory;
        hr = getActivationFactory(L"Windows.Storage.Streams.Buffer", IID_IBufferFactory_,
                                 bufferFactory.put());
        if (FAILED(hr) || !bufferFactory.valid()) {
            result.success = false;
            result.errorMessage = "创建 WinRT 像素缓冲区失败";
            return result;
        }

        ComRef pixelBuffer;
        hr = bufferFactoryCreate(bufferFactory.get(), byteCount, pixelBuffer.put());
        if (FAILED(hr) || !pixelBuffer.valid()) {
            result.success = false;
            result.errorMessage = "分配 WinRT 像素缓冲区失败";
            return result;
        }

        ComRef byteAccess;
        hr = comQueryInterface(pixelBuffer.get(), IID_IBufferByteAccess_, byteAccess.put());
        if (FAILED(hr) || !byteAccess.valid()) {
            result.success = false;
            result.errorMessage = "获取 WinRT 像素缓冲区内存指针失败";
            return result;
        }

        // IBufferByteAccess 是普通 COM 接口（仅继承 IUnknown），其唯一方法位于下标 3
        BYTE* rawBytes = nullptr;
        vcall<3, HRESULT, BYTE**>(byteAccess.get(), &rawBytes);
        if (rawBytes) {
            std::memcpy(rawBytes, bgraImg.constBits(), byteCount);
            bufferPutLength(pixelBuffer.get(), byteCount);
        }

        // 4. 由缓冲区创建 SoftwareBitmap
        ComRef bitmapStatics;
        hr = getActivationFactory(L"Windows.Graphics.Imaging.SoftwareBitmap",
                                 IID_ISoftwareBitmapStatics_, bitmapStatics.put());
        if (FAILED(hr) || !bitmapStatics.valid()) {
            result.success = false;
            result.errorMessage = "初始化 Windows.Graphics.Imaging 失败";
            return result;
        }

        ComRef softwareBitmap;
        hr = softwareBitmapCreateFromBuffer(bitmapStatics.get(), pixelBuffer.get(), kBitmapPixelFormatBgra8,
                                           w, h, softwareBitmap.put());
        if (FAILED(hr) || !softwareBitmap.valid()) {
            result.success = false;
            result.errorMessage = "创建 SoftwareBitmap 失败";
            return result;
        }

        // 5. 执行识别并等待异步结果
        ComRef operation;
        hr = ocrEngineRecognizeAsync(engine.get(), softwareBitmap.get(), operation.put());
        if (FAILED(hr) || !operation.valid()) {
            result.success = false;
            result.errorMessage = "启动 Windows OCR 识别失败";
            return result;
        }

        ComRef ocrResult;
        hr = awaitAsyncOperation(operation.get(), ocrResult.put());
        if (FAILED(hr) || !ocrResult.valid()) {
            result.success = false;
            result.errorMessage = "Windows OCR 识别过程发生错误";
            return result;
        }

        // 6. 解析识别结果
        result.success = true;
        QString fullText;
        const qreal safeDpr = (dpr > 0.0) ? dpr : 1.0;

        ComRef lines;
        if (SUCCEEDED(ocrResultGetLines(ocrResult.get(), lines.put())) && lines.valid()) {
            forEachInVectorView(lines.get(), [&](void* line) {
                OcrLine lineObj;

                HSTRING rawLineText = nullptr;
                if (SUCCEEDED(ocrLineGetText(line, &rawLineText)) && rawLineText) {
                    lineObj.text = hstringToQString(rawLineText);
                    WindowsDeleteString(rawLineText);
                }

                QRect lineBox;
                ComRef words;
                if (SUCCEEDED(ocrLineGetWords(line, words.put())) && words.valid()) {
                    forEachInVectorView(words.get(), [&](void* word) {
                        OcrWord wordObj;

                        HSTRING rawWordText = nullptr;
                        if (SUCCEEDED(ocrWordGetText(word, &rawWordText)) && rawWordText) {
                            wordObj.text = hstringToQString(rawWordText);
                            WindowsDeleteString(rawWordText);
                        }

                        WinRtRect r{0.f, 0.f, 0.f, 0.f};
                        ocrWordGetBoundingRect(word, &r);

                        // 还原超分辨率缩放前的真实物理坐标
                        const double realX = r.X / scaleFactor;
                        const double realY = r.Y / scaleFactor;
                        const double realW = r.Width / scaleFactor;
                        const double realH = r.Height / scaleFactor;

                        wordObj.boundingBox = QRect(
                            static_cast<int>(std::round(realX)),
                            static_cast<int>(std::round(realY)),
                            static_cast<int>(std::round(realW)),
                            static_cast<int>(std::round(realH)));

                        wordObj.logicalBox = QRect(
                            static_cast<int>(std::round(realX / safeDpr)),
                            static_cast<int>(std::round(realY / safeDpr)),
                            static_cast<int>(std::round(realW / safeDpr)),
                            static_cast<int>(std::round(realH / safeDpr)));

                        if (lineBox.isNull()) {
                            lineBox = wordObj.boundingBox;
                        } else {
                            lineBox = lineBox.united(wordObj.boundingBox);
                        }

                        lineObj.words.append(wordObj);
                    });
                }

                lineObj.boundingBox = lineBox;
                lineObj.logicalBox = QRect(
                    static_cast<int>(std::round(lineBox.x() / safeDpr)),
                    static_cast<int>(std::round(lineBox.y() / safeDpr)),
                    static_cast<int>(std::round(lineBox.width() / safeDpr)),
                    static_cast<int>(std::round(lineBox.height() / safeDpr)));

                result.lines.append(lineObj);
                if (!fullText.isEmpty()) {
                    fullText += QLatin1String("\n");
                }
                fullText += lineObj.text;
            });
        }

        result.fullText = fullText;

    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = QString::fromUtf8(e.what());
    } catch (...) {
        result.success = false;
        result.errorMessage = "执行 OCR 识别时发生未知异常";
    }
#else
    Q_UNUSED(image);
    result.success = false;
    result.errorMessage = "Windows.Media.Ocr 仅在 Windows 平台可用";
#endif

    return result;
}

void WindowsMediaOcrEngine::recognizeAsync(const QImage& image, qreal dpr,
                                           std::function<void(const OcrResult&)> callback)
{
    QImage imgCopy = image.copy();
    evan::runInThreadPool([this, imgCopy, dpr, callback]() {
        OcrResult res = recognize(imgCopy, dpr);
        evan::invokeQueued(this, [this, callback, res]() {
            if (callback) {
                callback(res);
            }
            emit recognitionFinished(res);
        });
    });
}
