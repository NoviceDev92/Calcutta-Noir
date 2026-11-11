#include "network_manager.h"
#include <QtGlobal>
#include <QNetworkDatagram>
#include <QDebug>
#include <algorithm>
#include <cstring>
#include <cerrno>

#if defined(Q_OS_UNIX) || defined(__unix__) || defined(__linux__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#define HAS_UNIX_SOCKETS 1
#endif

NetworkManager::NetworkManager(QObject *parent)
    : QObject(parent),
      udpSocket(new QUdpSocket(this)),
      tcpServer(new QTcpServer(this)),
      clientSocket(new QTcpSocket(this))
{
    connect(udpSocket, &QUdpSocket::readyRead, this, &NetworkManager::onUdpReadyRead);
    connect(tcpServer, &QTcpServer::newConnection, this, &NetworkManager::onNewTcpConnection);
    connect(clientSocket, &QTcpSocket::readyRead, this, &NetworkManager::onTcpReadyRead);
    connect(clientSocket, &QTcpSocket::connected, this, &NetworkManager::onTcpConnected);
    connect(clientSocket, &QTcpSocket::disconnected, this, &NetworkManager::onTcpDisconnected);

    connect(&discoveryTimer, &QTimer::timeout, this, &NetworkManager::onDiscoveryTimerTick);
    connect(&hostBroadcastTimer, &QTimer::timeout, this, &NetworkManager::onHostBroadcastTick);
}

NetworkManager::~NetworkManager()
{
    disconnectNetwork();
}

QString NetworkManager::getLocalIP() const
{
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &addr : addresses) {
        if (addr != QHostAddress::LocalHost && addr != QHostAddress::LocalHostIPv6 &&
            addr.protocol() == QAbstractSocket::IPv4Protocol) {
            QString str = addr.toString();
            // Exclude common virtual/docker interfaces if possible, otherwise first non-loopback IPv4
            if (!str.startsWith("127.") && !str.startsWith("169.254.")) {
                return str;
            }
        }
    }
    return "127.0.0.1";
}

bool NetworkManager::isConnected() const
{
    if (role == NetworkRole::HOST) {
        return tcpServer->isListening();
    } else if (role == NetworkRole::CLIENT) {
        return clientSocket->state() == QAbstractSocket::ConnectedState;
    }
    return false;
}

const NetPlayer* NetworkManager::getPlayer(int id) const
{
    for (const auto &p : players) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

bool NetworkManager::allPlayersReady() const
{
    if (players.empty()) return false;
    for (const auto &p : players) {
        if (!p.isReady) return false;
    }
    return true;
}

void NetworkManager::disconnectNetwork()
{
    discoveryTimer.stop();
    hostBroadcastTimer.stop();

    if (udpSocket && udpSocket->state() == QAbstractSocket::BoundState) {
        udpSocket->close();
    }

    if (tcpServer && tcpServer->isListening()) {
        tcpServer->close();
    }

    for (QTcpSocket *sock : clientSockets) {
        if (sock) {
            sock->disconnect(this);
            sock->close();
            sock->deleteLater();
        }
    }
    clientSockets.clear();
    socketToPlayerId.clear();
    socketBuffers.clear();

    if (clientSocket && clientSocket->state() != QAbstractSocket::UnconnectedState) {
        clientSocket->disconnectFromHost();
    }

    role = NetworkRole::NONE;
    players.clear();
    localPlayerId = -1;
    statusMessage = "DISCONNECTED";
}

bool NetworkManager::startHosting(const QString &code, const QString &hostName, int trackLevel, quint16 port)
{
    disconnectNetwork();

    role = NetworkRole::HOST;
    secretCode = code.trimmed().toUpper();
    localPlayerName = hostName.trimmed().isEmpty() ? "HOST" : hostName.trimmed();
    localPlayerId = 0;
    selectedTrack = trackLevel;
    nextPlayerId = 1;

    // Start TCP Server: try requested port and nearby ports
    bool listened = false;
    for (int offset = 0; offset < 50; ++offset) {
        quint16 p = port + offset;
#if defined(HAS_UNIX_SOCKETS)
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0) {
            int opt = 1;
            ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
            ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
            sockaddr_in addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(p);

            if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0 && ::listen(fd, 30) == 0) {
                listened = tcpServer->setSocketDescriptor(fd);
                if (listened) {
                    currentTcpPort = p;
                    break;
                } else {
                    ::close(fd);
                }
            } else {
                ::close(fd);
            }
        }
#endif
        if (!listened && tcpServer->listen(QHostAddress::AnyIPv4, p)) {
            currentTcpPort = p;
            listened = true;
            break;
        }
    }

    if (!listened) {
        statusMessage = "FAILED TO START TCP SERVER";
        emit statusChanged(statusMessage);
        return false;
    }

    // Start UDP socket for discovery
    if (!udpSocket->bind(QHostAddress::AnyIPv4, UDP_PORT, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        // Warning only, continue hosting
    }

    // Initialize Host Player
    NetPlayer hostPlayer;
    hostPlayer.id = 0;
    hostPlayer.name = localPlayerName;
    hostPlayer.characterId = 0;
    hostPlayer.isHost = true;
    hostPlayer.isReady = true;
    hostPlayer.kartColor = QColor(220, 20, 20);
    players.push_back(hostPlayer);

    statusMessage = QString("LOBBY OPEN [%1] - WAITING FOR RACERS...").arg(secretCode);
    emit statusChanged(statusMessage);
    emit lobbyUpdated();

    // Start host announcement broadcast
    hostBroadcastTimer.start(1200);

    return true;
}

