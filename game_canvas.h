#ifndef GAME_CANVAS_H
#define GAME_CANVAS_H

#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QString>
#include <QPointF>
#include <vector>
#include <set>
#include <cmath>
#include "kolkata_types.h"
#include "garage_system.h"
#include "hazard_system.h"
#include "kolkata_map.h"
#include "sprite_atlas.h"
#include "network_manager.h"

class GameCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit GameCanvas(QWidget *parent = nullptr);
    ~GameCanvas() override = default;

    void setStage(KolkataStage stage);
    void resetKart();
    void triggerCelebration(const QString &title = "SECTOR SECURED", const QString &subtitle = "KEEP MOVING");
    void startRace(bool resumeFromCheckpoint = false);

    // Backward compat aliases
    void setTrackLevel(TrackLevel level) { setStage(level); }

    GameState getGameState() const { return currentState; }
    void setGameState(GameState state);
    GameMode getGameMode() const { return selectedGameMode; }
    void setGameMode(GameMode mode) { selectedGameMode = mode; update(); }
    const std::vector<VehicleDef>& getVehicles() const { return vehicles; }
    const VehicleDef& getSelectedVehicleP1() const;
    const VehicleDef& getSelectedVehicleP2() const;
    int getSelectedVehicleIndexP1() const { return selectedVehicleP1; }
    int getSelectedVehicleIndexP2() const { return selectedVehicleP2; }
    void setSelectedVehicleIndexP1(int index) { selectedVehicleP1 = (index % 4 + 4) % 4; update(); }
    void setSelectedVehicleIndexP2(int index) { selectedVehicleP2 = (index % 4 + 4) % 4; update(); }

    // Backward compat aliases
    const std::vector<VehicleDef>& getCharacters() const { return vehicles; }
    const VehicleDef& getSelectedCharacterP1() const { return getSelectedVehicleP1(); }
    const VehicleDef& getSelectedCharacterP2() const { return getSelectedVehicleP2(); }
    int getSelectedCharIndexP1() const { return selectedVehicleP1; }
    int getSelectedCharIndexP2() const { return selectedVehicleP2; }
    void setSelectedCharIndexP1(int index) { setSelectedVehicleIndexP1(index); }
    void setSelectedCharIndexP2(int index) { setSelectedVehicleIndexP2(index); }

    const std::vector<QPointF>& getTrackWaypoints() const { return currentWaypoints; }
    const std::vector<QPointF>& getTrackWaypoints(int trackIdx) const {
        return (trackIdx >= 0 && trackIdx < 3) ? stageWaypoints[trackIdx] : currentWaypoints;
    }
    const std::vector<KartEntity>& getAIOpponents() const { return aiOpponents; }
    const std::vector<KartEntity>& getRemoteKarts() const { return remoteKarts; }
    const std::vector<Projectile>& getActiveProjectiles() const { return activeProjectiles; }
    const std::vector<TrackHazard>& getTrackHazards() const { return trackHazards; }

    NetworkManager* getNetworkManager() const { return networkManager; }
    void startMultiplayerRace(int stageLevel);
    MultiLobbyState getMultiLobbyState() const { return multiLobbyState; }
    bool getIsNetworkRace() const { return isNetworkRace; }

    std::vector<WorldSprite> worldSprites;

signals:
    void statsUpdated(double speed, int lap, double x, double z, double angle, QString surface);
    void survivalTelemetryUpdated(double hull, double evacTime, int rank, int totalRacers,
                                  int progressPct, int scrap, QString district);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private slots:
    void updateGameLoop();

