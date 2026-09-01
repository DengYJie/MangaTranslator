#pragma once

#include <QByteArray>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QPair>
#include <QQueue>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QVector>

#include <functional>

namespace manga::test {

struct QueuedHttpResponse {
    int status{200};
    QByteArray body;
    QList<QPair<QByteArray, QByteArray>> headers;
    bool silentTimeout{false};
    bool closeConnection{true};
};

struct RecordedHttpRequest {
    QByteArray raw;
    QString method;
    QString path;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray body;

    [[nodiscard]] QByteArray header(const QByteArray& name) const {
        for (const auto& [k, v] : headers) {
            if (k.compare(name, Qt::CaseInsensitive) == 0) {
                return v;
            }
        }
        return {};
    }

    [[nodiscard]] QByteArray authorizationHeader() const {
        return header("Authorization");
    }
};

/// Configurable loopback HTTP server for network adapter and translation scenario testing.
/// Supports enqueued responses (status, headers, body), recording requests, and silent timeout injection.
class LoopbackHttpServer : public QObject {
public:
    explicit LoopbackHttpServer(QObject* parent = nullptr) : QObject(parent) {
        connect(&m_server, &QTcpServer::newConnection, this, &LoopbackHttpServer::onNewConnection);
    }

    ~LoopbackHttpServer() override {
        close();
    }

    Q_DISABLE_COPY_MOVE(LoopbackHttpServer)

    bool listen() {
        return m_server.listen(QHostAddress(QHostAddress::LocalHost), 0);
    }

    void close() {
        m_server.close();
        for (QTcpSocket* socket : m_activeSockets) {
            if (socket != nullptr) {
                socket->disconnect(this);
                socket->abort();
                socket->deleteLater();
            }
        }
        m_activeSockets.clear();
        m_socketBuffers.clear();
    }

    [[nodiscard]] quint16 port() const {
        return m_server.serverPort();
    }

    [[nodiscard]] QUrl endpoint(const QString& path = QStringLiteral("/v1/chat/completions")) const {
        return QUrl(QStringLiteral("http://%1:%2%3")
                        .arg(m_server.serverAddress().toString())
                        .arg(m_server.serverPort())
                        .arg(path));
    }

    void
    enqueueResponse(int status, const QByteArray& body = {}, const QList<QPair<QByteArray, QByteArray>>& headers = {}) {
        m_responses.enqueue(QueuedHttpResponse{status, body, headers, false, true});
    }

    void enqueueResponse(const QueuedHttpResponse& response) {
        m_responses.enqueue(response);
    }

    void enqueueSilentTimeout() {
        m_responses.enqueue(QueuedHttpResponse{0, {}, {}, true, false});
    }

    void respondWith(const QByteArray& body) {
        m_responses.clear();
        enqueueResponse(200, body);
    }

    void setDeferResponse(bool defer) {
        m_deferResponse = defer;
    }

    void clear() {
        m_responses.clear();
        m_recordedRequests.clear();
    }

    [[nodiscard]] const QVector<RecordedHttpRequest>& recordedRequests() const {
        return m_recordedRequests;
    }

    [[nodiscard]] int requestCount() const {
        return static_cast<int>(m_recordedRequests.size());
    }

    [[nodiscard]] RecordedHttpRequest lastRequest() const {
        return m_recordedRequests.isEmpty() ? RecordedHttpRequest{} : m_recordedRequests.last();
    }

    [[nodiscard]] bool waitForRequests(int count, int timeoutMs = 10000) {
        if (m_recordedRequests.size() >= count) {
            return true;
        }
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        m_waitingLoop = &loop;
        m_targetRequestCount = count;
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(timeoutMs);
        loop.exec();
        m_waitingLoop = nullptr;
        m_targetRequestCount = 0;
        return m_recordedRequests.size() >= count;
    }

    [[nodiscard]] bool waitForRequest(int timeoutMs = 10000) {
        return waitForRequests(1, timeoutMs);
    }

    void sendDeferredResponse() {
        if (m_activeSocket != nullptr && m_activeSocket->isOpen()) {
            sendNextResponse(m_activeSocket);
        }
    }

    std::function<void(const RecordedHttpRequest&)> onRequestReceived;

private:
    static QByteArray statusReason(int status) {
        switch (status) {
            case 200:
                return "OK";
            case 201:
                return "Created";
            case 204:
                return "No Content";
            case 400:
                return "Bad Request";
            case 401:
                return "Unauthorized";
            case 403:
                return "Forbidden";
            case 404:
                return "Not Found";
            case 429:
                return "Too Many Requests";
            case 500:
                return "Internal Server Error";
            case 502:
                return "Bad Gateway";
            case 503:
                return "Service Unavailable";
            case 504:
                return "Gateway Timeout";
            default:
                if (status >= 200 && status < 300)
                    return "OK";
                if (status >= 400 && status < 500)
                    return "Client Error";
                return "Server Error";
        }
    }