bool NetworkManager::startJoining(const QString &code, const QString &playerName, const QString &manualHostIp, quint16 manualHostPort)
{
    disconnectNetwork();

    role = NetworkRole::CLIENT;
    secretCode = code.trimmed().toUpper();
    localPlayerName = playerName.trimmed().isEmpty() ? "PLAYER" : playerName.trimmed();
    manualIp = manualHostIp.trimmed();
    this->manualPort = (manualHostPort != 0) ? manualHostPort : TCP_PORT;
    discoveryAttempts = 0;

    // Bind UDP socket
    udpSocket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    statusMessage = QString("SEARCHING LAN FOR SECRET CODE \"%1\"...").arg(secretCode);
    emit statusChanged(statusMessage);

    if (!manualIp.isEmpty()) {
        // Direct IP connect attempt
        clientSocket->connectToHost(manualIp, this->manualPort);
    }

    // Start discovery broadcast loop
    sendDiscoveryBroadcast();
    discoveryTimer.start(600);

    return true;
}

void NetworkManager::sendDiscoveryBroadcast()
{
    if (role != NetworkRole::CLIENT) return;

    QJsonObject obj;
    obj["type"] = "DISCOVER_LOBBY";
    obj["code"] = secretCode;
    obj["name"] = localPlayerName;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    // Broadcast on 255.255.255.255
    udpSocket->writeDatagram(data, QHostAddress::Broadcast, UDP_PORT);

    // Also send on broadcast addresses of all active network interfaces
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.broadcast().isNull()) {
                udpSocket->writeDatagram(data, entry.broadcast(), UDP_PORT);
            }
        }
    }

    if (!manualIp.isEmpty()) {
        udpSocket->writeDatagram(data, QHostAddress(manualIp), UDP_PORT);
    }
}

void NetworkManager::onDiscoveryTimerTick()
{
    if (role != NetworkRole::CLIENT) {
        discoveryTimer.stop();
        return;
    }

    discoveryAttempts++;
    if (clientSocket->state() == QAbstractSocket::ConnectedState) {
        discoveryTimer.stop();
        return;
    }

    if (discoveryAttempts > 25) {
        statusMessage = QString("NO HOST FOUND WITH CODE \"%1\" (CHECK WI-FI/CODE)").arg(secretCode);
        emit statusChanged(statusMessage);
        return;
    }

    sendDiscoveryBroadcast();
}

void NetworkManager::onHostBroadcastTick()
{
    if (role != NetworkRole::HOST) {
        hostBroadcastTimer.stop();
        return;
    }

    QJsonObject obj;
    obj["type"] = "LOBBY_ADVERTISEMENT";
    obj["code"] = secretCode;
    obj["hostName"] = localPlayerName;
    obj["port"] = currentTcpPort;
    obj["numPlayers"] = static_cast<int>(players.size());
    obj["track"] = selectedTrack;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    udpSocket->writeDatagram(data, QHostAddress::Broadcast, UDP_PORT);

    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.broadcast().isNull()) {
                udpSocket->writeDatagram(data, entry.broadcast(), UDP_PORT);
            }
        }
    }
}

void NetworkManager::onUdpReadyRead()
{
    while (udpSocket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udpSocket->receiveDatagram();
        processUdpPacket(datagram.data(), datagram.senderAddress(), static_cast<quint16>(datagram.senderPort()));
    }
}

