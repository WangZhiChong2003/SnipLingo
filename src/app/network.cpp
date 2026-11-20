#include "app/network.h"

#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QSslConfiguration>
#include <QUrl>

namespace app {
namespace {

QNetworkProxy proxyFromUrl(const QString &proxyUrl) {
    const QUrl url(proxyUrl);
    const QNetworkProxy::ProxyType type = url.scheme() == QLatin1String("socks5")
        ? QNetworkProxy::Socks5Proxy
        : QNetworkProxy::HttpProxy;
    return QNetworkProxy(
        type, url.host(), static_cast<quint16>(url.port(8080)), url.userName(), url.password());
}

}  // namespace

void applyNetworkSettings(const settings::Settings &settings) {
    // Negotiate the highest mutually supported version without a separate connection or request.
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setProtocol(QSsl::TlsV1_2OrLater);
    QSslConfiguration::setDefaultConfiguration(ssl);
    if (settings.network.proxy == QLatin1String("none")) {
        QNetworkProxyFactory::setUseSystemConfiguration(false);
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        return;
    }
    if (settings.network.proxy == QLatin1String("manual")) {
        QNetworkProxyFactory::setUseSystemConfiguration(false);
        QNetworkProxy::setApplicationProxy(proxyFromUrl(settings.network.proxyUrl));
        return;
    }
    // "system": on Unix Qt reads http_proxy/https_proxy/all_proxy/no_proxy from the environment.
    QNetworkProxyFactory::setUseSystemConfiguration(true);
}

QString describeProxy() {
    const QNetworkProxy proxy = QNetworkProxy::applicationProxy();
    if (proxy.type() == QNetworkProxy::DefaultProxy) {
        const QStringList variables {QStringLiteral("https_proxy"), QStringLiteral("HTTPS_PROXY"),
            QStringLiteral("all_proxy"), QStringLiteral("ALL_PROXY"), QStringLiteral("http_proxy")};
        for (const QString &name : variables) {
            const QString value = qEnvironmentVariable(name.toUtf8().constData());
            if (!value.isEmpty())
                return QStringLiteral("system (%1=%2)").arg(name, value);
        }
        return QStringLiteral("system (no proxy variable set; connecting directly)");
    }
    if (proxy.type() == QNetworkProxy::NoProxy)
        return QStringLiteral("none (connecting directly)");
    return QStringLiteral("%1 %2:%3")
        .arg(proxy.type() == QNetworkProxy::Socks5Proxy ? QStringLiteral("socks5")
                                                        : QStringLiteral("http"))
        .arg(proxy.hostName())
        .arg(proxy.port());
}

}  // namespace app
