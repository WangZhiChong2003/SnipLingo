// Incremental Server-Sent-Events parser.
//
// Chunks may split anywhere: inside a line, between CR and LF, and inside a multi-byte UTF-8
// sequence. Payloads are handed over as raw bytes only after a complete line has been assembled, so
// the caller can decode UTF-8 safely.
#pragma once

#include <QByteArray>

#include <functional>

namespace provider {

class SseParser {
public:
    using DataHandler = std::function<void(const QByteArray &payload)>;

    explicit SseParser(DataHandler onData);

    void feed(const QByteArray &chunk);
    // Drops a trailing partial line: an unterminated event is not a complete event.
    void finish();
    void reset();

    [[nodiscard]] int dispatchedEvents() const { return m_dispatched; }

private:
    void processLine(const QByteArray &line);

    DataHandler m_onData;
    QByteArray m_buffer;
    QByteArray m_data;
    bool m_hasPendingData = false;
    int m_dispatched = 0;
};

}  // namespace provider
