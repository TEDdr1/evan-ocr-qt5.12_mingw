#pragma once

/**
 * @file simple_markdown.h
 * @brief 轻量 Markdown → HTML 转换（Qt 5.12 无 QTextDocument::setMarkdown）
 *
 * Qt 5.14 起才提供 QTextDocument::setMarkdown / QTextBrowser::setMarkdown。
 * 为在 Qt 5.12 上保持「更新日志按 Markdown 渲染」的原有表现，这里实现一个
 * 覆盖常见语法的轻量转换器：标题、粗体、斜体、行内代码、代码块、链接、
 * 无序/有序列表、引用、分隔线与段落。
 */

#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QCoreApplication>

namespace SimpleMarkdown {

inline QString escapeHtml(const QString& text)
{
    QString out = text;
    out.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    out.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    out.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    return out;
}

/** 处理行内语法：粗体、斜体、行内代码、链接 */
inline QString renderInline(const QString& raw)
{
    QString text = escapeHtml(raw);

    // 行内代码 `code`
    text.replace(QRegularExpression(QStringLiteral("`([^`]+)`")),
                 QStringLiteral("<code style=\"background:#eef2f7;padding:1px 4px;border-radius:3px;\">\\1</code>"));

    // 粗体 **text**
    text.replace(QRegularExpression(QStringLiteral("\\*\\*([^*]+)\\*\\*")),
                 QStringLiteral("<b>\\1</b>"));

    // 斜体 *text*
    text.replace(QRegularExpression(QStringLiteral("(^|[^*])\\*([^*]+)\\*")),
                 QStringLiteral("\\1<i>\\2</i>"));

    // 链接 [title](url)
    text.replace(QRegularExpression(QStringLiteral("\\[([^\\]]+)\\]\\(([^)]+)\\)")),
                 QStringLiteral("<a href=\"\\2\">\\1</a>"));

    return text;
}

/** 将 Markdown 文本转换为 HTML 片段 */
inline QString toHtml(const QString& markdown)
{
    const QStringList lines = markdown.split(QRegularExpression(QStringLiteral("\r?\n")));

    QString html;
    QString paragraph;
    bool inList = false;
    bool inOrderedList = false;
    bool inCodeBlock = false;
    QString codeBuffer;
    bool inQuote = false;

    auto flushParagraph = [&]() {
        if (!paragraph.trimmed().isEmpty()) {
            html += QStringLiteral("<p style=\"margin:6px 0;line-height:1.6;\">%1</p>")
                        .arg(renderInline(paragraph.trimmed()));
        }
        paragraph.clear();
    };

    auto closeLists = [&]() {
        if (inList) {
            html += QLatin1String("</ul>");
            inList = false;
        }
        if (inOrderedList) {
            html += QLatin1String("</ol>");
            inOrderedList = false;
        }
    };

    auto closeQuote = [&]() {
        if (inQuote) {
            html += QLatin1String("</blockquote>");
            inQuote = false;
        }
    };

    for (const QString& rawLine : lines) {
        const QString line = rawLine;

        // 代码块围栏
        if (line.trimmed().startsWith(QLatin1String("```"))) {
            if (inCodeBlock) {
                html += QStringLiteral("<pre style=\"background:#f1f5f9;padding:8px 10px;border-radius:6px;"
                                       "font-family:Consolas,monospace;font-size:12px;white-space:pre-wrap;\">%1</pre>")
                            .arg(escapeHtml(codeBuffer));
                codeBuffer.clear();
                inCodeBlock = false;
            } else {
                flushParagraph();
                closeLists();
                inCodeBlock = true;
            }
            continue;
        }
        if (inCodeBlock) {
            codeBuffer += line + QLatin1Char('\n');
            continue;
        }

        const QString trimmed = line.trimmed();

        if (trimmed.isEmpty()) {
            flushParagraph();
            closeLists();
            closeQuote();
            continue;
        }

        // 分隔线
        if (trimmed == QLatin1String("---") || trimmed == QLatin1String("***")) {
            flushParagraph();
            closeLists();
            html += QLatin1String("<hr style=\"border:none;border-top:1px solid #e2e8f0;margin:10px 0;\"/>");
            continue;
        }

        // 标题
        int headingLevel = 0;
        while (headingLevel < trimmed.size() && headingLevel < 6
               && trimmed.at(headingLevel) == QLatin1Char('#')) {
            ++headingLevel;
        }
        if (headingLevel > 0 && trimmed.size() > headingLevel && trimmed.at(headingLevel) == QLatin1Char(' ')) {
            flushParagraph();
            closeLists();
            closeQuote();
            const QString headingText = renderInline(trimmed.mid(headingLevel + 1).trimmed());
            const int fontSize = 17 - headingLevel;
            html += QStringLiteral("<p style=\"font-size:%1px;font-weight:700;margin:10px 0 6px 0;\">%2</p>")
                        .arg(fontSize)
                        .arg(headingText);
            continue;
        }

        // 引用
        if (trimmed.startsWith(QLatin1Char('>'))) {
            flushParagraph();
            closeLists();
            if (!inQuote) {
                html += QLatin1String("<blockquote style=\"margin:6px 0;padding-left:10px;"
                                      "border-left:3px solid #cbd5e1;color:#64748b;\">");
                inQuote = true;
            }
            html += QStringLiteral("<p style=\"margin:2px 0;\">%1</p>")
                        .arg(renderInline(trimmed.mid(1).trimmed()));
            continue;
        }

        // 无序列表
        if (trimmed.startsWith(QLatin1String("- ")) || trimmed.startsWith(QLatin1String("* "))
            || trimmed.startsWith(QLatin1String("+ "))) {
            flushParagraph();
            closeQuote();
            if (inOrderedList) {
                html += QLatin1String("</ol>");
                inOrderedList = false;
            }
            if (!inList) {
                html += QLatin1String("<ul style=\"margin:4px 0 4px 18px;\">");
                inList = true;
            }
            html += QStringLiteral("<li style=\"margin:2px 0;\">%1</li>")
                        .arg(renderInline(trimmed.mid(2).trimmed()));
            continue;
        }

        // 有序列表
        static const QRegularExpression orderedRe(QStringLiteral("^(\\d+)\\.\\s+(.*)$"));
        const QRegularExpressionMatch orderedMatch = orderedRe.match(trimmed);
        if (orderedMatch.hasMatch()) {
            flushParagraph();
            closeQuote();
            if (inList) {
                html += QLatin1String("</ul>");
                inList = false;
            }
            if (!inOrderedList) {
                html += QLatin1String("<ol style=\"margin:4px 0 4px 18px;\">");
                inOrderedList = true;
            }
            html += QStringLiteral("<li style=\"margin:2px 0;\">%1</li>")
                        .arg(renderInline(orderedMatch.captured(2).trimmed()));
            continue;
        }

        closeLists();
        closeQuote();
        if (!paragraph.isEmpty()) {
            paragraph += QLatin1Char(' ');
        }
        paragraph += trimmed;
    }

    if (inCodeBlock && !codeBuffer.isEmpty()) {
        html += QStringLiteral("<pre style=\"background:#f1f5f9;padding:8px 10px;border-radius:6px;"
                               "font-family:Consolas,monospace;font-size:12px;white-space:pre-wrap;\">%1</pre>")
                    .arg(escapeHtml(codeBuffer));
    }
    flushParagraph();
    closeLists();
    closeQuote();

    return html;
}

} // namespace SimpleMarkdown
