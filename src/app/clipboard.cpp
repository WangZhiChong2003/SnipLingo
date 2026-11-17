#include "app/clipboard.h"

#include <QClipboard>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMimeData>
#include <QTimer>

namespace app {

QString readClipboardText(int waitMs) {
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return {};

    const auto current = [clipboard] {
        // text() and mimeData() take different paths inside the platform plugin; either can be the
        // one that carries the answer on Wayland.
        const QString direct = clipboard->text();
        if (!direct.isEmpty())
            return direct;
        const QMimeData *data = clipboard->mimeData();
        return data != nullptr && data->hasText() ? data->text() : QString();
    };

    const QString immediate = current();
    if (!immediate.isEmpty() || waitMs <= 0)
        return immediate;

    // Wait for the data-source round trip, bounded: a silent clipboard must not block the caller.
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(clipboard, &QClipboard::dataChanged, &loop, &QEventLoop::quit);
    deadline.start(waitMs);
    loop.exec();
    return current();
}

}  // namespace app
