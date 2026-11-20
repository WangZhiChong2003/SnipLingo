#pragma once

#include <QMenu>
#include <QSystemTrayIcon>

namespace ui {

class SystemTray : public QObject {
    Q_OBJECT

public:
    explicit SystemTray(const QIcon &icon);

signals:
    void openRequested();
    void quitRequested();

private:
    QMenu m_menu;
    QSystemTrayIcon m_icon;
};

}  // namespace ui
