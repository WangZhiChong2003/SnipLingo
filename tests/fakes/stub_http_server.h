// Minimal HTTP/1.1 stub for provider tests: canned status, headers and body chunks, optional
// silence. No real network: binds to 127.0.0.1 on an ephemeral port. Never used outside tests.
#pragma once

#include <QHash>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

class StubHttpServer : public QObject {
    Q_OBJECT
public:
    struct Response {
        QByteArray statusLine = QByteArrayLiteral("HTTP/1.1 200 OK");
        QByteArray contentType = QByteArrayLiteral("application/json");
        QList<QByteArray> chunks;
        int chunkDelayMs = 0;  // >0 spreads chunks over time (streaming realism)
        bool neverRespond = false;  // for timeout tests
    };

    explicit StubHttpServer(QObject *parent = nullptr)
        : QObject(parent) {
        connect(&m_server, &QTcpServer::newConnection, this, &StubHttpServer::onNewConnection);
        connect(&m_timer, &QTimer::timeout, this, &StubHttpServer::writeNextChunk);
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }

    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }

    void setResponse(const Response &response) { m_response = response; }

    // Answers the next requests in order (e.g. a token call followed by the real call).
    void setSequentialResponses(const QList<Response> &responses) { m_queue = responses; }

    [[nodiscard]] QByteArray requestBody() const { return m_body; }

    [[nodiscard]] QHash<QByteArray, QByteArray> requestHeaders() const { return m_headers; }

    [[nodiscard]] QByteArray requestLine() const { return m_requestLine; }

    [[nodiscard]] int requestCount() const { return m_requests; }

private:
    void onNewConnection() {
        m_socket = m_server.nextPendingConnection();
        // Per-connection state: without this reset the stub answers only its first request ever.
        m_buffer.clear();
        m_body.clear();
        m_headers.clear();
        m_requestLine.clear();
        m_responded = false;
        m_chunkIndex = 0;
        connect(m_socket, &QTcpSocket::readyRead, this, &StubHttpServer::onReadyRead);
        connect(m_socket, &QTcpSocket::disconnected, m_socket, &QObject::deleteLater);
    }

    void onReadyRead() {
        if (!m_socket)
            return;
        m_buffer.append(m_socket->readAll());
        if (m_responded)
            return;

        const int headerEnd = m_buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;

        const QByteArray head = m_buffer.left(headerEnd);
        const QList<QByteArray> lines = head.split('\n');
        m_requestLine = lines.value(0).trimmed();
        int contentLength = 0;
        for (const QByteArray &line : lines) {
            const int colon = line.indexOf(':');
            if (colon <= 0)
                continue;
            const QByteArray name = line.left(colon).trimmed().toLower();
            const QByteArray value = line.mid(colon + 1).trimmed();
            m_headers.insert(name, value);
            if (name == "content-length")
                contentLength = value.toInt();
        }
        if (m_buffer.size() < headerEnd + 4 + contentLength)
            return;

        m_body = m_buffer.mid(headerEnd + 4, contentLength);
        m_responded = true;
        ++m_requests;
        if (!m_queue.isEmpty())
            m_response = m_queue.takeFirst();
        if (m_response.neverRespond)
            return;

        QByteArray head0 = m_response.statusLine + "\r\n";
        head0 += "Content-Type: " + m_response.contentType + "\r\n";
        head0 += "Connection: close\r\n\r\n";
        m_socket->write(head0);
        m_chunkIndex = 0;
        writeNextChunk();
    }

    void writeNextChunk() {
        if (!m_socket)
            return;
        if (m_chunkIndex >= m_response.chunks.size()) {
            m_timer.stop();
            m_socket->disconnectFromHost();
            return;
        }
        m_socket->write(m_response.chunks.at(m_chunkIndex));
        m_socket->flush();
        ++m_chunkIndex;
        if (m_response.chunkDelayMs > 0 && m_chunkIndex < m_response.chunks.size())
            m_timer.start(m_response.chunkDelayMs);
        else
            writeNextChunk();
    }

    QTcpServer m_server;
    QTcpSocket *m_socket = nullptr;
    QTimer m_timer;
    Response m_response;
    QList<Response> m_queue;
    QByteArray m_buffer;
    QByteArray m_body;
    QByteArray m_requestLine;
    QHash<QByteArray, QByteArray> m_headers;
    int m_chunkIndex = 0;
    int m_requests = 0;
    bool m_responded = false;
};
