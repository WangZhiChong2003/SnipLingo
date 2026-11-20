#include "provider/prompt_builder.h"

#include <QHash>

namespace provider::prompt {
namespace {

const QHash<QString, QString> &languageNames() {
    static const QHash<QString, QString> names {
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("zh-Hans"), QStringLiteral("Simplified Chinese")},
        {QStringLiteral("zh-Hant"), QStringLiteral("Traditional Chinese")},
        {QStringLiteral("ja"), QStringLiteral("Japanese")},
        {QStringLiteral("ko"), QStringLiteral("Korean")},
        {QStringLiteral("de"), QStringLiteral("German")},
        {QStringLiteral("fr"), QStringLiteral("French")},
        {QStringLiteral("es"), QStringLiteral("Spanish")},
        {QStringLiteral("pt"), QStringLiteral("Portuguese")},
        {QStringLiteral("it"), QStringLiteral("Italian")},
        {QStringLiteral("ru"), QStringLiteral("Russian")},
        {QStringLiteral("ar"), QStringLiteral("Arabic")},
        {QStringLiteral("hi"), QStringLiteral("Hindi")},
    };
    return names;
}

}  // namespace

QString languageNameFor(const QString &bcp47Tag) {
    if (bcp47Tag.isEmpty())
        return QStringLiteral("the language identified by an unspecified tag");
    const auto known = languageNames().constFind(bcp47Tag);
    if (known != languageNames().constEnd())
        return *known;
    return QStringLiteral("the language identified by the BCP-47 tag \"%1\"").arg(bcp47Tag);
}

QString systemPrompt(const QString &targetLanguageTag) {
    const QString target = languageNameFor(targetLanguageTag);
    // Layout, identifiers and existing target-language text are handled here so the coordinator can
    // stay content-agnostic (the user's selection is never rewritten before it is sent).
    return QStringLiteral(
        "You are a translation engine. Translate the user's text into %1.\n"
        "Output only the translation. Preserve line breaks, numbers, identifiers, URLs and inline "
        "code exactly. Do not add explanations, notes, transliterations or quotation marks.\n"
        "If the text is already in %1, return it unchanged.")
        .arg(target);
}

}  // namespace provider::prompt