void NetworkManager::processUdpPacket(const QByteArray &data, const QHostAddress &sender, quint16 port)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    QJsonObject obj = doc.object();
    QString type = obj.value("type").toString();
    QString packetCode = obj.value("code").toString().trimmed().toUpper();

    if (role == NetworkRole::HOST && type == "DISCOVER_LOBBY") {
        if (packetCode == secretCode) {
            // Reply with advertisement
            QJsonObject reply;
            reply["type"] = "LOBBY_ADVERTISEMENT";
            reply["code"] = secretCode;
            reply["hostName"] = localPlayerName;
            reply["port"] = currentTcpPort;
            reply["numPlayers"] = static_cast<int>(players.size());
            reply["track"] = selectedTrack;
            QByteArray replyData = QJsonDocument(reply).toJson(QJsonDocument::Compact);

            udpSocket->writeDatagram(replyData, sender, port);
            udpSocket->writeDatagram(replyData, sender, UDP_PORT);
        }
    } else if (role == NetworkRole::CLIENT && type == "LOBBY_ADVERTISEMENT") {
        if (packetCode == secretCode) {
            if (clientSocket->state() == QAbstractSocket::UnconnectedState) {
                statusMessage = QString("FOUND HOST AT %1! CONNECTING...").arg(sender.toString());
                emit statusChanged(statusMessage);
                quint16 hostTcpPort = static_cast<quint16>(obj.value("port").toInt(TCP_PORT));
                clientSocket->connectToHost(sender, hostTcpPort);
            }
        }
    }
}

void NetworkManager::onNewTcpConnection()
{
    if (role != NetworkRole::HOST) return;

    while (tcpServer->hasPendingConnections()) {
        QTcpSocket *sock = tcpServer->nextPendingConnection();
        clientSockets.push_back(sock);
        socketBuffers[sock] = QByteArray();

        connect(sock, &QTcpSocket::readyRead, this, &NetworkManager::onTcpReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &NetworkManager::onTcpDisconnected);
    }
}

void NetworkManager::onTcpConnected()
{
    if (role == NetworkRole::CLIENT) {
        statusMessage = "CONNECTED TO HOST! JOINING LOBBY...";
        emit statusChanged(statusMessage);
        discoveryTimer.stop();

        // Send JOIN_REQ
        QJsonObject req;
        req["type"] = "JOIN_REQ";
        req["code"] = secretCode;
        req["name"] = localPlayerName;
        req["char"] = 0;
        sendTcpJson(clientSocket, req);
    }
}

void NetworkManager::onTcpDisconnected()
{
    QTcpSocket *sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;

    if (role == NetworkRole::HOST) {
        int leftPlayerId = socketToPlayerId.value(sock, -1);
        socketToPlayerId.remove(sock);
        socketBuffers.remove(sock);

        auto it = std::find(clientSockets.begin(), clientSockets.end(), sock);
        if (it != clientSockets.end()) {
            clientSockets.erase(it);
        }
        sock->deleteLater();

        if (leftPlayerId != -1) {
            players.erase(
                std::remove_if(players.begin(), players.end(),
                               [leftPlayerId](const NetPlayer &p) { return p.id == leftPlayerId; }),
                players.end());

            // Broadcast updated lobby
            QJsonObject update;
            update["type"] = "LOBBY_UPDATE";
            update["track"] = selectedTrack;
            update["players"] = serializePlayers();
            broadcastTcpJson(update);

            emit lobbyUpdated();
        }
    } else if (role == NetworkRole::CLIENT) {
        statusMessage = "LOST CONNECTION TO HOST";
        emit statusChanged(statusMessage);
        emit connectionLost("HOST DISCONNECTED");
    }
}

void NetworkManager::onTcpReadyRead()
{
    QTcpSocket *sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;

    QByteArray &buffer = socketBuffers[sock];
    buffer.append(sock->readAll());

    // Messages are delimited by newline \n
    while (true) {
        int newlineIdx = buffer.indexOf('\n');
        if (newlineIdx == -1) break;

        QByteArray line = buffer.left(newlineIdx).trimmed();
        buffer.remove(0, newlineIdx + 1);

        if (line.isEmpty()) continue;

        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isObject()) {
            processTcpPacket(sock, doc.object());
        }
    }
}

void NetworkManager::sendTcpJson(QTcpSocket *socket, const QJsonObject &json)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n";
    socket->write(data);
}

