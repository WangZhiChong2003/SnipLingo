#pragma once

#include <QMenu>
#include <QSystemTrayIcon>

namespace ui {

class SystemTray : public QObject {
    Q_OBJECT

public:
    explicit SystemTray(const QIcon &icon);

    // Reapplies the menu labels after the interface language changes.
    void retranslate();

signals:
    void openRequested();
    void quitRequested();

private:
    QMenu m_menu;
    QSystemTrayIcon m_icon;
    QAction *m_openAction = nullptr;
    QAction *m_quitAction = nullptr;
};

}  // namespace ui
