#pragma once

/**
 * @file qt5_compat.h
 * @brief Qt 5.12 兼容层
 *
 * 本项目原始版本基于 Qt 6 编写，此处集中封装 Qt 6 → Qt 5.12 的 API 差异，
 * 使上层业务代码保持与原先完全一致的行为语义。
 */

#include <QtGlobal>
#include <QKeySequence>
#include <QThreadPool>
#include <QRunnable>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <functional>
#include <utility>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#define EVAN_HAS_QT6 1
#else
#define EVAN_HAS_QT6 0
#endif

namespace evan {

// ---------------------------------------------------------------------------
// 快捷键组合：Qt 6 使用 QKeyCombination；Qt 5.12 中 QKeySequence::operator[]
// 直接返回打包了修饰键的 int。两者语义一致，此处统一为一组轻量辅助函数。
// ---------------------------------------------------------------------------

/** 取快捷键序列第一段的打包码 (修饰键 << 24 | 键值) */
inline int firstKeyCode(const QKeySequence& seq)
{
    if (seq.isEmpty()) {
        return 0;
    }
    return seq[0];
}

/** 从打包码中提取主键 */
inline Qt::Key keyOf(int combo)
{
    return static_cast<Qt::Key>(combo & ~Qt::KeyboardModifierMask);
}

/** 从打包码中提取修饰键 */
inline Qt::KeyboardModifiers modifiersOf(int combo)
{
    return static_cast<Qt::KeyboardModifiers>(combo & Qt::KeyboardModifierMask);
}

/** 由修饰键与主键合成打包码 */
inline int makeKeyCode(Qt::KeyboardModifiers mods, Qt::Key key)
{
    return static_cast<int>(mods) | static_cast<int>(key);
}

// ---------------------------------------------------------------------------
// QThreadPool 异步派发：Qt 6 支持直接投递 lambda / std::function，
// Qt 5.12 仅接受 QRunnable*，此处提供等价封装。
// ---------------------------------------------------------------------------

namespace detail {
class FunctionRunnable : public QRunnable
{
public:
    explicit FunctionRunnable(std::function<void()> fn) : m_fn(std::move(fn))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        if (m_fn) {
            m_fn();
        }
    }

private:
    std::function<void()> m_fn;
};
} // namespace detail

/** 等价于 Qt 6 的 QThreadPool::start(Callable) */
inline void runInThreadPool(std::function<void()> fn)
{
    QThreadPool::globalInstance()->start(new detail::FunctionRunnable(std::move(fn)));
}

// ---------------------------------------------------------------------------
// 队列化调用：Qt 5.12 的 QMetaObject::invokeMethod 已支持 functor 重载，
// 但需要显式指定接收者上下文，此处保持 Qt 6 的调用风格。
// ---------------------------------------------------------------------------
template <typename Context, typename Fn>
inline void invokeQueued(Context* context, Fn&& fn)
{
    QMetaObject::invokeMethod(context, std::forward<Fn>(fn), Qt::QueuedConnection);
}

} // namespace evan