void NetworkManager::broadcastTcpJson(const QJsonObject &json, QTcpSocket *exclude)
{
    for (QTcpSocket *sock : clientSockets) {
        if (sock && sock != exclude && sock->state() == QAbstractSocket::ConnectedState) {
            sendTcpJson(sock, json);
        }
    }
}

void NetworkManager::processTcpPacket(QTcpSocket *socket, const QJsonObject &json)
{
    QString type = json.value("type").toString();

    if (role == NetworkRole::HOST) {
        if (type == "JOIN_REQ") {
            QString code = json.value("code").toString().trimmed().toUpper();
            if (code == secretCode) {
                int assignedId = nextPlayerId++;
                socketToPlayerId[socket] = assignedId;

                NetPlayer newPlayer;
                newPlayer.id = assignedId;
                newPlayer.name = json.value("name").toString("PLAYER");
                newPlayer.characterId = json.value("char").toInt(0);
                newPlayer.isReady = false;
                newPlayer.isHost = false;
                players.push_back(newPlayer);

                // Send JOIN_ACK to new player
                QJsonObject ack;
                ack["type"] = "JOIN_ACK";
                ack["id"] = assignedId;
                ack["track"] = selectedTrack;
                ack["players"] = serializePlayers();
                sendTcpJson(socket, ack);

                // Broadcast LOBBY_UPDATE to everyone else
                QJsonObject update;
                update["type"] = "LOBBY_UPDATE";
                update["track"] = selectedTrack;
                update["players"] = serializePlayers();
                broadcastTcpJson(update, socket);

                statusMessage = QString("%1 JOINED THE LOBBY!").arg(newPlayer.name);
                emit statusChanged(statusMessage);
                emit lobbyUpdated();
            } else {
                QJsonObject rej;
                rej["type"] = "JOIN_REJ";
                rej["reason"] = "INVALID SECRET CODE";
                sendTcpJson(socket, rej);
                socket->disconnectFromHost();
            }
        } else if (type == "SET_CHAR") {
            int pId = socketToPlayerId.value(socket, -1);
            int charId = json.value("char").toInt(0);
            for (auto &p : players) {
                if (p.id == pId) {
                    p.characterId = charId;
                    break;
                }
            }
            QJsonObject update;
            update["type"] = "LOBBY_UPDATE";
            update["track"] = selectedTrack;
            update["players"] = serializePlayers();
            broadcastTcpJson(update);
            emit lobbyUpdated();
        } else if (type == "SET_READY") {
            int pId = socketToPlayerId.value(socket, -1);
            bool ready = json.value("ready").toBool(false);
            for (auto &p : players) {
                if (p.id == pId) {
                    p.isReady = ready;
                    break;
                }
            }
            QJsonObject update;
            update["type"] = "LOBBY_UPDATE";
            update["track"] = selectedTrack;
            update["players"] = serializePlayers();
            broadcastTcpJson(update);
            emit lobbyUpdated();
        } else if (type == "KART_STATE") {
            int pId = json.value("id").toInt(-1);
            for (auto &p : players) {
                if (p.id == pId) {
                    p.x = json.value("x").toDouble(p.x);
                    p.z = json.value("z").toDouble(p.z);
                    p.angle = json.value("angle").toDouble(p.angle);
                    p.speed = json.value("speed").toDouble(p.speed);
                    p.currentLap = json.value("lap").toInt(p.currentLap);
                    p.raceFinished = json.value("finished").toBool(p.raceFinished);
                    p.finishTime = json.value("time").toDouble(p.finishTime);
                    p.isSpinning = json.value("spin").toBool(p.isSpinning);
                    p.isDrifting = json.value("drift").toBool(p.isDrifting);
                    p.heldItem = json.value("item").toInt(p.heldItem);
                    p.hull = json.value("hull").toDouble(p.hull);
                    p.progressMeters = json.value("prog").toDouble(p.progressMeters);
                    p.isEliminated = json.value("elim").toBool(p.isEliminated);
                    break;
                }
            }
            // Forward to all other clients
            broadcastTcpJson(json, socket);
            emit remoteKartUpdated(pId);
        } else if (type == "ITEM_EVENT") {
            int senderId = json.value("sender").toInt(-1);
            int itemType = json.value("item").toInt(0);
            double x = json.value("x").toDouble(0);
            double z = json.value("z").toDouble(0);
            double angle = json.value("angle").toDouble(0);

            // Forward to all other clients
            broadcastTcpJson(json, socket);
            emit remoteItemTriggered(senderId, itemType, x, z, angle);
        } else if (type == "PLAYER_FINISHED") {
            int pId = json.value("id").toInt(-1);
            double fTime = json.value("time").toDouble(0.0);
            for (auto &p : players) {
                if (p.id == pId) {
                    p.raceFinished = true;
                    p.finishTime = fTime;
                    break;
                }
            }
            updatePlacements();

            int placement = 1;
            for (const auto &p : players) {
                if (p.id == pId) {
                    placement = p.placement;
                    break;
                }
            }

            QJsonObject finishBcast;
            finishBcast["type"] = "PLAYER_FINISHED";
            finishBcast["id"] = pId;
            finishBcast["time"] = fTime;
            finishBcast["placement"] = placement;
            broadcastTcpJson(finishBcast);

            emit remotePlayerFinished(pId, fTime, placement);

            bool allDone = true;
            for (const auto &p : players) {
                if (!p.raceFinished) { allDone = false; break; }
            }
            if (allDone) {
                emit allPlayersFinished();
            }
        } else if (type == "CP_READY") {
            int pId = json.value("id").toInt(-1);
            int cpIdx = json.value("cp").toInt(0);
            bool ready = json.value("ready").toBool(false);
            for (auto &p : players) {
                if (p.id == pId) {
                    p.atCheckpoint = true;
                    p.checkpointIdx = cpIdx;
                    p.checkpointReady = ready;
                    break;
                }
            }
            // Broadcast checkpoint sync state to everyone
            QJsonObject syncMsg;
            syncMsg["type"] = "CP_SYNC";
            syncMsg["cp"] = cpIdx;
            syncMsg["players"] = serializePlayers();
            broadcastTcpJson(syncMsg);
            emit checkpointLobbyUpdated();

            // Check if all surviving (non-eliminated) players are ready at this checkpoint
            bool allReady = !players.empty();
            for (const auto &p : players) {
                if (!p.isEliminated && (!p.checkpointReady || p.checkpointIdx != cpIdx)) {
                    allReady = false;
                    break;
                }
            }
            if (allReady) {
                for (auto &p : players) {
                    p.atCheckpoint = false;
                    p.checkpointReady = false;
                }
                QJsonObject goMsg;
                goMsg["type"] = "CP_ROLLOUT";
                goMsg["cp"] = cpIdx;
                broadcastTcpJson(goMsg);
                emit checkpointRolloutTriggered(cpIdx);
            }
        } else if (type == "PLAYER_ELIMINATED") {
            int pId = json.value("id").toInt(-1);
            for (auto &p : players) {
                if (p.id == pId) {
                    p.isEliminated = true;
                    p.raceFinished = true;
                    p.finishTime = 9999.0;
                    break;
                }
            }
            broadcastTcpJson(json);
            emit remoteKartUpdated(pId);
            emit checkpointLobbyUpdated();
        }
    } else if (role == NetworkRole::CLIENT) {
        if (type == "JOIN_ACK") {
            localPlayerId = json.value("id").toInt(1);
            selectedTrack = json.value("track").toInt(0);
            deserializePlayers(json.value("players").toArray());

            statusMessage = "CONNECTED TO LOBBY!";
            emit statusChanged(statusMessage);
            emit lobbyUpdated();
        } else if (type == "JOIN_REJ") {
            QString reason = json.value("reason").toString("REJECTED");
            statusMessage = "JOIN FAILED: " + reason;
            emit statusChanged(statusMessage);
            clientSocket->disconnectFromHost();
        } else if (type == "LOBBY_UPDATE") {
            selectedTrack = json.value("track").toInt(selectedTrack);
            deserializePlayers(json.value("players").toArray());
            emit lobbyUpdated();
        } else if (type == "RACE_START") {
            selectedTrack = json.value("track").toInt(selectedTrack);
            deserializePlayers(json.value("players").toArray());
            emit raceStartTriggered(selectedTrack);
        } else if (type == "KART_STATE") {
            int pId = json.value("id").toInt(-1);
            if (pId != localPlayerId) {
                for (auto &p : players) {
                    if (p.id == pId) {
                        p.x = json.value("x").toDouble(p.x);
                        p.z = json.value("z").toDouble(p.z);
                        p.angle = json.value("angle").toDouble(p.angle);
                        p.speed = json.value("speed").toDouble(p.speed);
                        p.currentLap = json.value("lap").toInt(p.currentLap);
                        p.raceFinished = json.value("finished").toBool(p.raceFinished);
                        p.finishTime = json.value("time").toDouble(p.finishTime);
                        p.isSpinning = json.value("spin").toBool(p.isSpinning);
                        p.isDrifting = json.value("drift").toBool(p.isDrifting);
                        p.heldItem = json.value("item").toInt(p.heldItem);
                        p.hull = json.value("hull").toDouble(p.hull);
                        p.progressMeters = json.value("prog").toDouble(p.progressMeters);
                        p.isEliminated = json.value("elim").toBool(p.isEliminated);
                        break;
                    }
                }
                emit remoteKartUpdated(pId);
            }
        } else if (type == "ITEM_EVENT") {
            int senderId = json.value("sender").toInt(-1);
            if (senderId != localPlayerId) {
                int itemType = json.value("item").toInt(0);
                double x = json.value("x").toDouble(0);
                double z = json.value("z").toDouble(0);
                double angle = json.value("angle").toDouble(0);
                emit remoteItemTriggered(senderId, itemType, x, z, angle);
            }
        } else if (type == "PLAYER_FINISHED") {
            int pId = json.value("id").toInt(-1);
            double fTime = json.value("time").toDouble(0.0);
            int placement = json.value("placement").toInt(1);
            for (auto &p : players) {
                if (p.id == pId) {
                    p.raceFinished = true;
                    p.finishTime = fTime;
                    p.placement = placement;
                    break;
                }
            }
            emit remotePlayerFinished(pId, fTime, placement);

            bool allDone = true;
            for (const auto &p : players) {
                if (!p.raceFinished) { allDone = false; break; }
            }
            if (allDone) {
                emit allPlayersFinished();
            }
        } else if (type == "RETURN_TO_LOBBY") {
            for (auto &p : players) {
                p.raceFinished = false;
                p.finishTime = 0.0;
                p.isReady = false;
                p.isEliminated = false;
                p.atCheckpoint = false;
                p.checkpointReady = false;
            }
            emit returnedToLobby();
        } else if (type == "CP_SYNC") {
            deserializePlayers(json.value("players").toArray());
            emit checkpointLobbyUpdated();
        } else if (type == "CP_ROLLOUT") {
            int cpIdx = json.value("cp").toInt(0);
            for (auto &p : players) {
                p.atCheckpoint = false;
                p.checkpointReady = false;
            }
            emit checkpointRolloutTriggered(cpIdx);
        } else if (type == "PLAYER_ELIMINATED") {
            int pId = json.value("id").toInt(-1);
            for (auto &p : players) {
                if (p.id == pId) {
                    p.isEliminated = true;
                    p.raceFinished = true;
                    p.finishTime = 9999.0;
                    break;
                }
            }
            emit remoteKartUpdated(pId);
            emit checkpointLobbyUpdated();
        }
    }
}

