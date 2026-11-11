#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <QObject>
#include <QUdpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QTimer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QString>
#include <QColor>
#include <vector>
#include <map>

enum class NetworkRole {
    NONE,
    HOST,
    CLIENT
};

struct NetPlayer {
    int id = 0;
    QString name = "PLAYER";
    int characterId = 0;
    bool isReady = false;
    bool isHost = false;
    bool raceFinished = false;
    double finishTime = 0.0;
    int placement = 0;
    QColor kartColor = QColor(220, 20, 20);

    // In-race live state
    double x = 470.0;
    double z = 200.0;
    double angle = 0.0;
    double speed = 0.0;
    int currentLap = 1;
    bool isSpinning = false;
    bool isDrifting = false;
    int heldItem = 0;
    double hull = 100.0;
    double progressMeters = 0.0;
    bool isEliminated = false;
    bool atCheckpoint = false;
    int checkpointIdx = 0;
    bool checkpointReady = false;
};

class NetworkManager : public QObject
{
    Q_OBJECT
public:
    explicit NetworkManager(QObject *parent = nullptr);
    ~NetworkManager() override;

    static constexpr quint16 UDP_PORT = 45454;
    static constexpr quint16 TCP_PORT = 45455;

    // Lobby Actions
    bool startHosting(const QString &secretCode, const QString &hostName, int trackLevel = 0, quint16 port = TCP_PORT);
    bool startJoining(const QString &secretCode, const QString &playerName, const QString &manualHostIp = "", quint16 manualPort = 0);
    void disconnectNetwork();

    // In-Lobby Actions
    void setReady(bool ready);
    void setCharacter(int charId);
    void setTrackLevel(int trackLevel);
    void startRace();

    // In-Race Actions
    void broadcastLocalKartState(double x, double z, double angle, double speed, int lap, bool finished, double finishTime, bool spinning, bool drifting, int heldItem,
                                 double hull = 100.0, double progressMeters = 0.0, bool isEliminated = false);
    void sendItemEvent(int itemType, double x, double z, double angle);
    void notifyLocalFinish(double finishTime);
    void sendCheckpointReady(int cpIndex, bool ready);
    void sendEliminated();
    void returnToLobby();

    // Queries
    NetworkRole getRole() const { return role; }
    bool isHost() const { return role == NetworkRole::HOST; }
    bool isConnected() const;
    int getLocalPlayerId() const { return localPlayerId; }
    QString getSecretCode() const { return secretCode; }
    QString getLocalPlayerName() const { return localPlayerName; }
    const std::vector<NetPlayer>& getPlayers() const { return players; }
    const NetPlayer* getPlayer(int id) const;
    int getSelectedTrack() const { return selectedTrack; }
    QString getLocalIP() const;
    QString getStatusMessage() const { return statusMessage; }
    bool allPlayersReady() const;
    quint16 getServerPort() const { return currentTcpPort; }

signals:
    void statusChanged(const QString &status);
    void lobbyUpdated();
    void raceStartTriggered(int trackLevel);
    void remoteKartUpdated(int playerId);
    void remoteItemTriggered(int senderId, int itemType, double x, double z, double angle);
    void remotePlayerFinished(int playerId, double finishTime, int placement);
    void allPlayersFinished();
    void returnedToLobby();
    void checkpointLobbyUpdated();
    void checkpointRolloutTriggered(int cpIndex);
    void connectionLost(const QString &reason);

private slots:
    void onUdpReadyRead();
    void onNewTcpConnection();
    void onTcpReadyRead();
    void onTcpDisconnected();
    void onTcpConnected();
    void onDiscoveryTimerTick();
    void onHostBroadcastTick();

private:
    NetworkRole role = NetworkRole::NONE;
    QString secretCode;
    QString localPlayerName = "PLAYER 1";
    int localPlayerId = 0;
    int selectedTrack = 0;
    QString statusMessage;

    // UDP Discovery
    QUdpSocket *udpSocket = nullptr;
    QTimer discoveryTimer;
    QTimer hostBroadcastTimer;
    int discoveryAttempts = 0;
    QString manualIp;
    quint16 manualPort = 0;

    // TCP Host
    QTcpServer *tcpServer = nullptr;
    quint16 currentTcpPort = TCP_PORT;
    std::vector<QTcpSocket*> clientSockets;
    QMap<QTcpSocket*, int> socketToPlayerId;
    int nextPlayerId = 1;

    // TCP Client
    QTcpSocket *clientSocket = nullptr;

    // Sockets data buffer for streaming TCP
    QMap<QTcpSocket*, QByteArray> socketBuffers;

    // Player List
    std::vector<NetPlayer> players;

    // Helper functions
    void sendTcpJson(QTcpSocket *socket, const QJsonObject &json);
    void broadcastTcpJson(const QJsonObject &json, QTcpSocket *exclude = nullptr);
    void processTcpPacket(QTcpSocket *socket, const QJsonObject &json);
    void processUdpPacket(const QByteArray &data, const QHostAddress &sender, quint16 port);
    void updatePlacements();
    QJsonArray serializePlayers() const;
    void deserializePlayers(const QJsonArray &arr);
    void sendDiscoveryBroadcast();
};

#endif // NETWORK_MANAGER_H