    void sendNextResponse(QTcpSocket* socket) {
        if (socket == nullptr || !socket->isOpen()) {
            return;
        }
        QueuedHttpResponse response{200, {}, {}, false, true};
        if (!m_responses.isEmpty()) {
            response = m_responses.dequeue();
        }
        if (response.silentTimeout) {
            // Keep the socket open without writing anything to trigger timeout.
            return;
        }

        QByteArray data;
        data.append("HTTP/1.1 ");
        data.append(QByteArray::number(response.status));
        data.append(" ");
        data.append(statusReason(response.status));
        data.append("\r\n");

        data.append("Content-Length: ");
        data.append(QByteArray::number(response.body.size()));
        data.append("\r\n");

        if (response.closeConnection) {
            data.append("Connection: close\r\n");
        }

        bool hasContentType = false;
        for (const auto& [key, value] : response.headers) {
            if (key.compare("Content-Type", Qt::CaseInsensitive) == 0) {
                hasContentType = true;
            }
            data.append(key);
            data.append(": ");
            data.append(value);
            data.append("\r\n");
        }
        if (!hasContentType && !response.body.isEmpty()) {
            data.append("Content-Type: application/json\r\n");
        }

        data.append("\r\n");
        data.append(response.body);

        socket->write(data);
        socket->flush();
        if (response.closeConnection) {
            socket->disconnectFromHost();
        }
    }

    void parseRequest(const QByteArray& raw) {
        RecordedHttpRequest req;
        req.raw = raw;

        const int headerEnd = raw.indexOf("\r\n\r\n");
        const QByteArray headerBlock = (headerEnd >= 0) ? raw.left(headerEnd) : raw;
        const QList<QByteArray> lines = headerBlock.split('\n');

        if (!lines.isEmpty()) {
            const QByteArray firstLine = lines.first().trimmed();
            const QList<QByteArray> parts = firstLine.split(' ');
            if (parts.size() >= 1) {
                req.method = QString::fromUtf8(parts[0]);
            }
            if (parts.size() >= 2) {
                req.path = QString::fromUtf8(parts[1]);
            }
        }

        for (qsizetype i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines[i].trimmed();
            const int colon = line.indexOf(':');
            if (colon > 0) {
                const QByteArray key = line.left(colon).trimmed();
                const QByteArray val = line.mid(colon + 1).trimmed();
                req.headers.append(qMakePair(key, val));
            }
        }

        if (headerEnd >= 0) {
            req.body = raw.mid(headerEnd + 4);
        }

        m_recordedRequests.append(req);
        if (onRequestReceived) {
            onRequestReceived(req);
        }

        if (m_waitingLoop != nullptr && m_recordedRequests.size() >= m_targetRequestCount) {
            m_waitingLoop->quit();
        }
    }

    void onNewConnection() {
        QTcpSocket* socket = m_server.nextPendingConnection();
        if (socket == nullptr) {
            return;
        }
        m_activeSockets.append(socket);
        m_activeSocket = socket;

        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            QByteArray& buffer = m_socketBuffers[socket];
            buffer.append(socket->readAll());

            const int headerEnd = buffer.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
            }

            int contentLength = 0;
            const QByteArray header = buffer.left(headerEnd);
            for (const QByteArray& rawLine : header.split('\n')) {
                const QByteArray line = rawLine.trimmed();
                if (line.toLower().startsWith("content-length:")) {
                    contentLength = line.mid(15).trimmed().toInt();
                }
            }

            const int bodyStart = headerEnd + 4;
            if (buffer.size() - bodyStart < contentLength) {
                return;
            }

            parseRequest(buffer);

            if (!m_deferResponse) {
                sendNextResponse(socket);
            }
        });

        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            if (m_activeSocket == socket) {
                m_activeSocket = nullptr;
            }
            m_activeSockets.removeAll(socket);
            m_socketBuffers.remove(socket);
            socket->deleteLater();
        });
    }

    QTcpServer m_server;
    QTcpSocket* m_activeSocket{nullptr};
    QList<QTcpSocket*> m_activeSockets;
    QHash<QTcpSocket*, QByteArray> m_socketBuffers;
    QQueue<QueuedHttpResponse> m_responses;
    QVector<RecordedHttpRequest> m_recordedRequests;
    QEventLoop* m_waitingLoop{nullptr};
    int m_targetRequestCount{0};
    bool m_deferResponse{false};
};

} // namespace manga::test