QJsonArray NetworkManager::serializePlayers() const
{
    QJsonArray arr;
    for (const auto &p : players) {
        QJsonObject obj;
        obj["id"] = p.id;
        obj["name"] = p.name;
        obj["char"] = p.characterId;
        obj["ready"] = p.isReady;
        obj["isHost"] = p.isHost;
        obj["finished"] = p.raceFinished;
        obj["time"] = p.finishTime;
        obj["placement"] = p.placement;
        obj["elim"] = p.isEliminated;
        obj["atCp"] = p.atCheckpoint;
        obj["cpIdx"] = p.checkpointIdx;
        obj["cpReady"] = p.checkpointReady;
        arr.append(obj);
    }
    return arr;
}

void NetworkManager::deserializePlayers(const QJsonArray &arr)
{
    std::vector<NetPlayer> updated;
    for (const auto &val : arr) {
        QJsonObject obj = val.toObject();
        NetPlayer p;
        p.id = obj.value("id").toInt();
        p.name = obj.value("name").toString();
        p.characterId = obj.value("char").toInt();
        p.isReady = obj.value("ready").toBool();
        p.isHost = obj.value("isHost").toBool();
        p.raceFinished = obj.value("finished").toBool();
        p.finishTime = obj.value("time").toDouble();
        p.placement = obj.value("placement").toInt();
        p.isEliminated = obj.value("elim").toBool();
        p.atCheckpoint = obj.value("atCp").toBool();
        p.checkpointIdx = obj.value("cpIdx").toInt();
        p.checkpointReady = obj.value("cpReady").toBool();

        // Preserve coordinates from existing entry if present
        for (const auto &existing : players) {
            if (existing.id == p.id) {
                p.x = existing.x;
                p.z = existing.z;
                p.angle = existing.angle;
                p.speed = existing.speed;
                p.currentLap = existing.currentLap;
                p.isSpinning = existing.isSpinning;
                p.isDrifting = existing.isDrifting;
                p.heldItem = existing.heldItem;
                break;
            }
        }
        updated.push_back(p);
    }
    players = updated;
}

