#pragma once

#include <QPixmap>
#include <QQuickWindow>
#include <QScreen>
#include <QSignalSpy>

// Qt 6.2's software scene graph can leave QQuickWindow::grabWindow() empty on the offscreen
// platform. Capture the rendered raster backing store through the screen instead.
inline bool saveWindowSnapshot(QQuickWindow *window, const QString &path) {
    QSignalSpy frame(window, &QQuickWindow::frameSwapped);
    window->update();
    return frame.wait() && window->screen()->grabWindow(window->winId()).save(path);
}
