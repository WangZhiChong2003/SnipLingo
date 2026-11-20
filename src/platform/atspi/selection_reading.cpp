#include "platform/atspi/selection_reading.h"

namespace platform::atspi {

bool isIgnorableRole(const QString &roleName) {
    // AT-SPI exposes password entries as the "password text" role; some toolkits also use
    // "password" or "protected". Nothing here may ever be read.
    return roleName.compare(QLatin1String("password text"), Qt::CaseInsensitive) == 0
        || roleName.compare(QLatin1String("password"), Qt::CaseInsensitive) == 0
        || roleName.compare(QLatin1String("protected"), Qt::CaseInsensitive) == 0;
}

bool isOwnApplication(const QString &appName, const QString &ownAppName) {
    return !ownAppName.isEmpty() && appName.compare(ownAppName, Qt::CaseInsensitive) == 0;
}

core::Result<selection::SelectionText> composeSelection(
    const QStringList &rangeTexts, std::optional<QRect> anchor, const ReadPolicy &policy) {
    QStringList nonEmpty;
    for (const QString &part : rangeTexts) {
        if (!part.trimmed().isEmpty())
            nonEmpty.append(part);
    }
    if (nonEmpty.isEmpty()) {
        // Extremely common: a drag or a caret move reports a selection without content.
        return core::Error {
            core::ErrorCode::SelectionEmpty, QStringLiteral("no selection ranges with text")};
    }

    const QString joined = nonEmpty.join(QLatin1Char('\n'));
    if (joined.size() > policy.hardCap) {
        return core::Error {core::ErrorCode::SelectionTooLarge,
            QStringLiteral("%1 characters exceed the backend cap of %2")
                .arg(joined.size())
                .arg(policy.hardCap)};
    }

    selection::SelectionText text;
    text.text = joined.trimmed();
    if (text.text.isEmpty()) {
        return core::Error {
            core::ErrorCode::SelectionEmpty, QStringLiteral("selection is whitespace only")};
    }
    text.anchorLogical = anchor;
    return text;
}

}  // namespace platform::atspi