void NetworkManager::setReady(bool ready)
{
    if (role == NetworkRole::HOST) {
        for (auto &p : players) {
            if (p.id == 0) {
                p.isReady = ready;
                break;
            }
        }
        QJsonObject update;
        update["type"] = "LOBBY_UPDATE";
        update["track"] = selectedTrack;
        update["players"] = serializePlayers();
        broadcastTcpJson(update);
        emit lobbyUpdated();
    } else if (role == NetworkRole::CLIENT) {
        QJsonObject obj;
        obj["type"] = "SET_READY";
        obj["ready"] = ready;
        sendTcpJson(clientSocket, obj);
    }
}

void NetworkManager::setCharacter(int charId)
{
    if (role == NetworkRole::HOST) {
        for (auto &p : players) {
            if (p.id == 0) {
                p.characterId = charId;
                break;
            }
        }
        QJsonObject update;
        update["type"] = "LOBBY_UPDATE";
        update["track"] = selectedTrack;
        update["players"] = serializePlayers();
        broadcastTcpJson(update);
        emit lobbyUpdated();
    } else if (role == NetworkRole::CLIENT) {
        QJsonObject obj;
        obj["type"] = "SET_CHAR";
        obj["char"] = charId;
        sendTcpJson(clientSocket, obj);
    }
}