private:
    static constexpr int BUFFER_WIDTH = 400;
    static constexpr int BUFFER_HEIGHT = 300;
    static constexpr int HORIZON_Y = 150;
    static constexpr int TRACK_MAP_SIZE = KolkataMap::MAP_SIZE; // 4096

    QImage screenBuffer;
    QTimer gameTimer;
    QElapsedTimer frameTimer;
    std::set<int> pressedKeys;

    KolkataMap kolkataMap;
    HazardSystem hazardSystem;

    // Game State Flow & Mode
    GameState currentState = GameState::STORY_INTRO;
    GameMode selectedGameMode = GameMode::RANKED_ESCAPE;
    int menuIndex = 0;
    int selectedVehicleP1 = 0;
    int selectedVehicleP2 = 1;
    int charSelectPhase = 0;
    int garageUpgradeRow = 0;
    QString garageStatusMsg;
    double garageMsgTimer = 0.0;

    // Boot animated story cutscene state
    double storyTimer = 0.0;
    static constexpr double STORY_TOTAL_DURATION = 22.0;
    double wipeTransitionTimer = 0.0;

    // Continuous campaign & interactive checkpoint pit-stop state
    CheckpointSave activeCheckpointSave;
    bool pitIsStageTransition = false;
    int pitCheckpointIndex = 0;
    bool pitLocalReady = false;
    double checkpointPromptTimer = 0.0;
    int pitFocusRow = 0;
    QString pitStatusMsg;
    double pitStatusTimer = 0.0;

    // Backward compat
    int &selectedCharP1 = selectedVehicleP1;
    int &selectedCharP2 = selectedVehicleP2;
    std::vector<VehicleDef> vehicles;
    std::vector<VehicleDef>& characters = vehicles;

    void initVehicles();

    // Multi-vehicle entities
    KartEntity player1;
    KartEntity player2;
    std::vector<KartEntity> aiOpponents;
    std::vector<Projectile> activeProjectiles;
    std::vector<TrackHazard> trackHazards;
    std::vector<CollapsingStructure> collapsingStructures;
    std::vector<CheckpointDef> stageCheckpoints;

    double globalTime = 0.0;

    // Stage state
    KolkataStage currentStage = KolkataStage::GARIAHAT_COLLAPSE;
    KolkataStage &currentLevel = currentStage;
    QImage trackVisualMap;
    QImage trackMaskMap;

    // Hull & survival systems
    HullState playerHull;
    double evacTimer = 180.0;
    double evacTimerMax = 180.0;
    double collapseWaveProgress = -0.08;
    double collapseWaveSpeed = 0.0022;
    int scrapCollectedThisRun = 0;
    bool didSurviveRun = true;
    QString finishReason = "EXTRACTION SECURED";
    double damageFlashTimer = 0.0;
    double hitRippleTimer = 0.0;
    double lightningFlashTimer = 0.0;

    // Nitrous & Ram Shield
    double nitrousFuel = 60.0;
    double nitrousMaxFuel = 100.0;
    bool nitrousActive = false;
    double nitrousBoostTimer = 0.0;
    double dozerRamTimer = 0.0;

    // Vehicle physics state
    double kartX = 350.0;
    double kartZ = 3800.0;
    double kartAngle = 0.0;
    double kartSpeed = 0.0;
    double lastSafeX = 350.0;
    double lastSafeZ = 3800.0;
    double lastSafeAngle = 0.0;
    double maxSpeed = 142.0;
    double accel = 105.0;
    double brakeDecel = 250.0;
    double friction = 45.0;
    double turnRate = 2.8;
    double driftFactor = 1.0;
    double weight = 1.0;
    double armorRating = 0.0;
    double offroadGripBonus = 0.0;
    double steeringVisualAngle = 0.0;
    double bodyRoll = 0.0; // Scanline banking roll on turns/drifts

    // Handbrake Slide & Mini-Turbo (no kart hop)
    bool isDrifting = false;
    double driftTimer = 0.0;
    double miniTurboTimer = 0.0;
    double lateralSlipAngle = 0.0;
    int driftDirection = 0;

    struct SparkParticle {
        double x, y;
        double vx, vy;
        double life;
        double maxLife;
        QRgb color;
    };
    std::vector<SparkParticle> driftSparks;
    void updateDriftSparks(double dt);

    // Camera parameters (lowered for authentic long-track scale)
    double cameraHeight = 11.0;
    double focalLength = 185.0;
    double cameraNearPlane = 2.0;
    double cameraShakeX = 0.0;
    double cameraShakeY = 0.0;
    double horizonPitchOffset = 0.0;

    // 2D atmospheric ash + 3D world-space smoke/ember/dust/spark particles
    struct AshParticle {
        double x, y;
        double vx, vy;
        double size;
        QRgb color;
        bool isEmber;
    };
    std::vector<AshParticle> ashParticles;
    std::vector<WorldParticle> worldParticles;
    void initAshParticles();
    void updateAshParticles(double dt);
    void spawnWorldParticle(double x, double y, double z, double vx, double vy, double vz,
                            double life, double size, QRgb color, WorldParticleKind kind);
    void updateWorldParticles(double dt);

    // Route & checkpoint state
    int lastCheckpointReached = 0;
    double splineProgress = 0.0;
    double progressMeters = 0.0;
    int playerPlacement = 1;
    QString currentSurfaceName = "Cracked Asphalt";
    QString currentDistrictName = "GARIAHAT CROSSING";
    bool isRaceFinished = false;

    int currentLap = 1;
    int nextCheckpoint = 1;

    // Tactical Gear (No RNG Roulette)
    SurvivalGear heldGear = SurvivalGear::NONE;
    PlayerItem &heldItem = heldGear;
    double mushroomBoostTimer = 0.0;
    bool isSpinning = false;
    double spinTimer = 0.0;
    double inkTimer = 0.0;

    // Status Banner
    bool isCelebrationActive = false;
    double celebrationTimer = 0.0;
    double celebrationDuration = 3.2;
    QString celebrationTitle = "CHECKPOINT CLEARED";
    QString celebrationSubtitle = "PUSH FORWARD";
    double flashIntensity = 0.0;

    bool hasPassedHalfway = false;
    bool firstStartCrossed = false;

    // Waypoints & cumulative route distance per stage
    std::vector<QPointF> stageWaypoints[3];
    std::vector<QPointF> currentWaypoints;
    std::vector<double> waypointCumDist;
    double totalRouteLength = 20000.0;
    std::vector<QPointF> (&trackWaypoints)[3] = stageWaypoints;
    std::vector<QPointF> &currentTrackWaypoints = currentWaypoints;

    void initStageWaypoints();
    void rebuildSplineMetrics();

    void initStageMaps();
    void generateStage(KolkataStage stage);
    void applyCraterStamps();

    // Simulation updates
    void updatePhysics(double dt);
    void updateAIOpponents(double dt);
    void updateGearSystem(double dt);
    void updateProjectiles(double dt);
    void updateEvacTimer(double dt);
    void updateCollapseWave(double dt);
    void finishRun(bool survived, const QString &reason);
    void saveCheckpointSnapshot(int cpIdx);
    void enterCheckpointPit(int cpIdx, bool stageTransition);
    void rolloutFromCheckpointPit();
    QString determineDistrictName() const;
    SpriteType getVehicleSpriteType(int vehicleId, bool isAlt = false) const;

    double computeSplineProgress(double px, double pz, double *outMeters = nullptr) const;
    void updateRankings();
    void triggerCheckpoints(double oldX, double oldZ, double newX, double newZ);

    // Rendering pipeline
    void renderStoryIntro();
    void renderSky();
    void renderGroundMode7();
    void renderSprites();
    void renderCockpit();
    void renderWindshieldCracks();
    void renderAshOverlay();
    void renderSpeedLines();
    void renderGearHUD();
    void renderMinimap();
    void renderEscapeHUD();
    void renderCheckpointPitOverlay();
    void renderWipeTransition();

    void renderItemHUD() { renderGearHUD(); }
    void renderInkOverlay() {}

    QImage minimapTrackCache;
    void updateMinimapTrackCache();

    void updateCelebration(double dt);
    void renderCelebration();

    // Menu and overlay rendering
    void renderTitleMenu();
    void renderGarageScreen();
    void renderRaceFinishedOverlay();
    void renderMultiplayerLobby();
    void renderMultiplayerRankings();
    void renderCharacterSelect() { renderGarageScreen(); }

    // Input handlers
    void handleStoryIntroInput(int key);
    void handleTitleMenuInput(int key);
    void handleGarageInput(int key);
    void handleRacingInput(int key);
    void handleCheckpointPitInput(int key);
    void handleMultiplayerLobbyInput(QKeyEvent *event);
    void confirmTitleMenuSelection();
    void handleCharacterSelectInput(int key) { handleGarageInput(key); }

    // LAN Multiplayer state
    NetworkManager *networkManager = nullptr;
    MultiLobbyState multiLobbyState = MultiLobbyState::MODE_CHOICE;
    int multiChoiceIndex = 0;
    QString inputSecretCode = "";
    QString inputPlayerName = "SURVIVOR 2";
    QString hostPlayerName = "HOST";
    QString currentSecretCode = "EVAC7";
    int activeInputField = 1;
    bool isNetworkRace = false;
    double raceElapsedTime = 0.0;
    double networkSyncTimer = 0.0;
    std::vector<KartEntity> remoteKarts;

    void initNetworkManager();
    void updateRemoteKarts(double dt);
    QString generateSecretCode();

    void drawTexturedBillboard(int screenX, int screenY, int drawW, int drawH, const QImage& texture);
    void useHeldGear();
    void useHeldItem() { useHeldGear(); }
};

#endif // GAME_CANVAS_H
