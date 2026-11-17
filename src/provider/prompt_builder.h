// Prompt construction, versioned so behaviour changes are attributable in logs without logging any
// text (docs/design/06-provider.md §6).
#pragma once

#include <QString>

namespace provider::prompt {

constexpr int kPromptVersion = 1;

// English language name for a BCP-47 tag, e.g. "zh-Hans" -> "Simplified Chinese".
// Unknown tags fall back to a sentence that still identifies the language unambiguously.
QString languageNameFor(const QString &bcp47Tag);

QString systemPrompt(const QString &targetLanguageTag);

}  // namespace provider::prompt