void NetworkManager::setTrackLevel(int trackLevel)
{
    if (role == NetworkRole::HOST) {
        selectedTrack = trackLevel;
        QJsonObject update;
        update["type"] = "LOBBY_UPDATE";
        update["track"] = selectedTrack;
        update["players"] = serializePlayers();
        broadcastTcpJson(update);
        emit lobbyUpdated();
    }
}

void NetworkManager::startRace()
{
    if (role == NetworkRole::HOST) {
        for (auto &p : players) {
            p.raceFinished = false;
            p.finishTime = 0.0;
            p.placement = 0;
            p.currentLap = 1;
        }

        QJsonObject startMsg;
        startMsg["type"] = "RACE_START";
        startMsg["track"] = selectedTrack;
        startMsg["players"] = serializePlayers();
        broadcastTcpJson(startMsg);

        emit raceStartTriggered(selectedTrack);
    }
}

void NetworkManager::broadcastLocalKartState(double x, double z, double angle, double speed, int lap, bool finished, double finishTime, bool spinning, bool drifting, int heldItem,
                                             double hull, double progressMeters, bool isEliminated)
{
    QJsonObject msg;
    msg["type"] = "KART_STATE";
    msg["id"] = localPlayerId;
    msg["x"] = x;
    msg["z"] = z;
    msg["angle"] = angle;
    msg["speed"] = speed;
    msg["lap"] = lap;
    msg["finished"] = finished;
    msg["time"] = finishTime;
    msg["spin"] = spinning;
    msg["drift"] = drifting;
    msg["item"] = heldItem;
    msg["hull"] = hull;
    msg["prog"] = progressMeters;
    msg["elim"] = isEliminated;

    if (role == NetworkRole::HOST) {
        // Update player 0 locally and forward to clients
        for (auto &p : players) {
            if (p.id == 0) {
                p.x = x;
                p.z = z;
                p.angle = angle;
                p.speed = speed;
                p.currentLap = lap;
                p.raceFinished = finished;
                p.finishTime = finishTime;
                p.isSpinning = spinning;
                p.isDrifting = drifting;
                p.heldItem = heldItem;
                p.hull = hull;
                p.progressMeters = progressMeters;
                p.isEliminated = isEliminated;
                break;
            }
        }
        broadcastTcpJson(msg);
    } else if (role == NetworkRole::CLIENT) {
        sendTcpJson(clientSocket, msg);
    }
}

void NetworkManager::sendItemEvent(int itemType, double x, double z, double angle)
{
    QJsonObject msg;
    msg["type"] = "ITEM_EVENT";
    msg["sender"] = localPlayerId;
    msg["item"] = itemType;
    msg["x"] = x;
    msg["z"] = z;
    msg["angle"] = angle;

    if (role == NetworkRole::HOST) {
        broadcastTcpJson(msg);
    } else if (role == NetworkRole::CLIENT) {
        sendTcpJson(clientSocket, msg);
    }
}

void NetworkManager::notifyLocalFinish(double finishTime)
{
    if (role == NetworkRole::HOST) {
        for (auto &p : players) {
            if (p.id == 0) {
                p.raceFinished = true;
                p.finishTime = finishTime;
                break;
            }
        }
        updatePlacements();

        int placement = 1;
        for (const auto &p : players) {
            if (p.id == 0) {
                placement = p.placement;
                break;
            }
        }

        QJsonObject finishBcast;
        finishBcast["type"] = "PLAYER_FINISHED";
        finishBcast["id"] = 0;
        finishBcast["time"] = finishTime;
        finishBcast["placement"] = placement;
        broadcastTcpJson(finishBcast);

        emit remotePlayerFinished(0, finishTime, placement);

        bool allDone = true;
        for (const auto &p : players) {
            if (!p.raceFinished) { allDone = false; break; }
        }
        if (allDone) {
            emit allPlayersFinished();
        }
    } else if (role == NetworkRole::CLIENT) {
        QJsonObject msg;
        msg["type"] = "PLAYER_FINISHED";
        msg["id"] = localPlayerId;
        msg["time"] = finishTime;
        sendTcpJson(clientSocket, msg);
    }
}

void NetworkManager::sendCheckpointReady(int cpIndex, bool ready)
{
    if (role == NetworkRole::HOST) {
        for (auto &p : players) {
            if (p.id == 0) {
                p.atCheckpoint = true;
                p.checkpointIdx = cpIndex;
                p.checkpointReady = ready;
                break;
            }
        }
        QJsonObject syncMsg;
        syncMsg["type"] = "CP_SYNC";
        syncMsg["cp"] = cpIndex;
        syncMsg["players"] = serializePlayers();
        broadcastTcpJson(syncMsg);
        emit checkpointLobbyUpdated();

        bool allReady = !players.empty();
        for (const auto &p : players) {
            if (!p.isEliminated && (!p.checkpointReady || p.checkpointIdx != cpIndex)) {
                allReady = false;
                break;
            }
        }
        if (allReady) {
            for (auto &p : players) {
                p.atCheckpoint = false;
                p.checkpointReady = false;
            }
            QJsonObject goMsg;
            goMsg["type"] = "CP_ROLLOUT";
            goMsg["cp"] = cpIndex;
            broadcastTcpJson(goMsg);
            emit checkpointRolloutTriggered(cpIndex);
        }
    } else if (role == NetworkRole::CLIENT) {
        QJsonObject msg;
        msg["type"] = "CP_READY";
        msg["id"] = localPlayerId;
        msg["cp"] = cpIndex;
        msg["ready"] = ready;
        sendTcpJson(clientSocket, msg);
    }
}

void NetworkManager::sendEliminated()
{
    if (role == NetworkRole::HOST) {
        for (auto &p : players) {
            if (p.id == 0) {
                p.isEliminated = true;
                p.raceFinished = true;
                p.finishTime = 9999.0;
                break;
            }
        }
        QJsonObject msg;
        msg["type"] = "PLAYER_ELIMINATED";
        msg["id"] = 0;
        broadcastTcpJson(msg);
        emit checkpointLobbyUpdated();
    } else if (role == NetworkRole::CLIENT) {
        QJsonObject msg;
        msg["type"] = "PLAYER_ELIMINATED";
        msg["id"] = localPlayerId;
        sendTcpJson(clientSocket, msg);
    }
}

void NetworkManager::returnToLobby()
{
    if (role == NetworkRole::HOST) {
        for (auto &p : players) {
            p.raceFinished = false;
            p.finishTime = 0.0;
            p.isReady = false;
        }

        QJsonObject msg;
        msg["type"] = "RETURN_TO_LOBBY";
        broadcastTcpJson(msg);

        emit returnedToLobby();
    }
}

void NetworkManager::updatePlacements()
{
    std::vector<NetPlayer*> finished;
    for (auto &p : players) {
        if (p.raceFinished) {
            finished.push_back(&p);
        }
    }

    std::sort(finished.begin(), finished.end(), [](const NetPlayer *a, const NetPlayer *b) {
        return a->finishTime < b->finishTime;
    });

    for (size_t i = 0; i < finished.size(); ++i) {
        finished[i]->placement = static_cast<int>(i + 1);
    }
}
