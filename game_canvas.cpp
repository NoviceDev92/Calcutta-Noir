#include "game_canvas.h"
#include <QPainter>
#include <QPainterPath>
#include <QKeyEvent>
#include <QFont>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// KOLKATA: LAST EXIT — Overhauled Software-Rasterized Apocalyptic Engine
// Pure Qt QImage CPU rendering: Story Intro, Continuous 3-Stage Escape,
// Synchronized Checkpoint Pit-Stops, 3D Particles & Dynamic Firelight.
// ============================================================================

GameCanvas::GameCanvas(QWidget *parent)
    : QWidget(parent),
      screenBuffer(BUFFER_WIDTH, BUFFER_HEIGHT, QImage::Format_RGB32),
      trackVisualMap(TRACK_MAP_SIZE, TRACK_MAP_SIZE, QImage::Format_RGB32),
      trackMaskMap(TRACK_MAP_SIZE, TRACK_MAP_SIZE, QImage::Format_RGB32)
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(400, 300);

    initVehicles();
    initStageWaypoints();
    initStageMaps();
    generateStage(KolkataStage::GARIAHAT_COLLAPSE);
    initAshParticles();

    // Boot directly into the animated pixel-art story cutscene
    currentState = GameState::STORY_INTRO;
    storyTimer = 0.0;

    initNetworkManager();

    connect(&gameTimer, &QTimer::timeout, this, &GameCanvas::updateGameLoop);
    gameTimer.start(16); // ~60 FPS
    frameTimer.start();
}

void GameCanvas::initVehicles()
{
    const auto &roster = GarageSystem::instance().vehicleRoster();
    vehicles.assign(roster.begin(), roster.end());
    selectedVehicleP1 = GarageSystem::instance().selectedVehicle();
}

const VehicleDef& GameCanvas::getSelectedVehicleP1() const
{
    static VehicleDef fallback;
    if (vehicles.empty()) return fallback;
    int idx = (selectedVehicleP1 % static_cast<int>(vehicles.size()) + static_cast<int>(vehicles.size())) % static_cast<int>(vehicles.size());
    return vehicles[idx];
}

const VehicleDef& GameCanvas::getSelectedVehicleP2() const
{
    static VehicleDef fallback;
    if (vehicles.empty()) return fallback;
    int idx = (selectedVehicleP2 % static_cast<int>(vehicles.size()) + static_cast<int>(vehicles.size())) % static_cast<int>(vehicles.size());
    return vehicles[idx];
}

void GameCanvas::initStageWaypoints()
{
    QImage dummyVis(16, 16, QImage::Format_RGB32);
    QImage dummyMask(16, 16, QImage::Format_RGB32);
    std::vector<WorldSprite> tmpSprites;
    std::vector<CollapsingStructure> tmpCollapsing;
    std::vector<TrackHazard> tmpHazards;
    std::vector<CheckpointDef> tmpCps;

    for (int i = 0; i < 3; ++i) {
        kolkataMap.generateStage(static_cast<KolkataStage>(i),
                                 dummyVis, dummyMask,
                                 tmpSprites, tmpCollapsing, tmpHazards,
                                 stageWaypoints[i], tmpCps);
    }
    currentWaypoints = stageWaypoints[static_cast<int>(currentStage)];
}

void GameCanvas::initStageMaps()
{
    generateStage(currentStage);
}

void GameCanvas::generateStage(KolkataStage stage)
{
    kolkataMap.generateStage(stage,
                             trackVisualMap, trackMaskMap,
                             worldSprites, collapsingStructures, trackHazards,
                             currentWaypoints, stageCheckpoints);

    stageWaypoints[static_cast<int>(stage)] = currentWaypoints;
    rebuildSplineMetrics();

    hazardSystem.reset();
    hazardSystem.setStageDifficulty(stage);
    for (const auto &cs : collapsingStructures) {
        hazardSystem.addCollapsingStructure(cs);
    }
    for (const auto &hz : trackHazards) {
        hazardSystem.addTrackHazard(hz);
    }

    updateMinimapTrackCache();
}

void GameCanvas::rebuildSplineMetrics()
{
    waypointCumDist.clear();
    totalRouteLength = 0.0;
    if (currentWaypoints.empty()) return;

    waypointCumDist.reserve(currentWaypoints.size());
    waypointCumDist.push_back(0.0);
    for (size_t i = 1; i < currentWaypoints.size(); ++i) {
        double dx = currentWaypoints[i].x() - currentWaypoints[i - 1].x();
        double dz = currentWaypoints[i].y() - currentWaypoints[i - 1].y();
        totalRouteLength += std::hypot(dx, dz);
        waypointCumDist.push_back(totalRouteLength);
    }
    if (totalRouteLength < 1.0) totalRouteLength = 1.0;
}

double GameCanvas::computeSplineProgress(double px, double pz, double *outMeters) const
{
    if (currentWaypoints.size() < 2) {
        if (outMeters) *outMeters = 0.0;
        return 0.0;
    }

    double bestDistSq = 1e18;
    double bestMeters = 0.0;

    for (size_t i = 0; i + 1 < currentWaypoints.size(); ++i) {
        double ax = currentWaypoints[i].x();
        double az = currentWaypoints[i].y();
        double bx = currentWaypoints[i + 1].x();
        double bz = currentWaypoints[i + 1].y();

        double abx = bx - ax;
        double abz = bz - az;
        double lenSq = abx * abx + abz * abz;
        double t = 0.0;
        if (lenSq > 1e-6) {
            t = std::clamp(((px - ax) * abx + (pz - az) * abz) / lenSq, 0.0, 1.0);
        }
        double cx = ax + t * abx;
        double cz = az + t * abz;
        double dSq = (px - cx) * (px - cx) + (pz - cz) * (pz - cz);
        if (dSq < bestDistSq) {
            bestDistSq = dSq;
            bestMeters = waypointCumDist[i] + t * std::sqrt(lenSq);
        }
    }

    if (outMeters) *outMeters = bestMeters;
    return std::clamp(bestMeters / totalRouteLength, 0.0, 1.0);
}

void GameCanvas::setGameState(GameState state)
{
    currentState = state;
    pressedKeys.clear();
    update();
}

void GameCanvas::setStage(KolkataStage stage)
{
    currentStage = stage;
    activeCheckpointSave.valid = false;
    generateStage(currentStage);
    if (currentState == GameState::RACING || currentState == GameState::RACE_FINISHED || currentState == GameState::CHECKPOINT_PIT) {
        startRace(false);
    }
    update();
}

void GameCanvas::startRace(bool resumeFromCheckpoint)
{
    isNetworkRace = false;
    if (vehicles.empty()) initVehicles();
    pressedKeys.clear();

    if (resumeFromCheckpoint && activeCheckpointSave.valid) {
        currentStage = activeCheckpointSave.stage;
    }
    generateStage(currentStage);

    GarageSystem &garage = GarageSystem::instance();
    garage.selectVehicle(selectedVehicleP1);
    VehicleDef v1 = garage.selectedVehicleDef();
    const auto &ups = garage.upgrades();

    // Tuned speeds for authentic ~2.5-3.5 min long-map traversal
    double speedScale = 0.82;
    this->maxSpeed = v1.maxSpeed * speedScale;
    this->accel = v1.accel * 0.90;
    this->brakeDecel = 250.0;
    this->turnRate = v1.turnRate;
    this->driftFactor = v1.driftFactor;
    this->weight = v1.weight;
    this->armorRating = v1.armorRating;
    this->offroadGripBonus = ups.suspensionLevel * 0.08;
    this->nitrousMaxFuel = 100.0 + ups.nitrousLevel * 15.0;

    isDrifting = false;
    driftTimer = 0.0;
    miniTurboTimer = 0.0;
    lateralSlipAngle = 0.0;
    driftDirection = 0;
    bodyRoll = 0.0;
    driftSparks.clear();
    worldParticles.clear();

    double startX = 350.0, startZ = 3800.0, startAngle = 0.0;
    if (currentWaypoints.size() >= 2) {
        startX = currentWaypoints[0].x();
        startZ = currentWaypoints[0].y();
        startAngle = std::atan2(currentWaypoints[1].y() - startZ,
                                currentWaypoints[1].x() - startX);
    }

    switch (currentStage) {
    case KolkataStage::GARIAHAT_COLLAPSE:  evacTimerMax = 180.0; collapseWaveSpeed = 0.0020; break;
    case KolkataStage::PARK_STREET_RUINS:  evacTimerMax = 210.0; collapseWaveSpeed = 0.0023; break;
    case KolkataStage::HOWRAH_BRIDGE:      evacTimerMax = 240.0; collapseWaveSpeed = 0.0026; break;
    }

    if (resumeFromCheckpoint && activeCheckpointSave.valid) {
        kartX = activeCheckpointSave.x;
        kartZ = activeCheckpointSave.z;
        kartAngle = activeCheckpointSave.angle;
        lastCheckpointReached = activeCheckpointSave.checkpointIdx;
        nextCheckpoint = lastCheckpointReached + 1;
        playerHull = HullState();
        playerHull.integrity = std::max(35.0, activeCheckpointSave.hullIntegrity);
        playerHull.updateCrackLevel();
        evacTimer = std::max(45.0, activeCheckpointSave.evacTimer);
        nitrousFuel = std::max(40.0, activeCheckpointSave.nitrousFuel);
        collapseWaveProgress = activeCheckpointSave.collapseWaveProgress;
        raceElapsedTime = activeCheckpointSave.cumulativeTime;
        scrapCollectedThisRun = activeCheckpointSave.scrapThisRun;
    } else {
        kartX = startX;
        kartZ = startZ;
        kartAngle = startAngle;
        lastCheckpointReached = 0;
        nextCheckpoint = 1;
        playerHull = garage.currentHull();
        if (playerHull.isWrecked() || playerHull.integrity < 30.0) {
            playerHull = HullState();
        }
        playerHull.isOnFire = false;
        playerHull.fireTimer = 0.0;
        playerHull.overheatTimer = 0.0;
        playerHull.updateCrackLevel();
        evacTimer = evacTimerMax;
        collapseWaveProgress = -0.08;
        nitrousFuel = nitrousMaxFuel * 0.65;
        if (currentStage == KolkataStage::GARIAHAT_COLLAPSE) {
            raceElapsedTime = 0.0;
            scrapCollectedThisRun = 0;
        }
        saveCheckpointSnapshot(0);
    }

    lastSafeX = kartX;
    lastSafeZ = kartZ;
    lastSafeAngle = kartAngle;
    kartSpeed = 0.0;
    steeringVisualAngle = 0.0;
    splineProgress = computeSplineProgress(kartX, kartZ, &progressMeters);
    playerPlacement = 1;
    currentLap = 1;
    hasPassedHalfway = false;
    firstStartCrossed = true;

    player1 = KartEntity();
    player1.id = 0;
    player1.isHuman = true;
    player1.vehicleId = v1.id;
    player1.characterId = v1.id;
    player1.name = v1.name;
    player1.kartColor = v1.primaryColor;
    player1.maxSpeed = this->maxSpeed;
    player1.accel = this->accel;
    player1.turnRate = v1.turnRate;
    player1.driftFactor = v1.driftFactor;
    player1.weight = v1.weight;
    player1.armorRating = v1.armorRating;
    player1.x = kartX;
    player1.z = kartZ;
    player1.angle = kartAngle;
    player1.hull = playerHull;

    didSurviveRun = true;
    finishReason = "";
    damageFlashTimer = 0.0;
    hitRippleTimer = 0.0;
    lightningFlashTimer = 0.0;
    checkpointPromptTimer = 0.0;
    nitrousActive = false;
    nitrousBoostTimer = 0.0;
    dozerRamTimer = 0.0;
    cameraShakeX = 0.0;
    cameraShakeY = 0.0;
    horizonPitchOffset = 0.0;
    wipeTransitionTimer = 0.45;

    // Spawn AI Survivor Rivals near player start/resume
    aiOpponents.clear();
    if (selectedGameMode == GameMode::RANKED_ESCAPE) {
        double fwdX = std::cos(kartAngle);
        double fwdZ = std::sin(kartAngle);
        double rightX = -fwdZ;
        double rightZ = fwdX;
        int startWp = 1;
        if (resumeFromCheckpoint && !currentWaypoints.empty()) {
            startWp = std::min(static_cast<int>(currentWaypoints.size()) - 1,
                               static_cast<int>(splineProgress * currentWaypoints.size()) + 1);
        }

        int aiSlot = 1;
        for (int i = 0; i < static_cast<int>(vehicles.size()); ++i) {
            if (i == selectedVehicleP1) continue;
            const VehicleDef &aiDef = vehicles[i];
            KartEntity aiKart;
            aiKart.id = aiSlot;
            aiKart.isHuman = false;
            aiKart.isAI = true;
            aiKart.vehicleId = aiDef.id;
            aiKart.characterId = aiDef.id;
            aiKart.name = aiDef.name;
            aiKart.kartColor = aiDef.primaryColor;
            aiKart.maxSpeed = aiDef.maxSpeed * speedScale;
            aiKart.accel = aiDef.accel * 0.90;
            aiKart.brakeDecel = 240.0;
            aiKart.friction = 45.0;
            aiKart.turnRate = aiDef.turnRate;
            aiKart.driftFactor = aiDef.driftFactor;
            aiKart.weight = aiDef.weight;
            aiKart.armorRating = aiDef.armorRating;
            aiKart.x = kartX + fwdX * (-14.0 * aiSlot) + rightX * ((aiSlot % 2 == 0) ? -12.0 : 12.0);
            aiKart.z = kartZ + fwdZ * (-14.0 * aiSlot) + rightZ * ((aiSlot % 2 == 0) ? -12.0 : 12.0);
            aiKart.angle = kartAngle;
            aiKart.speed = 0.0;
            aiKart.currentWaypoint = startWp;
            aiKart.currentWaypointIndex = startWp;
            aiKart.placement = aiSlot + 1;
            aiKart.hull = HullState();
            aiOpponents.push_back(aiKart);
            aiSlot++;
            if (aiSlot >= 4) break;
        }
    }

    heldGear = SurvivalGear::NONE;
    mushroomBoostTimer = 0.0;
    isSpinning = false;
    spinTimer = 0.0;
    inkTimer = 0.0;
    isCelebrationActive = false;
    isRaceFinished = false;
    celebrationTimer = 0.0;
    flashIntensity = 0.0;
    activeProjectiles.clear();
    initAshParticles();

    currentDistrictName = determineDistrictName();
    currentState = GameState::RACING;
    pressedKeys.clear();

    QString stageTitle = (currentStage == KolkataStage::GARIAHAT_COLLAPSE) ? "SECTOR 1: GARIAHAT RUINS" :
                         (currentStage == KolkataStage::PARK_STREET_RUINS) ? "SECTOR 2: PARK ST & MAIDAN" :
                                                                             "SECTOR 3: HOWRAH BRIDGE";
    QString diffStr = (currentStage == KolkataStage::GARIAHAT_COLLAPSE) ? "MEDIUM • REACH AJC BOSE RAMP" :
                      (currentStage == KolkataStage::PARK_STREET_RUINS) ? "HARD • REACH PRINSEP GHAT" :
                                                                          "INSANE • CROSS THE CANTILEVER";
    triggerCelebration(stageTitle, diffStr);
    update();
}

void GameCanvas::resetKart()
{
    if (currentState == GameState::RACING || currentState == GameState::RACE_FINISHED || currentState == GameState::CHECKPOINT_PIT) {
        startRace(activeCheckpointSave.valid);
    }
}

void GameCanvas::saveCheckpointSnapshot(int cpIdx)
{
    activeCheckpointSave.valid = true;
    activeCheckpointSave.stage = currentStage;
    activeCheckpointSave.checkpointIdx = cpIdx;
    activeCheckpointSave.x = kartX;
    activeCheckpointSave.z = kartZ;
    activeCheckpointSave.angle = kartAngle;
    activeCheckpointSave.hullIntegrity = std::max(35.0, playerHull.integrity);
    activeCheckpointSave.evacTimer = evacTimer;
    activeCheckpointSave.nitrousFuel = nitrousFuel;
    activeCheckpointSave.collapseWaveProgress = collapseWaveProgress;
    activeCheckpointSave.cumulativeTime = raceElapsedTime;
    activeCheckpointSave.scrapThisRun = scrapCollectedThisRun;
}

void GameCanvas::enterCheckpointPit(int cpIdx, bool stageTransition)
{
    pitIsStageTransition = stageTransition;
    pitCheckpointIndex = cpIdx;
    pitLocalReady = false;
    pitFocusRow = 0;
    pitStatusMsg.clear();
    pitStatusTimer = 0.0;
    pressedKeys.clear();
    wipeTransitionTimer = 0.35;
    currentState = GameState::CHECKPOINT_PIT;

    if (isNetworkRace && networkManager) {
        networkManager->sendCheckpointReady(cpIdx, false);
    }
}

void GameCanvas::rolloutFromCheckpointPit()
{
    // Re-bake upgraded stats immediately!
    VehicleDef v1 = GarageSystem::instance().selectedVehicleDef();
    const auto &ups = GarageSystem::instance().upgrades();
    this->maxSpeed = v1.maxSpeed * 0.82;
    this->accel = v1.accel * 0.90;
    this->turnRate = v1.turnRate;
    this->armorRating = v1.armorRating;
    this->offroadGripBonus = ups.suspensionLevel * 0.08;
    this->nitrousMaxFuel = 100.0 + ups.nitrousLevel * 15.0;

    wipeTransitionTimer = 0.35;
    pressedKeys.clear();

    if (pitIsStageTransition) {
        if (currentStage == KolkataStage::GARIAHAT_COLLAPSE) {
            currentStage = KolkataStage::PARK_STREET_RUINS;
        } else if (currentStage == KolkataStage::PARK_STREET_RUINS) {
            currentStage = KolkataStage::HOWRAH_BRIDGE;
        }
        activeCheckpointSave.valid = false;
        GarageSystem::instance().currentHull() = playerHull;
        if (isNetworkRace) {
            startMultiplayerRace(static_cast<int>(currentStage));
        } else {
            startRace(false);
        }
    } else {
        saveCheckpointSnapshot(pitCheckpointIndex);
        currentState = GameState::RACING;
        triggerCelebration("ROLLING OUT!", "NEXT SECTOR AHEAD");
    }
}

// -------------------------------------------------------------
// INPUT HANDLING
// -------------------------------------------------------------

void GameCanvas::mousePressEvent(QMouseEvent *event) { setFocus(); QWidget::mousePressEvent(event); }
void GameCanvas::focusInEvent(QFocusEvent *event) { QWidget::focusInEvent(event); }
void GameCanvas::focusOutEvent(QFocusEvent *event) { pressedKeys.clear(); QWidget::focusOutEvent(event); }

void GameCanvas::keyPressEvent(QKeyEvent *event)
{
    pressedKeys.insert(event->key());

    switch (currentState) {
    case GameState::STORY_INTRO:
        handleStoryIntroInput(event->key());
        break;
    case GameState::TITLE_MENU:
        handleTitleMenuInput(event->key());
        break;
    case GameState::GARAGE:
        handleGarageInput(event->key());
        break;
    case GameState::RACING:
    case GameState::RACE_FINISHED:
        handleRacingInput(event->key());
        break;
    case GameState::CHECKPOINT_PIT:
        handleCheckpointPitInput(event->key());
        break;
    case GameState::MULTIPLAYER_LOBBY:
        handleMultiplayerLobbyInput(event);
        break;
    }
}

void GameCanvas::keyReleaseEvent(QKeyEvent *event)
{
    pressedKeys.erase(event->key());
}

void GameCanvas::handleStoryIntroInput(int key)
{
    if (key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape) {
        wipeTransitionTimer = 0.45;
        currentState = GameState::TITLE_MENU;
        update();
    }
}

void GameCanvas::handleTitleMenuInput(int key)
{
    if (key == Qt::Key_W || key == Qt::Key_Up) {
        menuIndex = (menuIndex - 1 + 3) % 3;
    } else if (key == Qt::Key_S || key == Qt::Key_Down) {
        menuIndex = (menuIndex + 1) % 3;
    } else if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) {
        confirmTitleMenuSelection();
    } else if (key == Qt::Key_1) {
        menuIndex = 0; confirmTitleMenuSelection();
    } else if (key == Qt::Key_2) {
        menuIndex = 1; confirmTitleMenuSelection();
    } else if (key == Qt::Key_3) {
        menuIndex = 2; confirmTitleMenuSelection();
    } else if (key == Qt::Key_C) {
        storyTimer = 0.0;
        wipeTransitionTimer = 0.4;
        currentState = GameState::STORY_INTRO;
    }
}

void GameCanvas::confirmTitleMenuSelection()
{
    wipeTransitionTimer = 0.35;
    switch (menuIndex) {
    case 0:
        selectedGameMode = GameMode::RANKED_ESCAPE;
        currentStage = KolkataStage::GARIAHAT_COLLAPSE;
        activeCheckpointSave.valid = false;
        currentState = GameState::GARAGE;
        break;
    case 1:
        selectedGameMode = GameMode::MULTIPLAYER;
        currentState = GameState::MULTIPLAYER_LOBBY;
        multiLobbyState = MultiLobbyState::MODE_CHOICE;
        break;
    case 2:
        selectedGameMode = GameMode::TIME_TRIAL;
        activeCheckpointSave.valid = false;
        currentState = GameState::GARAGE;
        break;
    }
    update();
}

void GameCanvas::handleGarageInput(int key)
{
    auto &garage = GarageSystem::instance();
    if (key == Qt::Key_A || key == Qt::Key_Left) {
        selectedVehicleP1 = (selectedVehicleP1 - 1 + 4) % 4;
        garage.selectVehicle(selectedVehicleP1);
    } else if (key == Qt::Key_D || key == Qt::Key_Right) {
        selectedVehicleP1 = (selectedVehicleP1 + 1) % 4;
        garage.selectVehicle(selectedVehicleP1);
    } else if (key == Qt::Key_W || key == Qt::Key_Up) {
        garageUpgradeRow = (garageUpgradeRow - 1 + 5) % 5;
    } else if (key == Qt::Key_S || key == Qt::Key_Down) {
        garageUpgradeRow = (garageUpgradeRow + 1) % 5;
    } else if (key == Qt::Key_U || key == Qt::Key_B) {
        bool ok = false;
        QString label;
        if (garageUpgradeRow == 0)      { ok = garage.upgradeArmor();      label = "ARMOR PLATING"; }
        else if (garageUpgradeRow == 1) { ok = garage.upgradeEngine();     label = "ENGINE / TURBO"; }
        else if (garageUpgradeRow == 2) { ok = garage.upgradeSuspension(); label = "SUSPENSION"; }
        else if (garageUpgradeRow == 3) { ok = garage.upgradeNitrous();    label = "NITROUS TANK"; }
        else if (garageUpgradeRow == 4) {
            int missing = static_cast<int>(std::ceil(100.0 - garage.currentHull().integrity));
            ok = (missing > 0) && garage.fieldRepair(missing);
            label = "CHASSIS REPAIR";
        }
        garageStatusMsg = ok ? (label + " APPLIED!") : "INSUFFICIENT SCRAP OR MAXED!";
        garageMsgTimer = 1.8;
    } else if (key == Qt::Key_1) {
        currentStage = KolkataStage::GARIAHAT_COLLAPSE;
        activeCheckpointSave.valid = false;
    } else if (key == Qt::Key_2) {
        currentStage = KolkataStage::PARK_STREET_RUINS;
        activeCheckpointSave.valid = false;
    } else if (key == Qt::Key_3) {
        currentStage = KolkataStage::HOWRAH_BRIDGE;
        activeCheckpointSave.valid = false;
    } else if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) {
        startRace(false);
    } else if (key == Qt::Key_Escape) {
        currentState = GameState::TITLE_MENU;
    }
    update();
}

void GameCanvas::handleRacingInput(int key)
{
    if (key == Qt::Key_R) {
        if (pressedKeys.count(Qt::Key_Shift) > 0) {
            activeCheckpointSave.valid = false;
            GarageSystem::instance().currentHull() = HullState();
            startRace(false);
        } else {
            resetKart(); // Resumes from saved checkpoint if available
        }
    } else if (key == Qt::Key_P) {
        if (!isRaceFinished && (checkpointPromptTimer > 0.0 || lastCheckpointReached > 0)) {
            enterCheckpointPit(lastCheckpointReached, false);
        }
    } else if (key == Qt::Key_Space) {
        if (nitrousFuel > 8.0) nitrousActive = true;
    } else if (key == Qt::Key_E) {
        if (heldGear != SurvivalGear::NONE) useHeldGear();
    } else if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (isRaceFinished) currentState = GameState::GARAGE;
    }
}

void GameCanvas::handleCheckpointPitInput(int key)
{
    auto &garage = GarageSystem::instance();
    if (key == Qt::Key_W || key == Qt::Key_Up) {
        pitFocusRow = (pitFocusRow - 1 + 6) % 6;
    } else if (key == Qt::Key_S || key == Qt::Key_Down) {
        pitFocusRow = (pitFocusRow + 1) % 6;
    } else if (key == Qt::Key_1) {
        pitFocusRow = 0;
    } else if (key == Qt::Key_2) {
        pitFocusRow = 1;
    } else if (key == Qt::Key_3) {
        pitFocusRow = 2;
    } else if (key == Qt::Key_4) {
        pitFocusRow = 3;
    } else if (key == Qt::Key_5) {
        pitFocusRow = 4;
    } else if (key == Qt::Key_6) {
        pitFocusRow = 5;
    }

    if (key == Qt::Key_U || key == Qt::Key_B || (key >= Qt::Key_1 && key <= Qt::Key_6)) {
        bool ok = false;
        if (pitFocusRow == 0) {
            // Field Hull Repair (+40% for 50 Scrap)
            int missing = static_cast<int>(std::ceil(100.0 - playerHull.integrity));
            int cost = std::min(60, std::max(25, missing));
            if (missing > 0 && garage.fieldRepair(cost)) {
                playerHull.repair(45.0);
                playerHull.isOnFire = false;
                ok = true;
            }
            pitStatusMsg = ok ? "FIELD WELD COMPLETE (+45% HULL)" : "NEED SCRAP OR HULL FULL";
        } else if (pitFocusRow == 1) {
            if (nitrousFuel < nitrousMaxFuel && garage.fieldRepair(30)) {
                nitrousFuel = nitrousMaxFuel;
                ok = true;
            }
            pitStatusMsg = ok ? "NITROUS TANK TOPPED UP!" : "NEED ₹30 SCRAP OR TANK FULL";
        } else if (pitFocusRow == 2) {
            ok = garage.upgradeArmor();
            pitStatusMsg = ok ? "ARMOR UPGRADED!" : "NEED SCRAP / MAXED";
        } else if (pitFocusRow == 3) {
            ok = garage.upgradeEngine();
            pitStatusMsg = ok ? "ENGINE UPGRADED!" : "NEED SCRAP / MAXED";
        } else if (pitFocusRow == 4) {
            ok = garage.upgradeSuspension();
            pitStatusMsg = ok ? "SUSPENSION UPGRADED!" : "NEED SCRAP / MAXED";
        } else if (pitFocusRow == 5) {
            ok = garage.upgradeNitrous();
            pitStatusMsg = ok ? "NITROUS TANK UPGRADED!" : "NEED SCRAP / MAXED";
        }
        pitStatusTimer = 1.6;
    } else if (key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (isNetworkRace && networkManager) {
            pitLocalReady = !pitLocalReady;
            networkManager->sendCheckpointReady(pitCheckpointIndex, pitLocalReady);
        } else {
            rolloutFromCheckpointPit();
        }
    }
}

// -------------------------------------------------------------
// GAME LOOP & SIMULATION
// -------------------------------------------------------------

void GameCanvas::updateGameLoop()
{
    double dt = frameTimer.restart() / 1000.0;
    if (dt > 0.05) dt = 0.05;

    globalTime += dt;
    if (garageMsgTimer > 0.0) garageMsgTimer = std::max(0.0, garageMsgTimer - dt);
    if (pitStatusTimer > 0.0) pitStatusTimer = std::max(0.0, pitStatusTimer - dt);
    if (wipeTransitionTimer > 0.0) wipeTransitionTimer = std::max(0.0, wipeTransitionTimer - dt);

    switch (currentState) {
    case GameState::STORY_INTRO:
        storyTimer += dt;
        if (storyTimer >= STORY_TOTAL_DURATION) {
            wipeTransitionTimer = 0.45;
            currentState = GameState::TITLE_MENU;
        }
        renderStoryIntro();
        renderWipeTransition();
        emit statsUpdated(0.0, 0, 0.0, 0.0, 0.0, "CUTSCENE: 02:40 IST");
        break;

    case GameState::TITLE_MENU:
        renderTitleMenu();
        renderWipeTransition();
        emit statsUpdated(0.0, 0, 0.0, 0.0, 0.0, "MENU: KOLKATA LAST EXIT");
        emit survivalTelemetryUpdated(100.0, 0.0, 1, 1, 0, GarageSystem::instance().getScrapBalance(), "STANDBY");
        break;

    case GameState::GARAGE:
        renderGarageScreen();
        renderWipeTransition();
        emit statsUpdated(0.0, 0, 0.0, 0.0, 0.0, "GARAGE: RIG & UPGRADES");
        emit survivalTelemetryUpdated(GarageSystem::instance().currentHull().integrity, 0.0, 1, 1, 0,
                                      GarageSystem::instance().getScrapBalance(), "GARAGE BAY");
        break;

    case GameState::MULTIPLAYER_LOBBY:
        renderMultiplayerLobby();
        emit statsUpdated(0.0, 0, 0.0, 0.0, 0.0, "LAN ESCAPE LOBBY");
        break;

    case GameState::CHECKPOINT_PIT:
        updateAshParticles(dt);
        renderSky();
        renderGroundMode7();
        renderSprites();
        renderAshOverlay();
        renderCockpit();
        renderWindshieldCracks();
        renderCheckpointPitOverlay();
        renderWipeTransition();
        break;

    case GameState::RACING:
        raceElapsedTime += dt;
        if (checkpointPromptTimer > 0.0) checkpointPromptTimer = std::max(0.0, checkpointPromptTimer - dt);

        updatePhysics(dt);
        hazardSystem.updateCollapsing(dt, kartX, kartZ);
        hazardSystem.updateHazards(dt);
        hazardSystem.updateSeismic(dt);
        applyCraterStamps();

        if (selectedGameMode != GameMode::TIME_TRIAL) {
            updateEvacTimer(dt);
            updateCollapseWave(dt);
        }

        if (isNetworkRace) {
            updateRemoteKarts(dt);
            networkSyncTimer += dt;
            if (networkSyncTimer >= 0.033 && networkManager) {
                networkSyncTimer = 0.0;
                networkManager->broadcastLocalKartState(
                    kartX, kartZ, kartAngle, kartSpeed, lastCheckpointReached + 1,
                    isRaceFinished, (isRaceFinished ? raceElapsedTime : 0.0),
                    isSpinning, isDrifting, static_cast<int>(heldGear),
                    playerHull.integrity, progressMeters, player1.isEliminated);
            }
        } else if (selectedGameMode == GameMode::RANKED_ESCAPE) {
            updateAIOpponents(dt);
        }

        updateGearSystem(dt);
        updateProjectiles(dt);
        updateRankings();
        updateAshParticles(dt);
        updateWorldParticles(dt);
        updateCelebration(dt);

        {
            const SeismicState &ss = hazardSystem.getSeismicState();
            cameraShakeX = ss.shakeX;
            cameraShakeY = ss.shakeY;
            horizonPitchOffset = ss.pitchOffset;
            if (damageFlashTimer > 0.0) {
                damageFlashTimer = std::max(0.0, damageFlashTimer - dt);
                cameraShakeX += ((rand() % 9) - 4) * damageFlashTimer * 1.6;
                cameraShakeY += ((rand() % 9) - 4) * damageFlashTimer * 1.6;
            }
            if (hitRippleTimer > 0.0) {
                hitRippleTimer = std::max(0.0, hitRippleTimer - dt);
            }
            if (lightningFlashTimer > 0.0) {
                lightningFlashTimer = std::max(0.0, lightningFlashTimer - dt);
            } else if ((rand() % 420) == 0) {
                lightningFlashTimer = 0.22;
            }
        }

        renderSky();
        renderGroundMode7();
        renderSprites();
        renderAshOverlay();
        renderSpeedLines();
        renderCockpit();
        renderWindshieldCracks();
        renderMinimap();
        renderGearHUD();
        renderEscapeHUD();
        if (isCelebrationActive) {
            renderCelebration();
        }
        renderWipeTransition();

        {
            double deg = std::fmod(kartAngle * 180.0 / M_PI + 360.0, 360.0);
            int totalRacers = isNetworkRace ? static_cast<int>(remoteKarts.size() + 1)
                              : (selectedGameMode == GameMode::RANKED_ESCAPE ? static_cast<int>(aiOpponents.size() + 1) : 1);
            emit statsUpdated(kartSpeed, lastCheckpointReached + 1, kartX, kartZ, deg, currentSurfaceName);
            emit survivalTelemetryUpdated(playerHull.integrity, evacTimer, playerPlacement, totalRacers,
                                          static_cast<int>(splineProgress * 100.0),
                                          GarageSystem::instance().getScrapBalance(),
                                          currentDistrictName);
        }
        break;

    case GameState::RACE_FINISHED:
        updateAshParticles(dt);
        updateWorldParticles(dt);
        updateCelebration(dt);
        renderSky();
        renderGroundMode7();
        renderSprites();
        renderAshOverlay();
        renderCockpit();
        renderWindshieldCracks();
        if (isNetworkRace) {
            renderMultiplayerRankings();
        } else {
            renderRaceFinishedOverlay();
        }
        renderWipeTransition();
        break;
    }

    this->update();
}

void GameCanvas::applyCraterStamps()
{
    auto &stamps = hazardSystem.getCraterStamps();
    bool anyNew = false;
    for (auto &stamp : stamps) {
        if (stamp.stamped) continue;
        stamp.stamped = true;
        anyNew = true;

        QPainter vp(&trackVisualMap);
        vp.setPen(Qt::NoPen);
        vp.setBrush(QColor(18, 15, 14, 225));
        vp.drawEllipse(QPointF(stamp.x, stamp.z), stamp.radius, stamp.radius * 0.85);
        vp.setBrush(QColor(8, 6, 6, 240));
        vp.drawEllipse(QPointF(stamp.x, stamp.z), stamp.radius * 0.55, stamp.radius * 0.45);
        vp.end();

        QPainter mp(&trackMaskMap);
        mp.setPen(Qt::NoPen);
        mp.setBrush(QColor::fromRgba(KolkataMap::MASK_OFFROAD));
        mp.drawEllipse(QPointF(stamp.x, stamp.z), stamp.radius * 0.75, stamp.radius * 0.75);
        mp.end();

        // Spawn 3D concrete dust & ember burst at girder impact
        for (int i = 0; i < 18; ++i) {
            double ang = (rand() % 360) * M_PI / 180.0;
            double spd = 15.0 + (rand() % 35);
            bool ember = (i % 3 == 0);
            spawnWorldParticle(stamp.x, 2.0, stamp.z,
                               std::cos(ang) * spd, 10.0 + (rand() % 18), std::sin(ang) * spd,
                               0.8 + (rand() % 6) * 0.15, ember ? 3.5 : 8.0,
                               ember ? qRgb(255, 145, 30) : qRgb(115, 108, 102),
                               ember ? WorldParticleKind::EMBER : WorldParticleKind::DUST);
        }
    }
    if (anyNew) updateMinimapTrackCache();
}

void GameCanvas::updateEvacTimer(double dt)
{
    if (isRaceFinished) return;
    evacTimer -= dt;
    if (evacTimer <= 0.0) {
        evacTimer = 0.0;
        finishRun(false, "EVACUATION WINDOW EXPIRED (00:00)");
    }
}

void GameCanvas::updateCollapseWave(double dt)
{
    if (isRaceFinished) return;
    collapseWaveProgress += collapseWaveSpeed * dt;
    if (collapseWaveProgress > splineProgress + 0.015 && splineProgress > 0.04) {
        playerHull.takeDamage(16.0 * dt, armorRating);
        damageFlashTimer = 0.25;
        if (playerHull.isWrecked()) {
            finishRun(false, "SWALLOWED BY COLLAPSING SECTOR");
        }
    }
}

void GameCanvas::finishRun(bool survived, const QString &reason)
{
    if (isRaceFinished) return;

    if (!survived) {
        isRaceFinished = true;
        didSurviveRun = false;
        finishReason = reason;
        player1.raceFinished = true;
        player1.isEliminated = true;
        player1.finishTime = 9999.0;
        updateRankings();
        GarageSystem::instance().addScrap(25 + scrapCollectedThisRun / 2);
        if (isNetworkRace && networkManager) {
            networkManager->sendEliminated();
            networkManager->notifyLocalFinish(9999.0);
        }
        currentState = GameState::RACE_FINISHED;
        triggerCelebration("VEHICLE LOST", reason);
        return;
    }

    // Continuous 3-stage campaign progression!
    if (currentStage != KolkataStage::HOWRAH_BRIDGE) {
        // Stage 1 -> Stage 2, or Stage 2 -> Stage 3 via Relay Pit-Stop!
        GarageSystem::instance().addScrap(150 + scrapCollectedThisRun);
        enterCheckpointPit(static_cast<int>(currentStage) + 10, true);
        return;
    }

    // Cleared Stage 3 (Howrah Bridge) -> Full Kolkata Evacuation Victory!
    isRaceFinished = true;
    didSurviveRun = true;
    finishReason = "ALL 3 SECTORS CLEARED — WEST BANK REACHED!";
    player1.raceFinished = true;
    player1.finishTime = raceElapsedTime;
    player1.isEliminated = false;
    updateRankings();

    GarageSystem &garage = GarageSystem::instance();
    garage.currentHull() = playerHull;
    garage.awardRaceScrap(playerPlacement, scrapCollectedThisRun + 250);
    activeCheckpointSave.valid = false;

    if (isNetworkRace && networkManager) {
        networkManager->notifyLocalFinish(raceElapsedTime);
    }

    currentState = GameState::RACE_FINISHED;
    triggerCelebration("KOLKATA EVACUATED!",
                       QString("RANK #%1 • TIME %2s").arg(playerPlacement).arg(QString::number(raceElapsedTime, 'f', 1)));
}

QString GameCanvas::determineDistrictName() const
{
    if (currentStage == KolkataStage::GARIAHAT_COLLAPSE) {
        if (splineProgress < 0.28) return "GARIAHAT CROSSING";
        if (splineProgress < 0.55) return "GOLPARK / RASHBEHARI";
        if (splineProgress < 0.80) return "BALLYGUNGE PHARI";
        return "PARK CIRCUS / AJC BOSE";
    } else if (currentStage == KolkataStage::PARK_STREET_RUINS) {
        if (splineProgress < 0.28) return "PARK STREET RUINS";
        if (splineProgress < 0.55) return "CHOWRINGHEE / ESPLANADE";
        if (splineProgress < 0.80) return "VICTORIA MEMORIAL / MAIDAN";
        return "SHAHEED MINAR / PRINSEP";
    } else {
        if (splineProgress < 0.28) return "BBD BAGH / WRITERS'";
        if (splineProgress < 0.55) return "STRAND ROAD DOCKS";
        if (splineProgress < 0.84) return "HOWRAH BRIDGE CANTILEVER";
        return "WEST BANK EVAC BUNKER";
    }
}

void GameCanvas::triggerCheckpoints(double oldX, double oldZ, double newX, double newZ)
{
    auto segmentsIntersect = [](double ax, double az, double bx, double bz,
                                double cx, double cz, double dx, double dz) -> bool {
        auto cross = [](double x1, double y1, double x2, double y2) {
            return x1 * y2 - y1 * x2;
        };
        double d1 = cross(dx - cx, dz - cz, ax - cx, az - cz);
        double d2 = cross(dx - cx, dz - cz, bx - cx, bz - cz);
        double d3 = cross(bx - ax, bz - az, cx - ax, cz - az);
        double d4 = cross(bx - ax, bz - az, dx - ax, dz - az);
        return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
               ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
    };

    for (const auto &cp : stageCheckpoints) {
        if (cp.index == lastCheckpointReached + 1) {
            bool crossedSeg = segmentsIntersect(oldX, oldZ, newX, newZ, cp.x1, cp.z1, cp.x2, cp.z2);
            double midX = 0.5 * (cp.x1 + cp.x2);
            double midZ = 0.5 * (cp.z1 + cp.z2);
            double distMid = std::hypot(newX - midX, newZ - midZ);
            if (crossedSeg || distMid < 54.0) {
                lastCheckpointReached = cp.index;
                nextCheckpoint = cp.index + 1;
                evacTimer = std::min(evacTimerMax, evacTimer + cp.timeBonus);
                lastSafeX = newX;
                lastSafeZ = newZ;
                lastSafeAngle = kartAngle;
                saveCheckpointSnapshot(cp.index);

                if (isNetworkRace) {
                    // Synchronized Multiplayer Checkpoint Wait!
                    enterCheckpointPit(cp.index, false);
                } else {
                    checkpointPromptTimer = 12.0;
                    triggerCelebration(QString("CHECKPOINT %1 SAVED").arg(cp.index),
                                       QString("+%1s • PRESS [P] FOR PIT-STOP").arg(static_cast<int>(cp.timeBonus)));
                }
            }
        }
    }
}

void GameCanvas::updatePhysics(double dt)
{
    if (dt > 0.05) dt = 0.05;
    if (isRaceFinished) return;

    if (isSpinning) {
        spinTimer -= dt;
        if (spinTimer <= 0.0) {
            spinTimer = 0.0;
            isSpinning = false;
        }
        isDrifting = false;
        driftTimer = 0.0;
        lateralSlipAngle = 0.0;
        bodyRoll *= 0.9;
        driftSparks.clear();

        kartAngle += 11.0 * dt;
        kartSpeed = std::max(0.0, kartSpeed - 135.0 * dt);
        kartX = std::clamp(kartX + kartSpeed * std::cos(kartAngle) * dt, 16.0, static_cast<double>(TRACK_MAP_SIZE - 16));
        kartZ = std::clamp(kartZ + kartSpeed * std::sin(kartAngle) * dt, 16.0, static_cast<double>(TRACK_MAP_SIZE - 16));
        currentSurfaceName = "LOSS OF TRACTION!";
        return;
    }

    if (playerHull.isOnFire) {
        playerHull.fireTimer -= dt;
        playerHull.takeDamage(3.8 * dt, armorRating);
        if (playerHull.fireTimer <= 0.0) {
            playerHull.isOnFire = false;
            playerHull.fireTimer = 0.0;
        }
        if (playerHull.isWrecked()) {
            finishRun(false, "CHASSIS CONSUMED BY FIRE");
            return;
        }
    }

    if (miniTurboTimer > 0.0) miniTurboTimer = std::max(0.0, miniTurboTimer - dt);
    if (nitrousBoostTimer > 0.0) nitrousBoostTimer = std::max(0.0, nitrousBoostTimer - dt);
    if (dozerRamTimer > 0.0) dozerRamTimer = std::max(0.0, dozerRamTimer - dt);

    bool holdingSpace = pressedKeys.count(Qt::Key_Space) > 0;
    if (holdingSpace && nitrousFuel > 0.0) {
        nitrousActive = true;
        nitrousFuel = std::max(0.0, nitrousFuel - 25.0 * dt);
        if (nitrousFuel <= 0.0) nitrousActive = false;
    } else if (!holdingSpace) {
        nitrousActive = false;
        nitrousFuel = std::min(nitrousMaxFuel, nitrousFuel + 2.8 * dt);
    }

    int sampleU = std::clamp(static_cast<int>(kartX), 0, TRACK_MAP_SIZE - 1);
    int sampleV = std::clamp(static_cast<int>(kartZ), 0, TRACK_MAP_SIZE - 1);
    QRgb maskColor = trackMaskMap.pixel(sampleU, sampleV);
    int mr = qRed(maskColor), mg = qGreen(maskColor), mb = qBlue(maskColor);

    double currentMaxSpeed = maxSpeed;
    double currentDrag = friction;
    double currentAccel = accel;

    if (miniTurboTimer > 0.0) {
        currentMaxSpeed *= 1.28;
        currentAccel *= 1.65;
    }
    if (nitrousActive || nitrousBoostTimer > 0.0) {
        currentMaxSpeed = maxSpeed * 1.48;
        currentAccel = accel * 2.0;
    }

    bool isWall       = (mr > 180 && mg < 80  && mb < 80);
    bool isLethal     = (mr > 180 && mb > 180 && mg < 80);
    bool isCheckpoint = (mr > 180 && mg > 180 && mb < 80);
    bool isTram       = (mb > 180 && mr < 80  && mg < 80);
    bool isOffroad    = (mg > 180 && mr < 80  && mb < 80);

    if (isLethal) {
        playerHull.takeDamage(18.0, armorRating);
        damageFlashTimer = 0.65;
        hitRippleTimer = 0.55;
        if (playerHull.isWrecked()) {
            finishRun(false, "LOST IN SINKHOLE / HOOGHLY");
            return;
        }
        kartX = lastSafeX;
        kartZ = lastSafeZ;
        kartAngle = lastSafeAngle;
        kartSpeed = 28.0;
        currentSurfaceName = "BRIDGE / SINKHOLE RECOVERY";
        return;
    } else if (isTram) {
        currentSurfaceName = "Steel Tram Rails";
        currentMaxSpeed *= std::min(1.0, 0.88 + offroadGripBonus * 0.5);
        if (std::abs(kartSpeed) > 40.0 && (rand() % 3 == 0)) {
            spawnWorldParticle(kartX, 1.0, kartZ, ((rand() % 20) - 10), 8.0, ((rand() % 20) - 10),
                               0.35, 3.0, qRgb(130, 225, 255), WorldParticleKind::SPARK);
        }
    } else if (isOffroad) {
        currentSurfaceName = "Rubble & Debris";
        if (!nitrousActive && nitrousBoostTimer <= 0.0 && dozerRamTimer <= 0.0) {
            currentMaxSpeed *= std::min(0.88, 0.55 + offroadGripBonus);
            currentDrag *= std::max(1.1, 1.9 - offroadGripBonus * 1.8);
        }
    } else if (isCheckpoint) {
        currentSurfaceName = "Relay Checkpoint";
    } else {
        currentSurfaceName = (nitrousActive || nitrousBoostTimer > 0.0) ? "NITROUS OVERDRIVE"
                           : (dozerRamTimer > 0.0) ? "RAM SHIELD ENGAGED"
                           : (isDrifting ? "Asphalt (POWERSLIDE)" : "Cracked Asphalt");
        if (!isWall && kartSpeed > 15.0) {
            lastSafeX = kartX;
            lastSafeZ = kartZ;
            lastSafeAngle = kartAngle;
        }
    }

    bool forward  = pressedKeys.count(Qt::Key_W) || pressedKeys.count(Qt::Key_Up);
    bool backward = pressedKeys.count(Qt::Key_S) || pressedKeys.count(Qt::Key_Down);
    bool left     = pressedKeys.count(Qt::Key_A) || pressedKeys.count(Qt::Key_Left);
    bool right    = pressedKeys.count(Qt::Key_D) || pressedKeys.count(Qt::Key_Right);
    bool driftKey = pressedKeys.count(Qt::Key_Shift) > 0;

    int steerDirection = 0;
    if (left && !right) steerDirection = -1;
    else if (right && !left) steerDirection = 1;

    if (forward) {
        double ratio = (currentMaxSpeed > 0.0) ? (kartSpeed / currentMaxSpeed) : 0.0;
        double accelEff = currentAccel * (1.0 - 0.35 * std::clamp(ratio, 0.0, 1.0));
        kartSpeed = std::min(currentMaxSpeed, kartSpeed + accelEff * dt);
    } else if (backward) {
        kartSpeed = std::max(-0.35 * maxSpeed, kartSpeed - brakeDecel * dt);
    } else {
        if (kartSpeed > 0.0) kartSpeed = std::max(0.0, kartSpeed - currentDrag * dt);
        else if (kartSpeed < 0.0) kartSpeed = std::min(0.0, kartSpeed + currentDrag * dt);
    }

    // Handbrake Powerslide (NO cute kart hop — pure body roll & rear tire smoke!)
    if (driftKey && steerDirection != 0 && kartSpeed > 38.0) {
        if (!isDrifting) {
            isDrifting = true;
            driftDirection = steerDirection;
            driftTimer = 0.0;
        }
        driftTimer += dt;
        double driftTurnMult = this->driftFactor * ((steerDirection == driftDirection) ? 1.22 : 0.48);
        kartAngle += driftDirection * this->turnRate * driftTurnMult * dt;
        double targetBeta = driftDirection * 0.25 * this->driftFactor;
        lateralSlipAngle += (targetBeta - lateralSlipAngle) * 8.0 * dt;
        updateDriftSparks(dt);

        if ((rand() % 2) == 0) {
            spawnWorldParticle(kartX, 1.5, kartZ, ((rand() % 14) - 7), 6.0, ((rand() % 14) - 7),
                               0.5, 6.0, qRgb(95, 92, 90), WorldParticleKind::SMOKE);
        }
    } else {
        if (isDrifting) {
            if (driftTimer >= 0.85) {
                miniTurboTimer = 1.0;
                kartSpeed = std::min(currentMaxSpeed * 1.28, kartSpeed + 20.0);
                flashIntensity = 0.22;
            }
            isDrifting = false;
            driftTimer = 0.0;
            driftDirection = 0;
            driftSparks.clear();
        }
        lateralSlipAngle += (0.0 - lateralSlipAngle) * 12.0 * dt;
        double steerSpeedFactor = (std::abs(kartSpeed) > 1.0)
            ? std::clamp(std::abs(kartSpeed) / 48.0, 0.72, 1.28) : 0.0;
        kartAngle += steerDirection * this->turnRate * steerSpeedFactor * dt;
    }

    // Body roll banking in turns (used by scanline horizon/ground tilt)
    double targetRoll = (isDrifting ? driftDirection * 0.045 : steerDirection * 0.022) * std::clamp(kartSpeed / 120.0, 0.0, 1.0);
    bodyRoll += (targetRoll - bodyRoll) * 8.0 * dt;

    double targetWheelAngle = isDrifting ? (driftDirection * 0.55) : (steerDirection * 0.40);
    steeringVisualAngle += (targetWheelAngle - steeringVisualAngle) * 10.0 * dt;

    while (kartAngle >= 2.0 * M_PI) kartAngle -= 2.0 * M_PI;
    while (kartAngle < 0.0) kartAngle += 2.0 * M_PI;

    double oldX = kartX, oldZ = kartZ;
    double travelAngle = kartAngle - lateralSlipAngle;
    double nextX = std::clamp(kartX + kartSpeed * std::cos(travelAngle) * dt, 16.0, static_cast<double>(TRACK_MAP_SIZE - 16));
    double nextZ = std::clamp(kartZ + kartSpeed * std::sin(travelAngle) * dt, 16.0, static_cast<double>(TRACK_MAP_SIZE - 16));

    int nu = std::clamp(static_cast<int>(nextX), 0, TRACK_MAP_SIZE - 1);
    int nv = std::clamp(static_cast<int>(nextZ), 0, TRACK_MAP_SIZE - 1);
    QRgb nextMask = trackMaskMap.pixel(nu, nv);
    if (qRed(nextMask) > 180 && qGreen(nextMask) < 80 && qBlue(nextMask) < 80) {
        auto isWallAt = [&](int px, int pz) -> double {
            px = std::clamp(px, 0, TRACK_MAP_SIZE - 1);
            pz = std::clamp(pz, 0, TRACK_MAP_SIZE - 1);
            QRgb c = trackMaskMap.pixel(px, pz);
            return (qRed(c) > 180 && qGreen(c) < 80 && qBlue(c) < 80) ? 1.0 : 0.0;
        };
        double nx = isWallAt(nu - 4, nv) - isWallAt(nu + 4, nv);
        double nz = isWallAt(nu, nv - 4) - isWallAt(nu, nv + 4);
        double nLen = std::hypot(nx, nz);
        if (nLen > 0.01) {
            kartX = std::clamp(nextX + (nx / nLen) * 4.5, 16.0, static_cast<double>(TRACK_MAP_SIZE - 16));
            kartZ = std::clamp(nextZ + (nz / nLen) * 4.5, 16.0, static_cast<double>(TRACK_MAP_SIZE - 16));
        } else {
            kartX = oldX;
            kartZ = oldZ;
        }
        if (kartSpeed > 45.0) {
            playerHull.takeDamage(2.0, armorRating);
            damageFlashTimer = 0.22;
            spawnWorldParticle(kartX, 3.0, kartZ, 0.0, 12.0, 0.0, 0.35, 3.0, qRgb(255, 200, 75), WorldParticleKind::SPARK);
        }
        kartSpeed *= 0.68;
    } else {
        kartX = nextX;
        kartZ = nextZ;
    }

    if (dozerRamTimer <= 0.0) {
        CollisionResult cr = hazardSystem.checkPlayerCollision(kartX, kartZ, kartAngle, kartSpeed, playerHull);
        if (cr.hit) {
            kartSpeed *= cr.speedMultiplier;
            kartX = std::clamp(kartX + cr.pushX, 16.0, static_cast<double>(TRACK_MAP_SIZE - 16));
            kartZ = std::clamp(kartZ + cr.pushZ, 16.0, static_cast<double>(TRACK_MAP_SIZE - 16));
            if (cr.spinOut) {
                isSpinning = true;
                spinTimer = cr.spinDuration;
            }
            if (cr.resetToCheckpoint) {
                kartX = lastSafeX;
                kartZ = lastSafeZ;
                kartAngle = lastSafeAngle;
                evacTimer = std::max(0.0, evacTimer - 1.5);
            }
            if (cr.damageDealt > 0.0) {
                damageFlashTimer = 0.55;
                hitRippleTimer = 0.45;
                hazardSystem.triggerQuake(0.5);
                for (int i = 0; i < 10; ++i) {
                    spawnWorldParticle(kartX, 3.0, kartZ, ((rand() % 40) - 20), 14.0 + (rand() % 14), ((rand() % 40) - 20),
                                       0.5, 4.0, qRgb(255, 160, 40), WorldParticleKind::SPARK);
                }
            }
            if (playerHull.isWrecked()) {
                finishRun(false, "CHASSIS DESTROYED BY WRECKAGE");
                return;
            }
        }
    }

    // Direct grounded tactical pickups (ZERO Mario roulette)
    for (auto &sprite : worldSprites) {
        if (!sprite.active) continue;
        double dx = kartX - sprite.x;
        double dz = kartZ - sprite.z;
        double distSq = dx * dx + dz * dz;

        if (SpriteAtlas::instance().isPickup(sprite.type)) {
            if (distSq < 22.0 * 22.0) {
                sprite.active = false;
                sprite.respawnTimer = 18.0;
                if (sprite.type == SpriteType::REPAIR_KIT || sprite.type == SpriteType::ROCKET) {
                    double heal = (player1.vehicleId == 3) ? 35.0 : 25.0;
                    playerHull.repair(heal);
                    playerHull.isOnFire = false;
                    flashIntensity = 0.22;
                } else if (sprite.type == SpriteType::NITROUS_CELL || sprite.type == SpriteType::MUSHROOM) {
                    nitrousFuel = std::min(nitrousMaxFuel, nitrousFuel + 50.0);
                    nitrousBoostTimer = 1.2 * (1.0 + GarageSystem::instance().upgrades().nitrousLevel * 0.15);
                } else if (sprite.type == SpriteType::SCRAP_CACHE || sprite.type == SpriteType::COIN) {
                    int gain = 45 + (rand() % 45);
                    scrapCollectedThisRun += gain;
                    GarageSystem::instance().addScrap(gain);
                } else if (sprite.type == SpriteType::SUPPLY_CRATE || sprite.type == SpriteType::QUESTION_BOX) {
                    // Direct tactical equipment grant — Dozer Ram or Shockwave Flare
                    if (heldGear == SurvivalGear::NONE) {
                        heldGear = (rand() % 2 == 0) ? SurvivalGear::DOZER_RAM : SurvivalGear::SHOCKWAVE_FLARE;
                    } else {
                        int gain = 40;
                        scrapCollectedThisRun += gain;
                        GarageSystem::instance().addScrap(gain);
                    }
                }
            }
        } else if (SpriteAtlas::instance().isObstacle(sprite.type)) {
            double hitR = 14.0;
            if (distSq < hitR * hitR) {
                if (dozerRamTimer > 0.0) {
                    sprite.active = false;
                    scrapCollectedThisRun += 30;
                    hazardSystem.triggerQuake(0.35);
                } else {
                    double dist = std::max(0.01, std::sqrt(distSq));
                    kartX = std::clamp(sprite.x + (dx / dist) * (hitR + 4.0), 16.0, static_cast<double>(TRACK_MAP_SIZE - 16));
                    kartZ = std::clamp(sprite.z + (dz / dist) * (hitR + 4.0), 16.0, static_cast<double>(TRACK_MAP_SIZE - 16));
                    kartSpeed = -22.0;
                    playerHull.takeDamage(12.0, armorRating);
                    damageFlashTimer = 0.45;
                    hitRippleTimer = 0.40;
                    if (playerHull.isWrecked()) {
                        finishRun(false, "WRECKED ON KOLKATA DEBRIS");
                        return;
                    }
                }
            }
        }
    }

    splineProgress = computeSplineProgress(kartX, kartZ, &progressMeters);
    currentDistrictName = determineDistrictName();

    player1.x = kartX;
    player1.z = kartZ;
    player1.speed = kartSpeed;
    player1.angle = kartAngle;
    player1.isDrifting = isDrifting;
    player1.driftTimer = driftTimer;
    player1.miniTurboTimer = miniTurboTimer;
    player1.lateralSlip = lateralSlipAngle;
    player1.hull = playerHull;
    player1.splineProgress = splineProgress;
    player1.progressMeters = progressMeters;
    player1.lastCheckpointReached = lastCheckpointReached;
    player1.totalRaceDistance += std::abs(kartSpeed) * dt;

    triggerCheckpoints(oldX, oldZ, kartX, kartZ);

    if (!currentWaypoints.empty()) {
        const QPointF &goal = currentWaypoints.back();
        double distToGoal = std::hypot(kartX - goal.x(), kartZ - goal.y());
        if ((splineProgress >= 0.985 || (distToGoal < 58.0 && splineProgress > 0.85))) {
            finishRun(true, "EXTRACTION GATE REACHED!");
        }
    }
}

void GameCanvas::updateAIOpponents(double dt)
{
    if (aiOpponents.empty() || currentWaypoints.size() < 2) return;
    dt = std::min(dt, 0.05);

    double stageSpeedScale = (currentStage == KolkataStage::GARIAHAT_COLLAPSE) ? 0.82
                           : (currentStage == KolkataStage::PARK_STREET_RUINS) ? 0.90 : 0.96;

    for (auto &ai : aiOpponents) {
        if (ai.raceFinished || ai.isEliminated) continue;

        if (ai.isSpinning) {
            ai.spinTimer -= dt;
            if (ai.spinTimer <= 0.0) { ai.spinTimer = 0.0; ai.isSpinning = false; }
            ai.angle += 10.0 * dt;
            ai.speed = std::max(0.0, ai.speed - 120.0 * dt);
            ai.x += ai.speed * std::cos(ai.angle) * dt;
            ai.z += ai.speed * std::sin(ai.angle) * dt;
            continue;
        }

        int wpIdx = std::clamp(ai.currentWaypointIndex, 0, static_cast<int>(currentWaypoints.size()) - 1);
        const QPointF &wp = currentWaypoints[wpIdx];
        if (std::hypot(wp.x() - ai.x, wp.y() - ai.z) < 65.0 && wpIdx + 1 < static_cast<int>(currentWaypoints.size())) {
            wpIdx++;
            ai.currentWaypointIndex = wpIdx;
            ai.currentWaypoint = wpIdx;
        }

        int targetIdx = std::min(wpIdx + 1, static_cast<int>(currentWaypoints.size()) - 1);
        QPointF targetPt = currentWaypoints[targetIdx];

        double lookX = ai.x + std::cos(ai.angle) * 90.0;
        double lookZ = ai.z + std::sin(ai.angle) * 90.0;
        double avoidOffset = hazardSystem.getHazardAvoidanceOffset(ai.x, ai.z, lookX, lookZ);
        double perpX = -std::sin(ai.angle);
        double perpZ =  std::cos(ai.angle);
        double steerTargetX = targetPt.x() + perpX * avoidOffset * 26.0;
        double steerTargetZ = targetPt.y() + perpZ * avoidOffset * 26.0;

        double desiredAngle = std::atan2(steerTargetZ - ai.z, steerTargetX - ai.x);
        double angleDiff = desiredAngle - ai.angle;
        while (angleDiff > M_PI)  angleDiff -= 2.0 * M_PI;
        while (angleDiff < -M_PI) angleDiff += 2.0 * M_PI;

        double maxTurn = ai.turnRate * dt;
        ai.angle += std::clamp(angleDiff, -maxTurn, maxTurn);
        ai.steerAngle = std::clamp(angleDiff * 2.0, -1.0, 1.0); // Used for 3-angle sprite leaning!

        double turnSlowdown = (std::abs(angleDiff) > 0.6) ? 0.72 : 1.0;
        double targetSpeed = ai.maxSpeed * stageSpeedScale * turnSlowdown;
        if (ai.speed < targetSpeed) ai.speed = std::min(targetSpeed, ai.speed + ai.accel * dt);
        else ai.speed = std::max(targetSpeed, ai.speed - 90.0 * dt);

        ai.x = std::clamp(ai.x + ai.speed * std::cos(ai.angle) * dt, 16.0, static_cast<double>(TRACK_MAP_SIZE - 16));
        ai.z = std::clamp(ai.z + ai.speed * std::sin(ai.angle) * dt, 16.0, static_cast<double>(TRACK_MAP_SIZE - 16));

        ai.splineProgress = computeSplineProgress(ai.x, ai.z, &ai.progressMeters);
        ai.totalRaceDistance += ai.speed * dt;

        if (ai.splineProgress >= 0.985) {
            ai.raceFinished = true;
            ai.finishTime = raceElapsedTime;
        }
    }
}

void GameCanvas::updateRankings()
{
    std::vector<KartEntity*> all;
    all.push_back(&player1);
    for (auto &ai : aiOpponents) all.push_back(&ai);
    for (auto &rk : remoteKarts) all.push_back(&rk);

    std::sort(all.begin(), all.end(), [](const KartEntity *a, const KartEntity *b) {
        if (a->raceFinished != b->raceFinished) return a->raceFinished;
        if (a->raceFinished && b->raceFinished) {
            if (a->isEliminated != b->isEliminated) return !a->isEliminated;
            return a->finishTime < b->finishTime;
        }
        if (a->isEliminated != b->isEliminated) return !a->isEliminated;
        return a->progressMeters > b->progressMeters;
    });

    for (size_t i = 0; i < all.size(); ++i) {
        all[i]->placement = static_cast<int>(i + 1);
    }
    playerPlacement = player1.placement;
}

void GameCanvas::updateGearSystem(double dt)
{
    for (auto &sprite : worldSprites) {
        if (!sprite.active && sprite.respawnTimer > 0.0) {
            sprite.respawnTimer -= dt;
            if (sprite.respawnTimer <= 0.0) {
                sprite.respawnTimer = 0.0;
                sprite.active = true;
            }
        }
    }
}

void GameCanvas::useHeldGear()
{
    if (heldGear == SurvivalGear::NONE) return;
    SurvivalGear g = heldGear;
    heldGear = SurvivalGear::NONE;

    if (g == SurvivalGear::NITROUS) {
        nitrousFuel = nitrousMaxFuel;
        nitrousBoostTimer = 2.5 * (1.0 + GarageSystem::instance().upgrades().nitrousLevel * 0.15);
    } else if (g == SurvivalGear::REPAIR_KIT) {
        double heal = (player1.vehicleId == 3) ? 35.0 : 25.0;
        playerHull.repair(heal);
        playerHull.isOnFire = false;
    } else if (g == SurvivalGear::DOZER_RAM) {
        dozerRamTimer = 4.5;
        flashIntensity = 0.25;
    } else if (g == SurvivalGear::SHOCKWAVE_FLARE) {
        Projectile p;
        p.x = kartX + std::cos(kartAngle) * 14.0;
        p.z = kartZ + std::sin(kartAngle) * 14.0;
        p.vx = std::cos(kartAngle) * (std::max(140.0, kartSpeed) + 120.0);
        p.vz = std::sin(kartAngle) * (std::max(140.0, kartSpeed) + 120.0);
        p.ownerId = 0;
        p.lifeTime = 3.0;
        p.active = true;
        activeProjectiles.push_back(p);
    }
}

void GameCanvas::updateProjectiles(double dt)
{
    for (auto &p : activeProjectiles) {
        if (!p.active) continue;
        p.lifeTime -= dt;
        if (p.lifeTime <= 0.0) { p.active = false; continue; }
        p.x += p.vx * dt;
        p.z += p.vz * dt;

        for (auto &sprite : worldSprites) {
            if (!sprite.active || !SpriteAtlas::instance().isObstacle(sprite.type)) continue;
            if (std::hypot(p.x - sprite.x, p.z - sprite.z) < 22.0) {
                sprite.active = false;
                p.active = false;
                for (int i = 0; i < 10; ++i) {
                    spawnWorldParticle(p.x, 3.0, p.z, ((rand() % 40) - 20), 12.0, ((rand() % 40) - 20),
                                       0.5, 4.0, qRgb(255, 160, 40), WorldParticleKind::EMBER);
                }
                break;
            }
        }
        for (auto &ai : aiOpponents) {
            if (!ai.isSpinning && std::hypot(p.x - ai.x, p.z - ai.z) < 18.0) {
                ai.isSpinning = true;
                ai.spinTimer = 1.0;
                p.active = false;
                break;
            }
        }
    }
    activeProjectiles.erase(
        std::remove_if(activeProjectiles.begin(), activeProjectiles.end(),
                       [](const Projectile &p) { return !p.active; }),
        activeProjectiles.end());
}

void GameCanvas::updateDriftSparks(double dt)
{
    for (auto it = driftSparks.begin(); it != driftSparks.end();) {
        it->x += it->vx * dt;
        it->y += it->vy * dt;
        it->vy += 120.0 * dt;
        it->life -= dt;
        if (it->life <= 0.0) it = driftSparks.erase(it);
        else ++it;
    }

    if (isDrifting && driftSparks.size() < 45) {
        QRgb sparkCol = (driftTimer >= 0.85) ? qRgb(255, 140, 20) : qRgb(190, 200, 215);
        for (int i = 0; i < 2; ++i) {
            SparkParticle sp;
            sp.x = (BUFFER_WIDTH / 2) + ((i == 0) ? -45.0 : 45.0) + (rand() % 12 - 6);
            sp.y = BUFFER_HEIGHT - 12 - (rand() % 6);
            sp.vx = -driftDirection * (35.0 + (rand() % 45));
            sp.vy = -(22.0 + (rand() % 35));
            sp.maxLife = 0.28;
            sp.life = sp.maxLife;
            sp.color = sparkCol;
            driftSparks.push_back(sp);
        }
    }
}

void GameCanvas::spawnWorldParticle(double x, double y, double z, double vx, double vy, double vz,
                                    double life, double size, QRgb color, WorldParticleKind kind)
{
    if (worldParticles.size() >= 140) return;
    WorldParticle wp;
    wp.x = x; wp.y = y; wp.z = z;
    wp.vx = vx; wp.vy = vy; wp.vz = vz;
    wp.life = life; wp.maxLife = life;
    wp.baseSize = size;
    wp.color = color;
    wp.kind = kind;
    worldParticles.push_back(wp);
}

void GameCanvas::updateWorldParticles(double dt)
{
    // Emit rising smoke & embers from nearby burning hazards
    for (const auto &hz : hazardSystem.getTrackHazards()) {
        if (hz.type == HazardType::BURNING_TAXI || hz.type == HazardType::BURNING_AUTO ||
            (hz.type == HazardType::GAS_VENT && hz.isErupting)) {
            if (std::hypot(hz.x - kartX, hz.z - kartZ) < 420.0 && (rand() % 4 == 0)) {
                bool isEmber = (rand() % 3 == 0);
                spawnWorldParticle(hz.x + ((rand() % 10) - 5), 10.0, hz.z + ((rand() % 10) - 5),
                                   ((rand() % 12) - 6), 14.0 + (rand() % 12), ((rand() % 12) - 6),
                                   isEmber ? 0.75 : 1.35, isEmber ? 2.5 : 7.0,
                                   isEmber ? qRgb(255, 165, 35) : qRgb(42, 40, 40),
                                   isEmber ? WorldParticleKind::EMBER : WorldParticleKind::SMOKE);
            }
        }
    }

    for (auto it = worldParticles.begin(); it != worldParticles.end();) {
        it->life -= dt;
        if (it->life <= 0.0) {
            it = worldParticles.erase(it);
            continue;
        }
        it->x += it->vx * dt;
        it->y += it->vy * dt;
        it->z += it->vz * dt;
        if (it->kind == WorldParticleKind::SPARK) it->vy -= 40.0 * dt;
        ++it;
    }
}

// -------------------------------------------------------------
// ANIMATED PIXEL-ART BOOT STORY CUTSCENE (4 SCENES)
// -------------------------------------------------------------

void GameCanvas::renderStoryIntro()
{
    QPainter painter(&screenBuffer);
    painter.setRenderHint(QPainter::Antialiasing, false);

    double t = storyTimer;
    int scene = (t < 5.5) ? 0 : (t < 11.5 ? 1 : (t < 17.5 ? 2 : 3));

    // Base dark sky
    QColor skyTop(5, 5, 10);
    QColor skyBot = (scene == 0) ? QColor(14, 18, 32)
                  : (scene == 1 ? QColor(32, 14, 16) : QColor(52, 16, 8));
    QLinearGradient sg(0, 0, 0, 210);
    sg.setColorAt(0.0, skyTop);
    sg.setColorAt(1.0, skyBot);
    painter.fillRect(0, 0, BUFFER_WIDTH, 210, sg);

    // Subtle seismic shake in scenes 1 & 2
    int sx = 0, sy = 0;
    if (scene == 1 || scene == 2) {
        double amp = (scene == 1) ? 2.5 : 4.0;
        sx = static_cast<int>(std::sin(t * 38.0) * amp);
        sy = static_cast<int>(std::cos(t * 45.0) * amp * 0.6);
    }

    painter.save();
    painter.translate(sx, sy);

    // Distant Kolkata skyline silhouettes
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(10, 10, 14));
    painter.drawRect(30, 110, 18, 100);   // The 42
    painter.drawRect(80, 145, 45, 65);
    painter.drawRect(160, 130, 28, 80);   // Victoria Memorial base
    painter.drawEllipse(156, 114, 36, 30);
    painter.drawRect(240, 150, 55, 60);
    painter.drawRect(320, 118, 10, 92);   // Howrah tower L
    painter.drawRect(365, 118, 10, 92);   // Howrah tower R

    // Scene 0: Calm pre-fault Gariahat night (lights snapping, taxi cruising)
    if (scene == 0) {
        // City lights grid (all lit)
        for (int bx = 86; bx < 290; bx += 12) {
            for (int by = 152; by < 195; by += 10) {
                if ((bx + by) % 3 == 0) painter.fillRect(bx, by, 4, 4, QColor(245, 185, 65));
            }
        }
        // Road & sodium streetlamps
        painter.fillRect(0, 200, BUFFER_WIDTH, 35, QColor(24, 24, 28));
        for (int lx = 20; lx < BUFFER_WIDTH; lx += 85) {
            painter.fillRect(lx, 155, 3, 45, QColor(45, 45, 50));
            painter.fillRect(lx - 2, 153, 8, 3, QColor(255, 190, 50));
        }
        // Moving Kaali-Peeli taxi silhouette
        int tx = static_cast<int>(std::fmod(t * 55.0, 440.0)) - 40;
        painter.fillRect(tx, 192, 34, 11, QColor(235, 185, 20));
        painter.fillRect(tx + 7, 185, 20, 7, QColor(22, 22, 24));
        painter.setBrush(QColor(255, 235, 150, 90));
        QPolygon cone;
        cone << QPoint(tx + 34, 196) << QPoint(tx + 95, 184) << QPoint(tx + 95, 208);
        painter.drawPolygon(cone);
    }
    // Scene 1: Subsidence fault tears open, grid blacks out block by block, lightning!
    else if (scene == 1) {
        double p = (t - 5.5) / 6.0;
        int blackoutX = static_cast<int>(p * BUFFER_WIDTH);
        for (int bx = 86; bx < 290; bx += 12) {
            for (int by = 152; by < 195; by += 10) {
                if (bx > blackoutX && (bx + by) % 3 == 0) {
                    painter.fillRect(bx, by, 4, 4, QColor(245, 185, 65));
                }
            }
        }
        // Branching lightning arc
        if (static_cast<int>(t * 10.0) % 4 == 0) {
            painter.setPen(QPen(QColor(210, 235, 255), 2));
            painter.drawLine(190, 0, 175, 55);
            painter.drawLine(175, 55, 205, 105);
            painter.drawLine(205, 105, 185, 150);
        }
        // Cracking road fissure
        painter.fillRect(0, 200, BUFFER_WIDTH, 35, QColor(22, 20, 22));
        painter.setPen(QPen(QColor(255, 110, 25), 2));
        int crackLen = static_cast<int>(p * 180);
        painter.drawLine(200 - crackLen, 215, 200, 210);
        painter.drawLine(200, 210, 200 + crackLen, 222);
    }
    // Scene 2: Gariahat Flyover collapses & fire plumes erupt
    else if (scene == 2) {
        double p = (t - 11.5) / 6.0;
        // Flyover deck snapping & dropping
        int dropY = static_cast<int>(std::min(1.0, p * 1.4) * 58.0);
        painter.fillRect(30, 138, 120, 10, QColor(70, 68, 66));
        painter.save();
        painter.translate(150, 142 + dropY);
        painter.rotate(std::min(28.0, p * 42.0));
        painter.fillRect(0, -5, 140, 12, QColor(85, 80, 75));
        painter.restore();

        // Fire plumes & rising smoke
        for (int i = 0; i < 14; ++i) {
            int fx = 110 + (i * 17) % 190;
            int fy = 178 - static_cast<int>(std::fmod(t * 35.0 + i * 19.0, 55.0));
            painter.setBrush((i % 2 == 0) ? QColor(255, 125, 25, 190) : QColor(45, 42, 40, 170));
            painter.drawEllipse(fx, fy, 14, 14);
        }
        painter.fillRect(0, 200, BUFFER_WIDTH, 35, QColor(24, 16, 14));
    }
    // Scene 3: Garage shutter rolls up, Arjun's rig headlights flare on
    else {
        double p = (t - 17.5) / 4.5;
        painter.fillRect(0, 200, BUFFER_WIDTH, 35, QColor(24, 16, 14));
        // Garage bay frame & rising corrugated shutter
        int shutterH = static_cast<int>(std::max(0.0, 1.0 - p * 1.5) * 95.0);
        painter.fillRect(110, 110, 180, 95, QColor(14, 12, 16));
        // Headlights shining out from inside bay
        if (p > 0.25) {
            painter.setBrush(QColor(255, 225, 135, 170));
            painter.drawEllipse(152, 176, 16, 12);
            painter.drawEllipse(232, 176, 16, 12);
            QPolygon beamL, beamR;
            beamL << QPoint(160, 182) << QPoint(90, 235) << QPoint(185, 235);
            beamR << QPoint(240, 182) << QPoint(215, 235) << QPoint(310, 235);
            painter.setBrush(QColor(255, 215, 110, 55));
            painter.drawPolygon(beamL);
            painter.drawPolygon(beamR);
        }
        painter.fillRect(110, 110, 180, shutterH, QColor(58, 56, 62));
        for (int y = 114; y < 110 + shutterH; y += 8) {
            painter.fillRect(110, y, 180, 2, QColor(36, 34, 38));
        }
    }

    painter.restore();

    // Bottom FM Radio Subtitle Box with Typewriter Caption
    painter.fillRect(0, 235, BUFFER_WIDTH, 65, QColor(8, 7, 10, 245));
    painter.setPen(QPen(QColor(225, 130, 25), 1));
    painter.drawLine(0, 235, BUFFER_WIDTH, 235);

    QString header, fullLine;
    double localT = 0.0;
    if (scene == 0) {
        header = "02:39 IST — GARIAHAT CROSSING";
        fullLine = "Power grid steady. Arjun Sen's night shift ends—until the alluvial clay beneath Kolkata shifts.";
        localT = t;
    } else if (scene == 1) {
        header = "02:40 IST — BLACKOUT OVER THE HOOGHLY";
        fullLine = "Subsidence fault ruptures! Substation arcs tear across Chowringhee as the city plunges dark.";
        localT = t - 5.5;
    } else if (scene == 2) {
        header = "FM EMERGENCY RELAY — STRUCTURAL FAILURE";
        fullLine = "Gariahat Flyover has sheared! Reach Howrah Bridge before the main cantilever collapses.";
        localT = t - 11.5;
    } else {
        header = "GARAGE BAY 4 — IGNITION";
        fullLine = "No choppers left in the ash storm. Weld the bull-bar, hit the gas, and survive the ruins.";
        localT = t - 17.5;
    }

    int chars = std::min(static_cast<int>(fullLine.length()), static_cast<int>(localT * 34.0));
    painter.setFont(QFont("Courier New", 7, QFont::Bold));
    painter.setPen(QColor(245, 165, 35));
    painter.drawText(QRect(14, 240, BUFFER_WIDTH - 28, 14), Qt::AlignLeft, header);

    painter.setFont(QFont("Arial", 7, QFont::Bold));
    painter.setPen(QColor(225, 222, 218));
    painter.drawText(QRect(14, 256, BUFFER_WIDTH - 28, 28), Qt::AlignLeft | Qt::TextWordWrap, fullLine.left(chars));

    painter.setFont(QFont("Arial", 5, QFont::Bold));
    painter.setPen(QColor(150, 145, 140));
    painter.drawText(QRect(14, 286, BUFFER_WIDTH - 28, 10), Qt::AlignRight, "[SPACE / ENTER] SKIP INTRO");
}

// -------------------------------------------------------------
// ANIMATED SKYBOX, MODE-7 GROUND, FIRELIGHT & 3D PARTICLES
// -------------------------------------------------------------

SpriteType GameCanvas::getVehicleSpriteType(int vehicleId, bool isAlt) const
{
    switch (vehicleId) {
    case 0: return isAlt ? SpriteType::RIG_KAALI_PEELI_ALT : SpriteType::RIG_KAALI_PEELI;
    case 1: return isAlt ? SpriteType::RIG_DOZER_ALT       : SpriteType::RIG_DOZER;
    case 2: return isAlt ? SpriteType::RIG_RUNNER_ALT      : SpriteType::RIG_RUNNER;
    case 3: return isAlt ? SpriteType::RIG_MEDVAN_ALT      : SpriteType::RIG_MEDVAN;
    default: return isAlt ? SpriteType::RIG_KAALI_PEELI_ALT : SpriteType::RIG_KAALI_PEELI;
    }
}

void GameCanvas::renderSky()
{
    QRgb *pixels = reinterpret_cast<QRgb*>(screenBuffer.bits());
    int effHorizon = std::clamp(HORIZON_Y + static_cast<int>(horizonPitchOffset + cameraShakeY * 0.5), 90, 210);

    QColor topSky, botSky;
    if (currentStage == KolkataStage::GARIAHAT_COLLAPSE) {
        topSky = QColor(5, 4, 8);
        botSky = QColor(46, 15, 10);
    } else if (currentStage == KolkataStage::PARK_STREET_RUINS) {
        topSky = QColor(7, 4, 8);
        botSky = QColor(62, 18, 9);
    } else {
        topSky = QColor(4, 3, 6);
        botSky = QColor(78, 20, 8);
    }

    int flashBoost = static_cast<int>(lightningFlashTimer * 260.0);

    // 1. Sky gradient + scrolling parallax storm cloud bands
    int cloudDrift = static_cast<int>(globalTime * 12.0);
    int horizonOffset = static_cast<int>((kartAngle / (2.0 * M_PI)) * BUFFER_WIDTH * 2.0);

    for (int y = 0; y < effHorizon; ++y) {
        double factor = static_cast<double>(y) / std::max(1, effHorizon);
        double glow = factor * factor;
        int baseR = static_cast<int>(topSky.red()   + glow * (botSky.red()   - topSky.red()))   + flashBoost;
        int baseG = static_cast<int>(topSky.green() + glow * (botSky.green() - topSky.green())) + flashBoost;
        int baseB = static_cast<int>(topSky.blue()  + glow * (botSky.blue()  - topSky.blue()))  + flashBoost;

        for (int x = 0; x < BUFFER_WIDTH; ++x) {
            int cx = (x + horizonOffset / 2 + cloudDrift) % BUFFER_WIDTH;
            double cloud = std::sin(cx * 0.035 + y * 0.08) * std::cos(cx * 0.018 - y * 0.04);
            int cloudShade = (y < effHorizon - 22 && cloud > 0.25) ? 14 : 0;

            int r = std::clamp(baseR + cloudShade, 0, 255);
            int g = std::clamp(baseG + cloudShade / 2, 0, 255);
            int b = std::clamp(baseB + cloudShade / 2, 0, 255);
            pixels[y * BUFFER_WIDTH + x] = qRgb(r, g, b);
        }
    }

    // 2. Sweeping Evacuation Searchlights in sky
    double beamAngle1 = std::sin(globalTime * 1.1) * 0.65;
    double beamAngle2 = std::cos(globalTime * 0.85) * 0.55;
    auto drawBeam = [&](int originX, double slope) {
        for (int y = 0; y < effHorizon; ++y) {
            int distFromBot = effHorizon - y;
            int centerX = originX + static_cast<int>(distFromBot * slope);
            int halfW = 2 + distFromBot / 14;
            for (int x = std::max(0, centerX - halfW); x <= std::min(BUFFER_WIDTH - 1, centerX + halfW); ++x) {
                QRgb c = pixels[y * BUFFER_WIDTH + x];
                pixels[y * BUFFER_WIDTH + x] = qRgb(std::min(255, qRed(c) + 18),
                                                    std::min(255, qGreen(c) + 18),
                                                    std::min(255, qBlue(c) + 22));
            }
        }
    };
    drawBeam((320 - horizonOffset % BUFFER_WIDTH + BUFFER_WIDTH) % BUFFER_WIDTH, beamAngle1);
    drawBeam((140 - horizonOffset % BUFFER_WIDTH + BUFFER_WIDTH) % BUFFER_WIDTH, beamAngle2);

    // 3. Branching lightning bolt during flashes
    if (lightningFlashTimer > 0.08) {
        QPainter lp(&screenBuffer);
        lp.setPen(QPen(QColor(225, 240, 255, 230), 2));
        int lx = 120 + (static_cast<int>(globalTime * 100.0) % 160);
        lp.drawLine(lx, 0, lx - 14, 38);
        lp.drawLine(lx - 14, 38, lx + 12, 78);
        lp.drawLine(lx + 12, 78, lx - 6, effHorizon - 20);
    }

    // 4. Parallax ruined Kolkata skyline silhouette (with scanline banking roll!)
    QRgb silColor  = qRgb(13, 10, 12);
    QRgb firePixel = qRgb(225, 90, 20);

    for (int x = 0; x < BUFFER_WIDTH; ++x) {
        int rollShift = static_cast<int>(bodyRoll * (x - BUFFER_WIDTH / 2) * 0.9);
        int sx = ((x + horizonOffset) % BUFFER_WIDTH + BUFFER_WIDTH) % BUFFER_WIDTH;
        int h = 10 + static_cast<int>(8.0 * std::sin(sx * 0.04) + 5.0 * std::cos(sx * 0.11));

        if (sx >= 60 && sx <= 76) h = 44;              // Broken The 42 tower
        else if (sx >= 150 && sx <= 160) h = 36;       // Shaheed Minar spire
        else if (sx >= 210 && sx <= 250) {             // Victoria Memorial dome
            int dx = sx - 230;
            h = 18 + static_cast<int>(std::sqrt(std::max(0, 400 - dx * dx)) * 0.6);
        } else if (sx >= 300 && sx <= 370) {           // Howrah Bridge twin towers
            if ((sx >= 310 && sx <= 318) || (sx >= 352 && sx <= 360)) h = 42;
            else h = 22 + ((sx % 6 == 0) ? 4 : 0);
        }

        int peakY = std::clamp(effHorizon + rollShift - h, 0, effHorizon - 1);
        int baseY = std::clamp(effHorizon + rollShift, 0, BUFFER_HEIGHT - 1);
        for (int y = peakY; y < baseY; ++y) {
            pixels[y * BUFFER_WIDTH + x] = silColor;
        }
        if (h > 18 && (sx % 15 == 0) && ((static_cast<int>(globalTime * 9.0) + sx) % 3 != 0)) {
            int wy = std::clamp(effHorizon + rollShift - h / 2, 0, BUFFER_HEIGHT - 1);
            pixels[wy * BUFFER_WIDTH + x] = firePixel;
        }
    }
}

void GameCanvas::renderGroundMode7()
{
    QRgb *pixels = reinterpret_cast<QRgb*>(screenBuffer.bits());
    const QRgb *trackPixels = reinterpret_cast<const QRgb*>(trackVisualMap.constBits());

    double cosA = std::cos(kartAngle);
    double sinA = std::sin(kartAngle);
    int effHorizon = std::clamp(HORIZON_Y + static_cast<int>(horizonPitchOffset + cameraShakeY * 0.5), 90, 210);
    double effFocal = focalLength + ((nitrousActive || nitrousBoostTimer > 0.0) ? 24.0 : 0.0);

    // Gather up to 8 closest burning fire light sources for real-time dynamic ground illumination
    struct FireLight { double x, z, radiusSq; double intensity; };
    FireLight lights[8];
    int numLights = 0;
    double flicker = 0.85 + 0.15 * std::sin(globalTime * 22.0);
    for (const auto &hz : hazardSystem.getTrackHazards()) {
        if (numLights >= 8) break;
        if (hz.type == HazardType::BURNING_TAXI || hz.type == HazardType::BURNING_AUTO ||
            (hz.type == HazardType::GAS_VENT && hz.isErupting)) {
            if (std::hypot(hz.x - kartX, hz.z - kartZ) < 360.0) {
                lights[numLights++] = { hz.x, hz.z, 75.0 * 75.0, (hz.type == HazardType::GAS_VENT ? 1.3 : 1.0) * flicker };
            }
        }
    }

    constexpr int FOG_R = 13, FOG_G = 9, FOG_B = 10;

    for (int y = effHorizon; y < BUFFER_HEIGHT; ++y) {
        int dy = y - effHorizon;
        if (dy == 0) dy = 1;

        double zGround = (cameraHeight * effFocal) / dy;
        double scaleGround = cameraHeight / dy;
        double fog = std::clamp((zGround - 40.0) / 360.0, 0.0, 0.93);
        double invFog = 1.0 - fog;

        // Horizontal hit-shockwave ripple
        int rippleShift = (hitRippleTimer > 0.0)
            ? static_cast<int>(std::sin(y * 0.35 + globalTime * 45.0) * hitRippleTimer * 8.0) : 0;

        for (int x = 0; x < BUFFER_WIDTH; ++x) {
            int dx = x - (BUFFER_WIDTH / 2) + static_cast<int>(cameraShakeX * 0.4) + rippleShift;
            // Subtle banking tilt per column
            int bankedDy = std::max(1, dy - static_cast<int>(bodyRoll * dx * 0.4));
            double zRow = (bankedDy == dy) ? zGround : ((cameraHeight * effFocal) / bankedDy);
            double sRow = (bankedDy == dy) ? scaleGround : (cameraHeight / bankedDy);

            double xGround = dx * sRow;
            double u = kartX - xGround * sinA + zRow * cosA;
            double v = kartZ + xGround * cosA + zRow * sinA;

            QRgb baseCol;
            if (u < 0.0 || u >= TRACK_MAP_SIZE || v < 0.0 || v >= TRACK_MAP_SIZE) {
                baseCol = qRgb(10, 8, 9);
            } else {
                int mapU = static_cast<int>(u);
                int mapV = static_cast<int>(v);
                baseCol = trackPixels[mapV * TRACK_MAP_SIZE + mapU];

                // Animated Hooghly River water ripples (matches river RGB signature 12, 26, 36)
                if (qRed(baseCol) == 12 && qGreen(baseCol) == 26 && qBlue(baseCol) == 36) {
                    double wave = std::sin(u * 0.14 + globalTime * 3.8) + std::cos(v * 0.18 - globalTime * 2.5);
                    if (wave > 1.1) baseCol = qRgb(28, 52, 68);
                    else if (wave < -1.2) baseCol = qRgb(42, 24, 18); // fire reflection shimmer
                }
            }

            // Ambient blackout + forward headlight cone
            double absDx = std::abs(dx);
            double coneHalfW = 36.0 + (dy * 0.85);
            double lightFactor = 0.46;
            if (zRow < 250.0 && absDx < coneHalfW) {
                double radial = 1.0 - (absDx / coneHalfW);
                double depthFade = 1.0 - (zRow / 250.0);
                lightFactor += 0.78 * radial * depthFade;
            }
            lightFactor = std::min(1.25, lightFactor);

            int sr = std::min(255, static_cast<int>(qRed(baseCol)   * lightFactor));
            int sg = std::min(255, static_cast<int>(qGreen(baseCol) * lightFactor * 0.96));
            int sb = std::min(255, static_cast<int>(qBlue(baseCol)  * lightFactor * 0.90));

            // Dynamic firelight halo from nearby burning taxis/vents
            for (int li = 0; li < numLights; ++li) {
                double ldx = u - lights[li].x;
                double ldz = v - lights[li].z;
                double d2 = ldx * ldx + ldz * ldz;
                if (d2 < lights[li].radiusSq) {
                    double falloff = (1.0 - d2 / lights[li].radiusSq) * lights[li].intensity;
                    sr = std::min(255, sr + static_cast<int>(125.0 * falloff));
                    sg = std::min(255, sg + static_cast<int>(52.0 * falloff));
                    sb = std::min(255, sb + static_cast<int>(12.0 * falloff));
                }
            }

            int outR = static_cast<int>(sr * invFog + FOG_R * fog);
            int outG = static_cast<int>(sg * invFog + FOG_G * fog);
            int outB = static_cast<int>(sb * invFog + FOG_B * fog);
            pixels[y * BUFFER_WIDTH + x] = qRgb(outR, outG, outB);
        }
    }
}

void GameCanvas::renderSprites()
{
    struct ProjectedSprite {
        int screenX;
        int screenY;
        int drawW;
        int drawH;
        double depth;
        SpriteType type;
        bool isPickup;
        bool isFireGlow;
        bool isWarningZone;
        bool isVehicle;
        int leanDir;
        bool isParticle;
        QRgb particleColor;
        int groundScreenY;
        int animFrame;
        QString name;
    };

    std::vector<ProjectedSprite> renderList;
    const auto &structures = hazardSystem.getStructures();
    const auto &hazards = hazardSystem.getTrackHazards();
    renderList.reserve(worldSprites.size() + structures.size() + hazards.size() + worldParticles.size() + aiOpponents.size() + 8);

    double cosA = std::cos(kartAngle);
    double sinA = std::sin(kartAngle);
    const auto &atlas = SpriteAtlas::instance();
    int effHorizon = std::clamp(HORIZON_Y + static_cast<int>(horizonPitchOffset + cameraShakeY * 0.5), 90, 210);
    double effFocal = focalLength + ((nitrousActive || nitrousBoostTimer > 0.0) ? 24.0 : 0.0);
    int animTick = static_cast<int>(globalTime * 10.0);

    auto isMonument = [](SpriteType t) {
        return t == SpriteType::VICTORIA_MEMORIAL || t == SpriteType::SHAHEED_MINAR ||
               t == SpriteType::WRITERS_BUILDING || t == SpriteType::HOWRAH_BRIDGE_TOWER ||
               t == SpriteType::GARIAHAT_FLYOVER_PIER || t == SpriteType::ESPLANADE_METRO ||
               t == SpriteType::ST_PAULS_CATHEDRAL || t == SpriteType::THE_42_TOWER;
    };
    auto isOverhead = [](SpriteType t) {
        return t == SpriteType::FLYOVER_BEAM_OVERHEAD ||
               t == SpriteType::BRIDGE_TRUSS_OVERHEAD ||
               t == SpriteType::SCAFFOLD_OVERHEAD;
    };

    for (const auto &sprite : worldSprites) {
        if (!sprite.active) continue;
        double dx = sprite.x - kartX;
        double dz = sprite.z - kartZ;
        double camX = -dx * sinA + dz * cosA;
        double camZ =  dx * cosA + dz * sinA;
        if (camZ <= cameraNearPlane || camZ > 560.0) continue;

        bool pickup = atlas.isPickup(sprite.type);
        double bobOffset = pickup ? (std::sin(globalTime * 5.0 + sprite.x * 0.1) * 1.6 + 2.0) : 0.0;
        double effectiveY = sprite.y + bobOffset;

        int sx = static_cast<int>((BUFFER_WIDTH / 2) + (effFocal * camX) / camZ + cameraShakeX * 0.5);
        int sy = static_cast<int>(effHorizon + (effFocal * (cameraHeight - effectiveY)) / camZ);
        int groundY = static_cast<int>(effHorizon + (effFocal * cameraHeight) / camZ);

        double scale = effFocal / camZ;
        double baseW = 18.0, baseH = 18.0;
        if (isMonument(sprite.type)) { baseW = 74.0; baseH = 56.0; }
        else if (isOverhead(sprite.type)) { baseW = 92.0; baseH = 38.0; }
        else if (sprite.type == SpriteType::BURNING_TAXI || sprite.type == SpriteType::WRECKED_BUS ||
                 sprite.type == SpriteType::DERAILED_TRAM || sprite.type == SpriteType::BURNING_AUTO) {
            baseW = 30.0; baseH = 22.0;
        } else if (sprite.type == SpriteType::STEEL_GIRDER || sprite.type == SpriteType::CONCRETE_RUBBLE) {
            baseW = 26.0; baseH = 19.0;
        }

        int drawW = std::min(260, static_cast<int>(baseW * scale));
        int drawH = std::min(220, static_cast<int>(baseH * scale));
        if (drawW <= 1 || drawH <= 1) continue;

        bool fire = (sprite.type == SpriteType::BURNING_TAXI || sprite.type == SpriteType::BURNING_AUTO);
        renderList.push_back({ sx, sy, drawW, drawH, camZ, sprite.type, pickup, fire, false, false, 0, false, 0, groundY, animTick, "" });
    }

    for (const auto &cs : structures) {
        double renderX = cs.x;
        if (cs.state == CollapseState::WARNING) renderX += ((rand() % 5) - 2) * 1.4;
        double dx = renderX - kartX;
        double dz = cs.z - kartZ;
        double camX = -dx * sinA + dz * cosA;
        double camZ =  dx * cosA + dz * sinA;
        if (camZ <= cameraNearPlane || camZ > 540.0) continue;

        int sx = static_cast<int>((BUFFER_WIDTH / 2) + (effFocal * camX) / camZ + cameraShakeX * 0.5);
        int sy = static_cast<int>(effHorizon + (effFocal * (cameraHeight - cs.y)) / camZ);
        int groundY = static_cast<int>(effHorizon + (effFocal * cameraHeight) / camZ);
        double scale = effFocal / camZ;

        SpriteType st = SpriteType::GIRDER_FALLING;
        double bw = 32.0, bh = 38.0;
        if (cs.state == CollapseState::INTACT || cs.state == CollapseState::WARNING) {
            st = (cs.spriteIndex == 1) ? SpriteType::BRIDGE_TRUSS_OVERHEAD
               : (cs.spriteIndex == 2 ? SpriteType::SCAFFOLD_OVERHEAD : SpriteType::FLYOVER_BEAM_OVERHEAD);
            bw = 82.0; bh = 34.0;
        } else if (cs.state == CollapseState::FALLING) {
            st = (cs.spriteIndex == 1) ? SpriteType::BRIDGE_TRUSS_FALLING
               : (cs.spriteIndex == 2 ? SpriteType::SCAFFOLD_FALLING : SpriteType::GIRDER_FALLING);
            bw = 34.0; bh = 42.0;
        } else {
            st = SpriteType::STEEL_GIRDER;
            bw = 30.0; bh = 20.0;
        }

        int drawW = std::min(240, static_cast<int>(bw * scale));
        int drawH = std::min(200, static_cast<int>(bh * scale));
        if (drawW <= 1 || drawH <= 1) continue;

        bool warn = (cs.state == CollapseState::WARNING || cs.state == CollapseState::FALLING);
        renderList.push_back({ sx, sy, drawW, drawH, camZ, st, false, false, warn, false, 0, false, 0, groundY, animTick, "" });
    }

    for (const auto &hz : hazards) {
        double dx = hz.x - kartX;
        double dz = hz.z - kartZ;
        double camX = -dx * sinA + dz * cosA;
        double camZ =  dx * cosA + dz * sinA;
        if (camZ <= cameraNearPlane || camZ > 500.0) continue;

        int sx = static_cast<int>((BUFFER_WIDTH / 2) + (effFocal * camX) / camZ + cameraShakeX * 0.5);
        int groundY = static_cast<int>(effHorizon + (effFocal * cameraHeight) / camZ);
        if (groundY <= effHorizon) continue;

        double scale = effFocal / camZ;
        SpriteType st = SpriteType::BURNING_TAXI;
        double bw = 28.0, bh = 22.0;
        bool fire = false;

        switch (hz.type) {
        case HazardType::BURNING_TAXI:  st = SpriteType::BURNING_TAXI; fire = true; break;
        case HazardType::WRECKED_BUS:   st = SpriteType::WRECKED_BUS;  bw = 34.0; bh = 24.0; break;
        case HazardType::DERAILED_TRAM: st = SpriteType::DERAILED_TRAM; bw = 36.0; bh = 26.0; break;
        case HazardType::BURNING_AUTO:  st = SpriteType::BURNING_AUTO; fire = true; bw = 22.0; bh = 18.0; break;
        case HazardType::ROAD_FISSURE:  st = SpriteType::CONCRETE_RUBBLE; bw = 26.0; bh = 15.0; break;
        case HazardType::GAS_VENT:
            st = hz.isErupting ? SpriteType::BURNING_TAXI : SpriteType::ROAD_BARRIER;
            fire = hz.isErupting;
            bw = hz.isErupting ? 26.0 : 16.0;
            bh = hz.isErupting ? 36.0 : 14.0;
            break;
        case HazardType::LIVE_WIRE:     st = SpriteType::STEEL_GIRDER; bw = 20.0; bh = 14.0; break;
        default: break;
        }

        int drawW = std::min(200, static_cast<int>(bw * scale));
        int drawH = std::min(180, static_cast<int>(bh * scale));
        if (drawW <= 1 || drawH <= 1) continue;

        renderList.push_back({ sx, groundY, drawW, drawH, camZ, st, false, fire, false, false, 0, false, 0, groundY, animTick, "" });
    }

    // 3D Mode-7 Projected World Particles (Smoke, Embers, Concrete Dust, Electric Sparks)
    for (const auto &wp : worldParticles) {
        double dx = wp.x - kartX;
        double dz = wp.z - kartZ;
        double camX = -dx * sinA + dz * cosA;
        double camZ =  dx * cosA + dz * sinA;
        if (camZ <= cameraNearPlane || camZ > 450.0) continue;

        int sx = static_cast<int>((BUFFER_WIDTH / 2) + (effFocal * camX) / camZ);
        int sy = static_cast<int>(effHorizon + (effFocal * (cameraHeight - wp.y)) / camZ);
        int pSize = std::clamp(static_cast<int>((wp.baseSize * effFocal) / camZ), 1, 14);
        renderList.push_back({ sx, sy, pSize, pSize, camZ, SpriteType::SUPPLY_CRATE, false, false, false, false, 0, true, wp.color, sy, 0, "" });
    }

    auto projectVehicle = [&](const KartEntity &k, bool checkAlt, const QString &tag) {
        double dx = k.x - kartX;
        double dz = k.z - kartZ;
        double camX = -dx * sinA + dz * cosA;
        double camZ =  dx * cosA + dz * sinA;
        if (camZ <= 5.0 || camZ > 500.0) return;

        int sx = static_cast<int>((BUFFER_WIDTH / 2) + (effFocal * camX) / camZ + cameraShakeX * 0.5);
        int groundY = static_cast<int>(effHorizon + (effFocal * cameraHeight) / camZ);
        if (groundY <= effHorizon) return;

        double scale = effFocal / camZ;
        double baseSize = (k.vehicleId == 1) ? 26.0 : 22.0;
        int drawW = std::min(180, static_cast<int>(baseSize * scale));
        int drawH = std::min(180, static_cast<int>(baseSize * scale));
        if (drawW <= 2 || drawH <= 2) return;
        if (sx + drawW / 2 < 0 || sx - drawW / 2 >= BUFFER_WIDTH) return;

        int lean = (k.steerAngle < -0.25) ? -1 : (k.steerAngle > 0.25 ? 1 : 0);
        SpriteType st = getVehicleSpriteType(k.vehicleId, checkAlt);
        renderList.push_back({ sx, groundY, drawW, drawH, camZ, st, false, false, false, true, lean, false, 0, groundY, 0, tag });
    };

    if (isNetworkRace) {
        for (const auto &rk : remoteKarts) projectVehicle(rk, rk.vehicleId == selectedVehicleP1, rk.name);
    } else if (selectedGameMode == GameMode::RANKED_ESCAPE) {
        for (const auto &ai : aiOpponents) projectVehicle(ai, false, ai.name);
    }

    for (const auto &p : activeProjectiles) {
        if (!p.active) continue;
        double dx = p.x - kartX, dz = p.z - kartZ;
        double camX = -dx * sinA + dz * cosA;
        double camZ =  dx * cosA + dz * sinA;
        if (camZ <= 4.0 || camZ > 450.0) continue;
        int sx = static_cast<int>((BUFFER_WIDTH / 2) + (effFocal * camX) / camZ);
        int groundY = static_cast<int>(effHorizon + (effFocal * cameraHeight) / camZ);
        if (groundY <= effHorizon) continue;
        int drawW = std::max(2, static_cast<int>(14.0 * effFocal / camZ));
        renderList.push_back({ sx, groundY, drawW, drawW, camZ, SpriteType::NITROUS_CELL, false, true, false, false, 0, false, 0, groundY, 0, "" });
    }

    std::sort(renderList.begin(), renderList.end(), [](const ProjectedSprite &a, const ProjectedSprite &b) {
        return a.depth > b.depth;
    });

    QRgb *pixels = reinterpret_cast<QRgb*>(screenBuffer.bits());

    for (const auto &item : renderList) {
        if (item.isParticle) {
            for (int py = item.screenY - item.drawH / 2; py <= item.screenY + item.drawH / 2; ++py) {
                if (py < 0 || py >= BUFFER_HEIGHT) continue;
                for (int px = item.screenX - item.drawW / 2; px <= item.screenX + item.drawW / 2; ++px) {
                    if (px >= 0 && px < BUFFER_WIDTH) pixels[py * BUFFER_WIDTH + px] = item.particleColor;
                }
            }
            continue;
        }

        if (item.groundScreenY >= effHorizon && item.groundScreenY < BUFFER_HEIGHT) {
            int shadowW = std::max(4, item.drawW * 3 / 4);
            int shadowH = std::max(2, item.drawW / 4);
            int shX0 = item.screenX - shadowW / 2;
            int shY0 = item.groundScreenY - shadowH / 2;

            for (int sy = 0; sy < shadowH; ++sy) {
                int py = shY0 + sy;
                if (py < effHorizon || py >= BUFFER_HEIGHT) continue;
                for (int sx = 0; sx < shadowW; ++sx) {
                    int px = shX0 + sx;
                    if (px < 0 || px >= BUFFER_WIDTH) continue;
                    double nx = (sx - shadowW / 2.0) / (shadowW / 2.0);
                    double ny = (sy - shadowH / 2.0) / (shadowH / 2.0);
                    if (nx * nx + ny * ny <= 1.0) {
                        QRgb bg = pixels[py * BUFFER_WIDTH + px];
                        if (item.isWarningZone) {
                            int pulse = static_cast<int>(125 + 80 * std::sin(globalTime * 18.0));
                            pixels[py * BUFFER_WIDTH + px] = qRgb(std::min(255, qRed(bg) + pulse), qGreen(bg) / 2, qBlue(bg) / 2);
                        } else if (item.isFireGlow) {
                            pixels[py * BUFFER_WIDTH + px] = qRgb(std::min(255, qRed(bg) + 80), std::min(255, qGreen(bg) + 32), qBlue(bg));
                        } else {
                            pixels[py * BUFFER_WIDTH + px] = qRgb((qRed(bg) * 50) / 100, (qGreen(bg) * 50) / 100, (qBlue(bg) * 50) / 100);
                        }
                    }
                }
            }
        }

        const QImage &texture = item.isVehicle
            ? atlas.getVehicleFrame(item.type, item.leanDir)
            : atlas.getAnimFrame(item.type, item.animFrame);
        drawTexturedBillboard(item.screenX, item.screenY, item.drawW, item.drawH, texture);

        if (!item.name.isEmpty()) {
            QPainter tagPainter(&screenBuffer);
            tagPainter.setFont(QFont("Arial", 5, QFont::Bold));
            int tagW = std::clamp(item.drawW + 14, 44, 96);
            int tagH = 10;
            int tagX = item.screenX - tagW / 2;
            int tagY = item.screenY - item.drawH - 11;
            if (tagY >= 0 && tagX >= 0 && tagX + tagW < BUFFER_WIDTH) {
                tagPainter.fillRect(QRect(tagX, tagY, tagW, tagH), QColor(12, 10, 14, 210));
                tagPainter.setPen(QPen(QColor(245, 155, 25), 1));
                tagPainter.drawRect(QRect(tagX, tagY, tagW, tagH));
                tagPainter.setPen(Qt::white);
                tagPainter.drawText(QRect(tagX, tagY, tagW, tagH), Qt::AlignCenter, item.name);
            }
        }
    }
}

void GameCanvas::drawTexturedBillboard(int screenX, int screenY, int drawW, int drawH, const QImage &texture)
{
    if (texture.isNull() || drawW <= 0 || drawH <= 0) return;

    QRgb *pixels = reinterpret_cast<QRgb*>(screenBuffer.bits());
    const QRgb *texPixels = reinterpret_cast<const QRgb*>(texture.constBits());
    int texW = texture.width();
    int texH = texture.height();

    int startX = screenX - drawW / 2;
    int startY = screenY - drawH;

    int clipStartX = std::max(0, startX);
    int clipEndX   = std::min(BUFFER_WIDTH, startX + drawW);
    int clipStartY = std::max(0, startY);
    int clipEndY   = std::min(BUFFER_HEIGHT, startY + drawH);

    for (int y = clipStartY; y < clipEndY; ++y) {
        int ty = std::clamp(((y - startY) * texH) / drawH, 0, texH - 1);
        const QRgb *texRow = texPixels + ty * texW;
        QRgb *bufRow = pixels + y * BUFFER_WIDTH;

        for (int x = clipStartX; x < clipEndX; ++x) {
            int tx = std::clamp(((x - startX) * texW) / drawW, 0, texW - 1);
            QRgb color = texRow[tx];
            int alpha = qAlpha(color);
            if (alpha < 32) continue;

            if (alpha >= 250) {
                bufRow[x] = color;
            } else {
                QRgb dst = bufRow[x];
                int invA = 255 - alpha;
                int r = (qRed(color)   * alpha + qRed(dst)   * invA) / 255;
                int g = (qGreen(color) * alpha + qGreen(dst) * invA) / 255;
                int b = (qBlue(color)  * alpha + qBlue(dst)  * invA) / 255;
                bufRow[x] = qRgb(r, g, b);
            }
        }
    }
}

void GameCanvas::initAshParticles()
{
    ashParticles.clear();
    for (int i = 0; i < 70; ++i) {
        AshParticle p;
        p.x = rand() % BUFFER_WIDTH;
        p.y = rand() % BUFFER_HEIGHT;
        p.vx = -15.0 - (rand() % 30);
        p.vy = 18.0 + (rand() % 35);
        p.isEmber = (i % 4 == 0);
        p.size = p.isEmber ? 1.5 : 1.0;
        p.color = p.isEmber ? qRgb(255, 130, 30) : qRgb(145, 140, 138);
        ashParticles.push_back(p);
    }
}

void GameCanvas::updateAshParticles(double dt)
{
    for (auto &p : ashParticles) {
        p.x += (p.vx - steeringVisualAngle * 45.0) * dt;
        p.y += p.vy * dt;
        if (p.x < 0) p.x += BUFFER_WIDTH;
        if (p.x >= BUFFER_WIDTH) p.x -= BUFFER_WIDTH;
        if (p.y >= BUFFER_HEIGHT) {
            p.y = 0;
            p.x = rand() % BUFFER_WIDTH;
        }
    }
}

void GameCanvas::renderAshOverlay()
{
    QRgb *pixels = reinterpret_cast<QRgb*>(screenBuffer.bits());
    for (const auto &p : ashParticles) {
        int px = static_cast<int>(p.x);
        int py = static_cast<int>(p.y);
        if (px >= 0 && px < BUFFER_WIDTH && py >= 0 && py < BUFFER_HEIGHT) {
            pixels[py * BUFFER_WIDTH + px] = p.color;
            if (p.isEmber && px + 1 < BUFFER_WIDTH) {
                pixels[py * BUFFER_WIDTH + px + 1] = qRgb(220, 75, 15);
            }
        }
    }
}

void GameCanvas::renderSpeedLines()
{
    if (!nitrousActive && nitrousBoostTimer <= 0.0 && miniTurboTimer <= 0.0) return;

    QPainter painter(&screenBuffer);
    bool isNitro = (nitrousActive || nitrousBoostTimer > 0.0);
    painter.setPen(QPen(isNitro ? QColor(110, 235, 255, 140) : QColor(255, 185, 55, 130), 1));
    int cx = BUFFER_WIDTH / 2, cy = HORIZON_Y;
    for (int i = 0; i < 16; ++i) {
        double a = (i * (2.0 * M_PI / 16.0)) + std::fmod(globalTime * 4.0, 0.4);
        int r1 = 125 + (rand() % 20);
        int r2 = 195 + (rand() % 30);
        painter.drawLine(cx + static_cast<int>(std::cos(a) * r1), cy + static_cast<int>(std::sin(a) * r1 * 0.7),
                         cx + static_cast<int>(std::cos(a) * r2), cy + static_cast<int>(std::sin(a) * r2 * 0.7));
    }
}

void GameCanvas::renderCockpit()
{
    QRgb *pixels = reinterpret_cast<QRgb*>(screenBuffer.bits());
    const VehicleDef &v = getSelectedVehicleP1();

    for (const auto &sp : driftSparks) {
        int sx = static_cast<int>(sp.x), sy = static_cast<int>(sp.y);
        if (sx >= 0 && sx < BUFFER_WIDTH && sy >= 0 && sy < BUFFER_HEIGHT) {
            pixels[sy * BUFFER_WIDTH + sx] = sp.color;
        }
    }

    // 1. Angled Roll-Cage A-Pillars + Roof Visor Bar (real survival truck/car cabin)
    QRgb cageCol = qRgb(16, 15, 18);
    for (int y = 0; y < BUFFER_HEIGHT; ++y) {
        int barW = 4 + (y / 38);
        for (int x = 0; x < barW; ++x) {
            pixels[y * BUFFER_WIDTH + x] = cageCol;
            pixels[y * BUFFER_WIDTH + (BUFFER_WIDTH - 1 - x)] = cageCol;
        }
    }

    // 2. Scarred Vehicle Hood seen over dashboard
    int vibY = static_cast<int>(cameraShakeY * 0.6 + ((std::abs(kartSpeed) > 10.0) ? std::sin(globalTime * 35.0) * 0.8 : 0.0));
    int hoodCenter = BUFFER_WIDTH / 2 + static_cast<int>(cameraShakeX * 0.5);
    int hoodTop = BUFFER_HEIGHT - 44 + vibY;
    QRgb hoodCol = (dozerRamTimer > 0.0) ? qRgb(135, 145, 160) : v.primaryColor.darker(135).rgb();
    QRgb stripeCol = v.secondaryColor.rgb();

    for (int y = std::max(0, hoodTop); y < BUFFER_HEIGHT - 24; ++y) {
        int halfW = static_cast<int>(55.0 + (y - hoodTop) * 2.6);
        for (int x = hoodCenter - halfW; x < hoodCenter + halfW; ++x) {
            if (x >= 0 && x < BUFFER_WIDTH) {
                pixels[y * BUFFER_WIDTH + x] = (std::abs(x - hoodCenter) <= 3) ? stripeCol : hoodCol;
            }
        }
    }

    // 3. Full Dark Apocalypse Dashboard & Instrument Cluster
    QPainter painter(&screenBuffer);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(0, BUFFER_HEIGHT - 28, BUFFER_WIDTH, 28, QColor(14, 13, 16));
    painter.setPen(QPen(QColor(55, 50, 48), 1));
    painter.drawLine(0, BUFFER_HEIGHT - 28, BUFFER_WIDTH, BUFFER_HEIGHT - 28);

    // Animated LED RPM strip in center dash
    double rpmFrac = std::clamp(std::abs(kartSpeed) / std::max(1.0, maxSpeed), 0.0, 1.0);
    int activeLeds = static_cast<int>(rpmFrac * 12.0);
    for (int i = 0; i < 12; ++i) {
        QColor ledCol = (i >= activeLeds) ? QColor(28, 28, 32)
                      : (i >= 9 ? QColor(235, 45, 35) : (i >= 6 ? QColor(245, 165, 25) : QColor(80, 210, 95)));
        painter.fillRect(156 + i * 7, BUFFER_HEIGHT - 22, 5, 5, ledCol);
    }

    // Blinking hazard LED if hull < 40% or collapse close
    if (playerHull.integrity < 40.0 || playerHull.isOnFire) {
        bool blink = (static_cast<int>(globalTime * 8.0) % 2 == 0);
        painter.fillRect(142, BUFFER_HEIGHT - 22, 6, 6, blink ? QColor(255, 40, 35) : QColor(50, 15, 15));
    }

    // Tactical Steering Wheel (shifted left/right smoothly)
    int wx = BUFFER_WIDTH / 2 + static_cast<int>(steeringVisualAngle * 22.0);
    int wy = BUFFER_HEIGHT - 6;
    painter.setPen(QPen(QColor(38, 38, 42), 4));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPoint(wx, wy), 22, 22);
    painter.setPen(QPen(QColor(115, 75, 30), 3));
    painter.drawLine(wx - 18, wy - 2, wx + 18, wy - 2);
    painter.drawLine(wx, wy, wx, wy + 18);

    // 4. Top Rearview Mirror showing approaching firestorm / collapse front
    QRect mirror(BUFFER_WIDTH / 2 - 42, 4, 84, 14);
    double threat = std::clamp(1.0 - (splineProgress - collapseWaveProgress) * 5.0, 0.0, 1.0);
    int glowR = static_cast<int>(25 + threat * 195);
    painter.fillRect(mirror, QColor(glowR, 20 + static_cast<int>(threat * 45), 14));
    painter.setPen(QPen(QColor(65, 62, 68), 1));
    painter.drawRect(mirror);
    painter.setFont(QFont("Arial", 4, QFont::Bold));
    painter.setPen(threat > 0.6 ? QColor(255, 220, 140) : QColor(145, 140, 138));
    painter.drawText(mirror, Qt::AlignCenter, threat > 0.6 ? "REAR: COLLAPSE!" : "REAR VIEW");

    // 5. Red damage border flash
    if (damageFlashTimer > 0.0) {
        int boost = static_cast<int>(damageFlashTimer * 180.0);
        for (int y = 0; y < BUFFER_HEIGHT; ++y) {
            for (int x = 0; x < BUFFER_WIDTH; ++x) {
                if (x < 10 || x >= BUFFER_WIDTH - 10 || y < 10 || y >= BUFFER_HEIGHT - 10) {
                    QRgb c = pixels[y * BUFFER_WIDTH + x];
                    pixels[y * BUFFER_WIDTH + x] = qRgb(std::min(255, qRed(c) + boost), qGreen(c) / 2, qBlue(c) / 2);
                }
            }
        }
    }
}

void GameCanvas::renderWindshieldCracks()
{
    if (playerHull.crackLevel <= 0 && !playerHull.isOnFire) return;
    QPainter painter(&screenBuffer);
    if (playerHull.crackLevel >= 1) {
        painter.setPen(QPen(QColor(210, 220, 230, 155), 1));
        painter.drawLine(28, 22, 68, 56);
        painter.drawLine(68, 56, 94, 48);
        painter.drawLine(68, 56, 56, 92);
    }
    if (playerHull.crackLevel >= 2) {
        painter.setPen(QPen(QColor(235, 240, 245, 205), 1));
        painter.drawLine(360, 24, 315, 68);
        painter.drawLine(315, 68, 338, 108);
        painter.drawLine(315, 68, 282, 82);
    }
    if (playerHull.isOnFire) {
        int a = static_cast<int>(50 + 30 * std::sin(globalTime * 16.0));
        painter.fillRect(0, BUFFER_HEIGHT - 55, BUFFER_WIDTH, 30, QColor(220, 65, 10, a));
    }
}

void GameCanvas::renderGearHUD()
{
    QPainter painter(&screenBuffer);
    int boxSize = 36;
    int boxX = BUFFER_WIDTH - boxSize - 8;
    int boxY = 8;
    QRect hudRect(boxX, boxY, boxSize, boxSize);

    QColor borderColor = (heldGear != SurvivalGear::NONE) ? QColor(245, 165, 25) : QColor(75, 78, 88);
    painter.fillRect(hudRect, QColor(14, 14, 18, 220));
    painter.setPen(QPen(borderColor, 2));
    painter.drawRect(hudRect);

    const auto &atlas = SpriteAtlas::instance();
    if (heldGear != SurvivalGear::NONE) {
        SpriteType st = (heldGear == SurvivalGear::DOZER_RAM) ? SpriteType::SUPPLY_CRATE : SpriteType::SCRAP_CACHE;
        painter.drawImage(QRect(boxX + 4, boxY + 4, 28, 28), atlas.getSprite(st));
        painter.setFont(QFont("Arial", 5, QFont::Bold));
        painter.setPen(QColor(255, 200, 60));
        painter.drawText(QRect(boxX - 12, boxY + boxSize + 2, boxSize + 24, 10), Qt::AlignCenter,
                         heldGear == SurvivalGear::DOZER_RAM ? "[E] RAM SHIELD" : "[E] FLARE");
    } else {
        painter.setFont(QFont("Arial", 5, QFont::Bold));
        painter.setPen(QColor(110, 115, 125));
        painter.drawText(hudRect, Qt::AlignCenter, "GEAR");
    }
}

void GameCanvas::renderEscapeHUD()
{
    QPainter painter(&screenBuffer);
    int topW = 152, topX = (BUFFER_WIDTH - topW) / 2;
    painter.fillRect(QRect(topX, 20, topW, 34), QColor(12, 10, 14, 225));
    bool urgent = (evacTimer < 20.0 && selectedGameMode != GameMode::TIME_TRIAL);
    painter.setPen(QPen(urgent ? QColor(245, 50, 45) : QColor(235, 150, 25), 1));
    painter.drawRect(QRect(topX, 20, topW, 34));

    QString timerStr = (selectedGameMode == GameMode::TIME_TRIAL)
        ? QString("TIME %1s").arg(QString::number(raceElapsedTime, 'f', 1))
        : QString("EVAC %1s").arg(QString::number(evacTimer, 'f', 1));
    painter.setFont(QFont("Courier New", 10, QFont::Bold));
    painter.setPen(urgent ? QColor(255, 70, 70) : QColor(255, 195, 55));
    painter.drawText(QRect(topX, 22, topW, 15), Qt::AlignCenter, timerStr);

    int totalRacers = isNetworkRace ? static_cast<int>(remoteKarts.size() + 1)
                      : (selectedGameMode == GameMode::RANKED_ESCAPE ? static_cast<int>(aiOpponents.size() + 1) : 1);
    QString rankStr = QString("RANK %1/%2 • %3% • CP %4/2")
                          .arg(playerPlacement).arg(totalRacers)
                          .arg(static_cast<int>(splineProgress * 100.0))
                          .arg(lastCheckpointReached);
    painter.setFont(QFont("Arial", 6, QFont::Bold));
    painter.setPen(QColor(210, 215, 220));
    painter.drawText(QRect(topX, 38, topW, 13), Qt::AlignCenter, rankStr);

    painter.fillRect(QRect(topX + 4, 56, topW - 8, 11), QColor(18, 14, 16, 195));
    painter.setFont(QFont("Arial", 5, QFont::Bold));
    painter.setPen(QColor(245, 140, 40));
    painter.drawText(QRect(topX + 4, 56, topW - 8, 11), Qt::AlignCenter, currentDistrictName);

    // Pit-Stop Prompt when checkpoint reached in Solo/AI mode
    if (checkpointPromptTimer > 0.0) {
        bool pulse = (static_cast<int>(globalTime * 6.0) % 2 == 0);
        painter.fillRect(QRect(85, 70, 230, 16), QColor(15, 28, 18, 225));
        painter.setPen(QPen(pulse ? QColor(100, 240, 120) : QColor(245, 185, 45), 1));
        painter.drawRect(QRect(85, 70, 230, 16));
        painter.setFont(QFont("Arial", 6, QFont::Bold));
        painter.setPen(Qt::white);
        painter.drawText(QRect(85, 70, 230, 16), Qt::AlignCenter, "PRESS [P] FOR CHECKPOINT REPAIR & UPGRADES");
    }

    // Bottom-Left: Hull, Nitrous & Scrap
    painter.fillRect(QRect(8, BUFFER_HEIGHT - 70, 112, 38), QColor(12, 10, 14, 215));
    painter.setPen(QPen(QColor(70, 72, 82), 1));
    painter.drawRect(QRect(8, BUFFER_HEIGHT - 70, 112, 38));

    painter.setFont(QFont("Arial", 5, QFont::Bold));
    painter.setPen(QColor(220, 220, 225));
    painter.drawText(QRect(12, BUFFER_HEIGHT - 68, 42, 10), Qt::AlignLeft, QString("HULL %1%").arg(static_cast<int>(playerHull.integrity)));
    double hullFrac = std::clamp(playerHull.integrity / 100.0, 0.0, 1.0);
    QColor hullCol = (hullFrac > 0.6) ? QColor(65, 200, 90) : (hullFrac > 0.3 ? QColor(245, 170, 25) : QColor(235, 45, 40));
    painter.fillRect(QRect(54, BUFFER_HEIGHT - 66, 60, 7), QColor(32, 30, 34));
    painter.fillRect(QRect(54, BUFFER_HEIGHT - 66, static_cast<int>(60 * hullFrac), 7), hullCol);

    painter.setPen(QColor(110, 220, 255));
    painter.drawText(QRect(12, BUFFER_HEIGHT - 55, 42, 10), Qt::AlignLeft, "NITRO");
    double nitFrac = std::clamp(nitrousFuel / std::max(1.0, nitrousMaxFuel), 0.0, 1.0);
    painter.fillRect(QRect(54, BUFFER_HEIGHT - 53, 60, 7), QColor(22, 30, 38));
    painter.fillRect(QRect(54, BUFFER_HEIGHT - 53, static_cast<int>(60 * nitFrac), 7), QColor(40, 200, 255));

    painter.setPen(QColor(255, 200, 55));
    painter.drawText(QRect(12, BUFFER_HEIGHT - 44, 104, 10), Qt::AlignLeft,
                     QString("SCRAP: ₹%1 (+%2)").arg(GarageSystem::instance().getScrapBalance()).arg(scrapCollectedThisRun));

    // Bottom-Right: Speedometer
    painter.fillRect(QRect(BUFFER_WIDTH - 104, BUFFER_HEIGHT - 62, 96, 28), QColor(12, 10, 14, 215));
    painter.setPen(QPen(QColor(70, 72, 82), 1));
    painter.drawRect(QRect(BUFFER_WIDTH - 104, BUFFER_HEIGHT - 62, 96, 28));
    painter.setFont(QFont("Courier New", 11, QFont::Bold));
    painter.setPen(QColor(245, 175, 45));
    painter.drawText(QRect(BUFFER_WIDTH - 100, BUFFER_HEIGHT - 60, 88, 15), Qt::AlignRight,
                     QString("%1 KM/H").arg(static_cast<int>(std::abs(kartSpeed))));

    if (selectedGameMode != GameMode::TIME_TRIAL) {
        double gapPct = (splineProgress - collapseWaveProgress) * 100.0;
        bool dangerClose = (gapPct < 8.0);
        painter.setFont(QFont("Arial", 5, QFont::Bold));
        painter.setPen(dangerClose ? QColor(255, 70, 60) : QColor(165, 170, 180));
        painter.drawText(QRect(BUFFER_WIDTH - 100, BUFFER_HEIGHT - 45, 88, 10), Qt::AlignRight,
                         dangerClose ? "! COLLAPSE CLOSE !" : QString("QUAKE: -%1%").arg(static_cast<int>(std::max(0.0, gapPct))));
    }
}

void GameCanvas::updateMinimapTrackCache()
{
    minimapTrackCache = QImage(MinimapHUD::RADAR_SIZE, MinimapHUD::RADAR_SIZE, QImage::Format_ARGB32_Premultiplied);
    minimapTrackCache.fill(Qt::transparent);
    if (currentWaypoints.size() < 2) return;

    QPainter p(&minimapTrackCache);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    for (size_t i = 0; i < currentWaypoints.size(); ++i) {
        double rx = (currentWaypoints[i].x() / static_cast<double>(TRACK_MAP_SIZE)) * MinimapHUD::RADAR_SIZE;
        double ry = (currentWaypoints[i].y() / static_cast<double>(TRACK_MAP_SIZE)) * MinimapHUD::RADAR_SIZE;
        if (i == 0) path.moveTo(rx, ry);
        else path.lineTo(rx, ry);
    }
    p.setPen(QPen(QColor(80, 75, 70), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
    p.setPen(QPen(QColor(175, 170, 165), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
}

void GameCanvas::renderMinimap()
{
    QPainter painter(&screenBuffer);
    QRect box(MinimapHUD::RADAR_X, MinimapHUD::RADAR_Y, MinimapHUD::RADAR_SIZE, MinimapHUD::RADAR_SIZE);
    painter.fillRect(box, QColor(10, 10, 14, 215));
    painter.setPen(QPen(QColor(225, 140, 25), 1));
    painter.drawRect(box);

    if (!minimapTrackCache.isNull()) painter.drawImage(MinimapHUD::RADAR_X, MinimapHUD::RADAR_Y, minimapTrackCache);

    for (const auto &cp : stageCheckpoints) {
        QPointF pt = MinimapHUD::worldToRadar(0.5 * (cp.x1 + cp.x2), 0.5 * (cp.z1 + cp.z2));
        painter.fillRect(QRectF(pt.x() - 2, pt.y() - 2, 4, 4),
                         (lastCheckpointReached >= cp.index) ? QColor(80, 220, 90) : QColor(255, 185, 30));
    }
    if (!currentWaypoints.empty()) {
        QPointF goalPt = MinimapHUD::worldToRadar(currentWaypoints.back().x(), currentWaypoints.back().y());
        painter.fillRect(QRectF(goalPt.x() - 2.5, goalPt.y() - 2.5, 5, 5), QColor(50, 255, 110));
    }
    for (const auto &ai : aiOpponents) {
        painter.setBrush(ai.kartColor);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(MinimapHUD::worldToRadar(ai.x, ai.z), 2.0, 2.0);
    }
    for (const auto &rk : remoteKarts) {
        painter.setBrush(QColor(0, 225, 255));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(MinimapHUD::worldToRadar(rk.x, rk.z), 2.5, 2.5);
    }
    painter.setBrush(QColor(255, 220, 40));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawEllipse(MinimapHUD::worldToRadar(kartX, kartZ), 3.0, 3.0);
}

void GameCanvas::renderWipeTransition()
{
    if (wipeTransitionTimer <= 0.0) return;
    double frac = std::clamp(wipeTransitionTimer / 0.45, 0.0, 1.0);
    QPainter p(&screenBuffer);
    int barH = static_cast<int>(frac * (BUFFER_HEIGHT / 2));
    p.fillRect(0, 0, BUFFER_WIDTH, barH, QColor(6, 5, 8));
    p.fillRect(0, BUFFER_HEIGHT - barH, BUFFER_WIDTH, barH, QColor(6, 5, 8));
}

void GameCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(rect(), screenBuffer);
}

void GameCanvas::triggerCelebration(const QString &title, const QString &subtitle)
{
    isCelebrationActive = true;
    celebrationTimer = 0.0;
    celebrationTitle = title;
    celebrationSubtitle = subtitle;
    flashIntensity = 0.35;
}

void GameCanvas::updateCelebration(double dt)
{
    if (!isCelebrationActive) return;
    celebrationTimer += dt;
    flashIntensity = std::max(0.0, flashIntensity - dt * 2.5);
    if (!isRaceFinished && celebrationTimer >= celebrationDuration) isCelebrationActive = false;
}

void GameCanvas::renderCelebration()
{
    if (!isCelebrationActive) return;
    QPainter painter(&screenBuffer);
    int bannerW = 286, bannerH = 40;
    QRect r((BUFFER_WIDTH - bannerW) / 2, 88, bannerW, bannerH);
    painter.fillRect(r, QColor(14, 10, 12, 225));
    painter.setPen(QPen(QColor(245, 145, 25), 2));
    painter.drawRect(r);

    painter.setFont(QFont("Courier New", 10, QFont::Bold));
    painter.setPen(QColor(255, 195, 50));
    painter.drawText(QRect(r.x(), r.y() + 4, bannerW, 18), Qt::AlignCenter, celebrationTitle);
    painter.setFont(QFont("Arial", 6, QFont::Bold));
    painter.setPen(QColor(215, 220, 225));
    painter.drawText(QRect(r.x(), r.y() + 22, bannerW, 14), Qt::AlignCenter, celebrationSubtitle);
}

// -------------------------------------------------------------
// CHECKPOINT PIT-STOP & STAGE TRANSITION OVERLAY
// -------------------------------------------------------------

void GameCanvas::renderCheckpointPitOverlay()
{
    QPainter painter(&screenBuffer);
    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.fillRect(0, 0, BUFFER_WIDTH, BUFFER_HEIGHT, QColor(8, 7, 10, 200));

    QRect box(24, 18, BUFFER_WIDTH - 48, BUFFER_HEIGHT - 36);
    painter.fillRect(box, QColor(14, 13, 17, 245));
    painter.setPen(QPen(QColor(235, 145, 30), 2));
    painter.drawRect(box);

    auto &garage = GarageSystem::instance();
    const auto &ups = garage.upgrades();

    QString header = pitIsStageTransition
        ? QString("SECTOR %1 CLEARED — NEXT CORRIDOR READY").arg(static_cast<int>(currentStage) + 1)
        : QString("CHECKPOINT %1 RELAY BAY — FIELD REPAIRS").arg(pitCheckpointIndex);

    painter.setFont(QFont("Courier New", 9, QFont::Bold));
    painter.setPen(QColor(245, 175, 45));
    painter.drawText(QRect(32, 24, BUFFER_WIDTH - 64, 16), Qt::AlignCenter, header);

    painter.setFont(QFont("Arial", 7, QFont::Bold));
    painter.setPen(QColor(215, 220, 225));
    painter.drawText(QRect(34, 42, BUFFER_WIDTH - 68, 14), Qt::AlignCenter,
                     QString("HULL: %1%   •   NITRO: %2%   •   SCRAP: ₹%3")
                         .arg(static_cast<int>(playerHull.integrity))
                         .arg(static_cast<int>((nitrousFuel / std::max(1.0, nitrousMaxFuel)) * 100.0))
                         .arg(garage.getScrapBalance()));

    struct PitOption { QString label; QString cost; };
    int repMissing = static_cast<int>(std::ceil(100.0 - playerHull.integrity));
    int repCost = std::min(60, std::max(25, repMissing));
    PitOption opts[6] = {
        { "[1] FIELD WELD (+45% HULL)", repMissing > 0 ? QString("₹%1").arg(repCost) : "FULL" },
        { "[2] REFILL NITROUS TANK",    nitrousFuel < nitrousMaxFuel ? "₹30" : "FULL" },
        { QString("[3] ARMOR LV %1").arg(ups.armorLevel),           ups.armorLevel < 5 ? QString("₹%1").arg(GarageSystem::upgradeCost(ups.armorLevel)) : "MAX" },
        { QString("[4] ENGINE LV %1").arg(ups.engineLevel),         ups.engineLevel < 5 ? QString("₹%1").arg(GarageSystem::upgradeCost(ups.engineLevel)) : "MAX" },
        { QString("[5] SUSPENSION LV %1").arg(ups.suspensionLevel), ups.suspensionLevel < 5 ? QString("₹%1").arg(GarageSystem::upgradeCost(ups.suspensionLevel)) : "MAX" },
        { QString("[6] NITROUS CAP LV %1").arg(ups.nitrousLevel),   ups.nitrousLevel < 5 ? QString("₹%1").arg(GarageSystem::upgradeCost(ups.nitrousLevel)) : "MAX" }
    };

    int listW = isNetworkRace ? 210 : (BUFFER_WIDTH - 76);
    for (int i = 0; i < 6; ++i) {
        int ry = 62 + i * 26;
        bool sel = (pitFocusRow == i);
        QRect rr(36, ry, listW, 22);
        painter.fillRect(rr, sel ? QColor(225, 120, 25, 55) : QColor(22, 20, 26));
        painter.setPen(QPen(sel ? QColor(245, 175, 45) : QColor(55, 52, 60), 1));
        painter.drawRect(rr);

        painter.setFont(QFont("Arial", 6, QFont::Bold));
        painter.setPen(Qt::white);
        painter.drawText(QRect(42, ry + 4, listW - 55, 14), Qt::AlignLeft, opts[i].label);
        painter.setPen(QColor(255, 200, 65));
        painter.drawText(QRect(36, ry + 4, listW - 8, 14), Qt::AlignRight, opts[i].cost);
    }

    // In LAN Multiplayer, show live convoy synchronization panel on right
    if (isNetworkRace && networkManager) {
        QRect syncBox(254, 62, 114, 152);
        painter.fillRect(syncBox, QColor(18, 17, 22));
        painter.setPen(QPen(QColor(80, 75, 85), 1));
        painter.drawRect(syncBox);

        painter.setFont(QFont("Arial", 6, QFont::Bold));
        painter.setPen(QColor(245, 175, 45));
        painter.drawText(QRect(256, 66, 110, 14), Qt::AlignCenter, "CONVOY SYNC");

        const auto &plist = networkManager->getPlayers();
        for (size_t i = 0; i < plist.size() && i < 4; ++i) {
            int py = 84 + static_cast<int>(i) * 30;
            painter.setFont(QFont("Arial", 6, QFont::Bold));
            painter.setPen(Qt::white);
            painter.drawText(QRect(260, py, 102, 12), Qt::AlignLeft, plist[i].name);

            QString stTxt = plist[i].isEliminated ? "ELIMINATED"
                : (plist[i].checkpointReady && plist[i].checkpointIdx == pitCheckpointIndex ? "READY" : "UPGRADING...");
            QColor stCol = plist[i].isEliminated ? QColor(235, 60, 55)
                : (stTxt == "READY" ? QColor(90, 230, 110) : QColor(245, 175, 45));
            painter.setFont(QFont("Arial", 5, QFont::Bold));
            painter.setPen(stCol);
            painter.drawText(QRect(260, py + 12, 102, 10), Qt::AlignLeft, stTxt);
        }
    }

    if (pitStatusTimer > 0.0 && !pitStatusMsg.isEmpty()) {
        painter.setFont(QFont("Arial", 6, QFont::Bold));
        painter.setPen(QColor(110, 240, 130));
        painter.drawText(QRect(36, 224, BUFFER_WIDTH - 72, 14), Qt::AlignCenter, pitStatusMsg);
    }

    painter.setFont(QFont("Arial", 7, QFont::Bold));
    if (isNetworkRace) {
        painter.setPen(pitLocalReady ? QColor(90, 235, 115) : QColor(245, 185, 50));
        painter.drawText(QRect(36, 244, BUFFER_WIDTH - 72, 16), Qt::AlignCenter,
                         pitLocalReady ? "READY! WAITING FOR ALL SURVIVORS..." : "[SPACE / ENTER] READY FOR SIMULTANEOUS ROLLOUT");
    } else {
        painter.setPen(QColor(100, 235, 120));
        painter.drawText(QRect(36, 244, BUFFER_WIDTH - 72, 16), Qt::AlignCenter,
                         "[1-6] Buy Repair/Upgrade  •  [SPACE / ENTER] ROLL OUT!");
    }
}

// -------------------------------------------------------------
// DARK MENUS: TITLE, GARAGE & RESULTS
// -------------------------------------------------------------

void GameCanvas::renderTitleMenu()
{
    QPainter painter(&screenBuffer);
    painter.setRenderHint(QPainter::Antialiasing, false);

    QLinearGradient skyGrad(0, 0, 0, HORIZON_Y);
    skyGrad.setColorAt(0.0, QColor(5, 4, 8));
    skyGrad.setColorAt(1.0, QColor(44, 15, 10));
    painter.fillRect(QRect(0, 0, BUFFER_WIDTH, HORIZON_Y), skyGrad);

    QLinearGradient groundGrad(0, HORIZON_Y, 0, BUFFER_HEIGHT);
    groundGrad.setColorAt(0.0, QColor(20, 11, 10));
    groundGrad.setColorAt(1.0, QColor(9, 8, 10));
    painter.fillRect(QRect(0, HORIZON_Y, BUFFER_WIDTH, BUFFER_HEIGHT - HORIZON_Y), groundGrad);

    painter.setBrush(QColor(12, 10, 12));
    painter.setPen(Qt::NoPen);
    painter.drawRect(36, HORIZON_Y - 58, 14, 58);
    painter.drawRect(302, HORIZON_Y - 46, 8, 46);
    painter.drawRect(352, HORIZON_Y - 46, 8, 46);
    painter.setPen(QPen(QColor(18, 14, 16), 2));
    painter.drawLine(275, HORIZON_Y, 306, HORIZON_Y - 44);
    painter.drawLine(306, HORIZON_Y - 44, 356, HORIZON_Y - 44);
    painter.drawLine(356, HORIZON_Y - 44, 385, HORIZON_Y);

    painter.setPen(QColor(215, 95, 25, 45));
    for (int i = 0; i < 14; ++i) {
        painter.drawLine(BUFFER_WIDTH / 2, HORIZON_Y, -100 + i * 46, BUFFER_HEIGHT);
    }
    double gridScroll = std::fmod(globalTime * 40.0, 18.0);
    for (int y = HORIZON_Y; y < BUFFER_HEIGHT; y += 12) {
        int lineY = y + static_cast<int>(gridScroll);
        if (lineY > HORIZON_Y && lineY < BUFFER_HEIGHT) {
            painter.setPen(QColor(225, 110, 25, 65));
            painter.drawLine(0, lineY, BUFFER_WIDTH, lineY);
        }
    }

    int stripeW = 12;
    double offset = std::fmod(globalTime * 22.0, static_cast<double>(stripeW * 2));
    for (int x = -stripeW * 2; x < BUFFER_WIDTH + stripeW * 2; x += stripeW) {
        int bx = static_cast<int>(x + offset);
        bool even = ((x / stripeW) % 2 == 0);
        painter.fillRect(QRect(bx, 0, stripeW, 7), even ? QColor(225, 145, 20) : QColor(18, 18, 20));
    }

    painter.setFont(QFont("Courier New", 20, QFont::Black));
    painter.setPen(QColor(8, 5, 6));
    painter.drawText(QRect(2, 20, BUFFER_WIDTH, 32), Qt::AlignCenter, "KOLKATA: LAST EXIT");
    painter.setPen(QColor(185, 42, 22));
    painter.drawText(QRect(1, 19, BUFFER_WIDTH, 32), Qt::AlignCenter, "KOLKATA: LAST EXIT");
    painter.setPen(QColor(245, 175, 45));
    painter.drawText(QRect(0, 18, BUFFER_WIDTH, 32), Qt::AlignCenter, "KOLKATA: LAST EXIT");

    painter.setFont(QFont("Arial", 6, QFont::Bold));
    painter.setPen(QColor(200, 110, 65));
    painter.drawText(QRect(0, 50, BUFFER_WIDTH, 14), Qt::AlignCenter, "02:40 IST • 3-SECTOR CONTINUOUS EVACUATION");

    int boxX = 62, boxY = 74, boxW = 276, boxH = 148;
    painter.setBrush(QColor(12, 11, 14, 238));
    painter.setPen(QPen(QColor(225, 130, 25), 2));
    painter.drawRoundedRect(QRect(boxX, boxY, boxW, boxH), 6, 6);

    painter.setFont(QFont("Arial", 7, QFont::Bold));
    painter.setPen(QColor(245, 175, 45));
    painter.drawText(QRect(boxX, boxY + 8, boxW, 14), Qt::AlignCenter, "— SELECT EVACUATION MODE —");

    const char* menuOptions[3] = {
        "1. CONTINUOUS ESCAPE (VS AI RIVALS)",
        "2. LAN CONVOY ESCAPE (MULTIPLAYER)",
        "3. SURVIVAL TIME TRIAL (SOLO)"
    };
    const char* menuDescs[3] = {
        "All 3 Kolkata sectors • Pit-stop repairs at checkpoints",
        "Wait & upgrade together at checkpoints • Synced rollout",
        "Long-route solo survival to farm Scrap (₹)"
    };

    for (int i = 0; i < 3; ++i) {
        int iy = boxY + 28 + i * 38;
        bool sel = (menuIndex == i);
        if (sel) {
            painter.fillRect(QRect(boxX + 12, iy, boxW - 24, 32), QColor(225, 110, 20, 55));
            painter.setPen(QPen(QColor(245, 165, 30), 1));
            painter.drawRect(QRect(boxX + 12, iy, boxW - 24, 32));
            painter.setFont(QFont("Arial", 8, QFont::Bold));
            painter.setPen(QColor(255, 220, 120));
            painter.drawText(QRect(boxX + 16, iy + 3, boxW - 32, 14), Qt::AlignCenter, menuOptions[i]);
            painter.setFont(QFont("Arial", 5, QFont::Normal));
            painter.setPen(QColor(195, 175, 155));
            painter.drawText(QRect(boxX + 16, iy + 18, boxW - 32, 11), Qt::AlignCenter, menuDescs[i]);
        } else {
            painter.setFont(QFont("Arial", 8, QFont::Bold));
            painter.setPen(QColor(135, 130, 130));
            painter.drawText(QRect(boxX + 16, iy + 8, boxW - 32, 14), Qt::AlignCenter, menuOptions[i]);
        }
    }

    painter.fillRect(QRect(26, 234, BUFFER_WIDTH - 52, 46), QColor(12, 11, 14, 225));
    painter.setPen(QPen(QColor(70, 55, 45), 1));
    painter.drawRect(QRect(26, 234, BUFFER_WIDTH - 52, 46));
    painter.setFont(QFont("Arial", 6, QFont::Bold));
    painter.setPen(QColor(235, 165, 45));
    painter.drawText(QRect(26, 238, BUFFER_WIDTH - 52, 14), Qt::AlignCenter,
                     "W/S: NAVIGATE  •  ENTER: GARAGE  •  [C]: REPLAY INTRO STORY");
    painter.setPen(QColor(175, 170, 165));
    painter.drawText(QRect(26, 256, BUFFER_WIDTH - 52, 14), Qt::AlignCenter,
                     QString("SALVAGE SCRAP BALANCE: ₹%1").arg(GarageSystem::instance().getScrapBalance()));
}

void GameCanvas::renderGarageScreen()
{
    QPainter painter(&screenBuffer);
    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.fillRect(QRect(0, 0, BUFFER_WIDTH, BUFFER_HEIGHT), QColor(12, 11, 14));
    painter.setPen(QPen(QColor(225, 130, 25), 2));
    painter.drawLine(0, 36, BUFFER_WIDTH, 36);
    painter.drawLine(0, 252, BUFFER_WIDTH, 252);

    auto &garage = GarageSystem::instance();
    const auto &ups = garage.upgrades();

    painter.setFont(QFont("Courier New", 11, QFont::Bold));
    painter.setPen(QColor(245, 175, 45));
    painter.drawText(QRect(10, 4, 240, 18), Qt::AlignLeft | Qt::AlignVCenter, "GARAGE: RIG & UPGRADES");

    painter.setFont(QFont("Arial", 8, QFont::Bold));
    painter.setPen(QColor(255, 210, 65));
    painter.drawText(QRect(240, 4, 150, 18), Qt::AlignRight | Qt::AlignVCenter,
                     QString("SCRAP: ₹%1").arg(garage.getScrapBalance()));

    QString stageLabel = (currentStage == KolkataStage::GARIAHAT_COLLAPSE)
        ? "START SECTOR 1 [MEDIUM]: GARIAHAT COLLAPSE"
        : (currentStage == KolkataStage::PARK_STREET_RUINS
            ? "START SECTOR 2 [HARD]: PARK STREET & MAIDAN"
            : "START SECTOR 3 [INSANE]: HOWRAH BRIDGE RUIN");
    painter.setFont(QFont("Arial", 6, QFont::Bold));
    painter.setPen(currentStage == KolkataStage::HOWRAH_BRIDGE ? QColor(255, 75, 60) : QColor(235, 165, 55));
    painter.drawText(QRect(10, 21, BUFFER_WIDTH - 20, 13), Qt::AlignLeft,
                     stageLabel + "   [KEYS 1-3 TO SWITCH]");

    const auto &atlas = SpriteAtlas::instance();
    const VehicleDef &v = vehicles[selectedVehicleP1];
    QRect rigBox(10, 44, 176, 200);
    painter.fillRect(rigBox, QColor(18, 17, 22));
    painter.setPen(QPen(QColor(225, 130, 25), 1));
    painter.drawRect(rigBox);

    painter.fillRect(QRect(11, 45, 174, 18), v.primaryColor);
    painter.setFont(QFont("Arial", 7, QFont::Bold));
    painter.setPen(Qt::black);
    painter.drawText(QRect(11, 45, 174, 18), Qt::AlignCenter, QString("◄ %1 ►").arg(v.name));

    painter.fillRect(QRect(20, 68, 156, 54), QColor(10, 9, 12));
    const QImage &rigImg = atlas.getSprite(getVehicleSpriteType(v.id));
    int bob = static_cast<int>(std::sin(globalTime * 6.0) * 1.5);
    painter.drawImage(QRect(74, 72 + bob, 48, 48), rigImg);

    painter.setFont(QFont("Arial", 6, QFont::Bold));
    painter.setPen(QColor(235, 160, 45));
    painter.drawText(QRect(16, 125, 164, 12), Qt::AlignCenter, v.archetype);

    auto drawPipBar = [&](int y, const QString &lbl, int pips, QColor col) {
        painter.setFont(QFont("Arial", 5, QFont::Bold));
        painter.setPen(QColor(175, 175, 185));
        painter.drawText(QRect(18, y, 36, 10), Qt::AlignLeft, lbl);
        for (int p = 0; p < 5; ++p) {
            QRect pr(58 + p * 20, y + 1, 16, 6);
            painter.fillRect(pr, (p < pips) ? col : QColor(34, 34, 40));
        }
    };
    drawPipBar(140, "SPEED",  v.speedPips,    QColor(235, 75, 50));
    drawPipBar(154, "ACCEL",  v.accelPips,    QColor(245, 185, 35));
    drawPipBar(168, "HANDL",  v.handlingPips, QColor(65, 195, 235));
    drawPipBar(182, "ARMOR",  v.armorPips,    QColor(95, 200, 105));

    painter.setFont(QFont("Arial", 5));
    painter.setPen(QColor(185, 180, 175));
    painter.drawText(QRect(16, 198, 164, 42), Qt::AlignLeft | Qt::TextWordWrap, v.description);

    QRect shopBox(194, 44, 196, 200);
    painter.fillRect(shopBox, QColor(18, 17, 22));
    painter.setPen(QPen(QColor(80, 75, 70), 1));
    painter.drawRect(shopBox);

    painter.setFont(QFont("Arial", 7, QFont::Bold));
    painter.setPen(QColor(245, 175, 45));
    painter.drawText(QRect(200, 48, 184, 14), Qt::AlignCenter, "WORKSHOP UPGRADES [W/S + U]");

    struct UpRow { QString name; int lvl; };
    UpRow rows[4] = {
        { "ARMOR PLATING", ups.armorLevel },
        { "ENGINE / TURBO", ups.engineLevel },
        { "SUSPENSION", ups.suspensionLevel },
        { "NITROUS TANK", ups.nitrousLevel }
    };

    for (int i = 0; i < 4; ++i) {
        int ry = 68 + i * 32;
        bool sel = (garageUpgradeRow == i);
        QRect rr(200, ry, 184, 28);
        painter.fillRect(rr, sel ? QColor(225, 120, 25, 45) : QColor(24, 23, 28));
        painter.setPen(QPen(sel ? QColor(245, 175, 45) : QColor(48, 46, 54), 1));
        painter.drawRect(rr);

        painter.setFont(QFont("Arial", 6, QFont::Bold));
        painter.setPen(Qt::white);
        painter.drawText(QRect(206, ry + 3, 110, 11), Qt::AlignLeft, rows[i].name);

        QString costTxt = (rows[i].lvl >= 5) ? "MAX" : QString("₹%1").arg(GarageSystem::upgradeCost(rows[i].lvl));
        painter.setPen(rows[i].lvl >= 5 ? QColor(90, 210, 100) : QColor(255, 200, 60));
        painter.drawText(QRect(318, ry + 3, 60, 11), Qt::AlignRight, costTxt);

        for (int p = 0; p < 5; ++p) {
            painter.fillRect(QRect(206 + p * 18, ry + 17, 14, 5),
                             (p < rows[i].lvl) ? QColor(245, 165, 35) : QColor(40, 40, 48));
        }
    }

    {
        int ry = 198;
        bool sel = (garageUpgradeRow == 4);
        QRect rr(200, ry, 184, 26);
        painter.fillRect(rr, sel ? QColor(60, 160, 80, 50) : QColor(24, 23, 28));
        painter.setPen(QPen(sel ? QColor(90, 220, 110) : QColor(48, 46, 54), 1));
        painter.drawRect(rr);

        int curHull = static_cast<int>(garage.currentHull().integrity);
        int repairCost = std::max(0, 100 - curHull);
        painter.setFont(QFont("Arial", 6, QFont::Bold));
        painter.setPen(QColor(210, 230, 215));
        painter.drawText(QRect(206, ry + 6, 116, 14), Qt::AlignLeft, QString("REPAIR HULL (%1%)").arg(curHull));
        painter.setPen(QColor(110, 235, 130));
        painter.drawText(QRect(320, ry + 6, 58, 14), Qt::AlignRight, repairCost > 0 ? QString("₹%1").arg(repairCost) : "FULL");
    }

    if (garageMsgTimer > 0.0 && !garageStatusMsg.isEmpty()) {
        painter.setFont(QFont("Arial", 6, QFont::Bold));
        painter.setPen(QColor(255, 215, 70));
        painter.drawText(QRect(200, 228, 184, 12), Qt::AlignCenter, garageStatusMsg);
    }

    painter.setFont(QFont("Arial", 6, QFont::Bold));
    painter.setPen(QColor(185, 185, 190));
    painter.drawText(QRect(0, 258, BUFFER_WIDTH, 14), Qt::AlignCenter,
                     "[A/D] Vehicle  •  [W/S] Select Upgrade  •  [U] Buy  •  [1/2/3] Sector");
    painter.setPen(QColor(245, 175, 45));
    painter.drawText(QRect(0, 274, BUFFER_WIDTH, 14), Qt::AlignCenter,
                     "[ENTER / SPACE] LAUNCH ESCAPE   •   [ESC] MAIN MENU");
}

void GameCanvas::renderRaceFinishedOverlay()
{
    QPainter painter(&screenBuffer);
    painter.setRenderHint(QPainter::Antialiasing, false);

    QRect bannerRect(36, 76, BUFFER_WIDTH - 72, 144);
    painter.fillRect(bannerRect, QColor(12, 10, 14, 242));
    painter.setPen(QPen(didSurviveRun ? QColor(80, 220, 100) : QColor(235, 55, 45), 2));
    painter.drawRect(bannerRect);

    painter.setFont(QFont("Courier New", 13, QFont::Bold));
    painter.setPen(didSurviveRun ? QColor(110, 240, 120) : QColor(255, 75, 60));
    painter.drawText(QRect(36, 84, BUFFER_WIDTH - 72, 22), Qt::AlignCenter,
                     didSurviveRun ? "KOLKATA EVACUATED!" : "VEHICLE LOST IN RUINS");

    painter.setFont(QFont("Arial", 7, QFont::Bold));
    painter.setPen(QColor(235, 175, 55));
    painter.drawText(QRect(44, 108, BUFFER_WIDTH - 88, 14), Qt::AlignCenter, finishReason);

    painter.setPen(Qt::white);
    painter.drawText(QRect(44, 128, BUFFER_WIDTH - 88, 14), Qt::AlignCenter,
                     QString("RANK #%1  •  PROGRESS %2%  •  HULL %3%")
                         .arg(playerPlacement)
                         .arg(static_cast<int>(splineProgress * 100.0))
                         .arg(static_cast<int>(playerHull.integrity)));

    painter.setPen(QColor(255, 210, 75));
    painter.drawText(QRect(44, 144, BUFFER_WIDTH - 88, 14), Qt::AlignCenter,
                     QString("TIME: %1s   •   SCRAP COLLECTED: +₹%2")
                         .arg(QString::number(raceElapsedTime, 'f', 1))
                         .arg(scrapCollectedThisRun));

    QString resumeTxt = activeCheckpointSave.valid
        ? QString("PRESS [R] TO RESUME FROM CHECKPOINT %1").arg(activeCheckpointSave.checkpointIdx)
        : "PRESS [R] TO RETRY STAGE";
    painter.setPen(QColor(180, 220, 255));
    painter.drawText(QRect(44, 168, BUFFER_WIDTH - 88, 14), Qt::AlignCenter, resumeTxt);
    painter.setPen(QColor(160, 165, 175));
    painter.drawText(QRect(44, 184, BUFFER_WIDTH - 88, 14), Qt::AlignCenter, "PRESS [SHIFT+R] FOR FRESH RESTART");
    painter.setPen(QColor(245, 165, 40));
    painter.drawText(QRect(44, 200, BUFFER_WIDTH - 88, 14), Qt::AlignCenter, "PRESS [ENTER] OR [ESC] FOR GARAGE");
}

// -------------------------------------------------------------
// LAN MULTIPLAYER ESCAPE LOBBY & SYNCHRONIZED CHECKPOINTS
// -------------------------------------------------------------

QString GameCanvas::generateSecretCode()
{
    static const char alphanum[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    QString code;
    for (int i = 0; i < 5; ++i) {
        code += alphanum[QRandomGenerator::global()->bounded(static_cast<int>(sizeof(alphanum) - 1))];
    }
    return code;
}

void GameCanvas::initNetworkManager()
{
    networkManager = new NetworkManager(this);
    connect(networkManager, &NetworkManager::lobbyUpdated, this, [this]() { update(); });
    connect(networkManager, &NetworkManager::raceStartTriggered, this, [this](int stageLevel) {
        startMultiplayerRace(stageLevel);
    });
    connect(networkManager, &NetworkManager::remoteKartUpdated, this, [this](int) {
        updateRemoteKarts(0.0);
    });
    connect(networkManager, &NetworkManager::remotePlayerFinished, this, [this](int, double, int) {
        updateRemoteKarts(0.0);
    });
    connect(networkManager, &NetworkManager::checkpointLobbyUpdated, this, [this]() {
        update();
    });
    connect(networkManager, &NetworkManager::checkpointRolloutTriggered, this, [this](int) {
        if (currentState == GameState::CHECKPOINT_PIT) {
            rolloutFromCheckpointPit();
        }
    });
    connect(networkManager, &NetworkManager::returnedToLobby, this, [this]() {
        isNetworkRace = false;
        isRaceFinished = false;
        currentState = GameState::MULTIPLAYER_LOBBY;
        multiLobbyState = (networkManager->isHost()) ? MultiLobbyState::HOST_SETUP : MultiLobbyState::WAITING_ROOM;
        update();
    });
}

void GameCanvas::startMultiplayerRace(int stageLevel)
{
    isNetworkRace = true;
    selectedGameMode = GameMode::MULTIPLAYER;
    currentStage = static_cast<KolkataStage>(std::clamp(stageLevel, 0, 2));

    int myId = networkManager ? networkManager->getLocalPlayerId() : 0;
    if (networkManager) {
        const NetPlayer *me = networkManager->getPlayer(myId);
        if (me) selectedVehicleP1 = me->characterId;
    }

    activeCheckpointSave.valid = false;
    startRace(false);
    isNetworkRace = true;
    aiOpponents.clear();

    if (networkManager && currentWaypoints.size() >= 2) {
        double startX = currentWaypoints[0].x();
        double startZ = currentWaypoints[0].y();
        double startAngle = std::atan2(currentWaypoints[1].y() - startZ, currentWaypoints[1].x() - startX);
        double fwdX = std::cos(startAngle), fwdZ = std::sin(startAngle);
        double rightX = -fwdZ, rightZ = fwdX;

        kartX = startX + fwdX * (-12.0 * myId) + rightX * ((myId % 2 == 0) ? -10.0 : 10.0);
        kartZ = startZ + fwdZ * (-12.0 * myId) + rightZ * ((myId % 2 == 0) ? -10.0 : 10.0);
        kartAngle = startAngle;
    }
    updateRemoteKarts(0.0);
}

void GameCanvas::updateRemoteKarts(double)
{
    if (!networkManager || !isNetworkRace) return;
    int myId = networkManager->getLocalPlayerId();
    remoteKarts.clear();
    for (const auto &np : networkManager->getPlayers()) {
        if (np.id == myId) continue;
        KartEntity rk;
        rk.id = np.id;
        rk.isHuman = true;
        rk.isAI = false;
        rk.vehicleId = np.characterId;
        rk.characterId = np.characterId;
        rk.name = np.name;
        rk.kartColor = (np.characterId >= 0 && np.characterId < static_cast<int>(vehicles.size()))
            ? vehicles[np.characterId].primaryColor : QColor(240, 180, 30);
        rk.x = np.x;
        rk.z = np.z;
        rk.angle = np.angle;
        rk.speed = np.speed;
        rk.isSpinning = np.isSpinning;
        rk.isDrifting = np.isDrifting;
        rk.raceFinished = np.raceFinished;
        rk.finishTime = np.finishTime;
        rk.placement = np.placement;
        rk.hull.integrity = np.hull;
        rk.isEliminated = np.isEliminated;
        rk.progressMeters = np.progressMeters;
        remoteKarts.push_back(rk);
    }
}

void GameCanvas::renderMultiplayerLobby()
{
    QPainter painter(&screenBuffer);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(QRect(0, 0, BUFFER_WIDTH, BUFFER_HEIGHT), QColor(12, 11, 14));

    painter.setPen(QPen(QColor(225, 130, 25), 2));
    painter.drawLine(0, 34, BUFFER_WIDTH, 34);
    painter.setFont(QFont("Courier New", 11, QFont::Bold));
    painter.setPen(QColor(245, 175, 45));
    painter.drawText(QRect(0, 8, BUFFER_WIDTH, 22), Qt::AlignCenter, "LAN ESCAPE CONVOY LOBBY");

    if (multiLobbyState == MultiLobbyState::MODE_CHOICE) {
        const char* opts[2] = { "1. HOST EVACUATION LOBBY", "2. JOIN BY SECRET CODE" };
        for (int i = 0; i < 2; ++i) {
            int y = 92 + i * 48;
            bool sel = (multiChoiceIndex == i);
            painter.fillRect(QRect(60, y, 280, 36), sel ? QColor(225, 120, 25, 55) : QColor(20, 18, 24));
            painter.setPen(QPen(sel ? QColor(245, 175, 45) : QColor(65, 65, 75), 1));
            painter.drawRect(QRect(60, y, 280, 36));
            painter.setFont(QFont("Arial", 8, QFont::Bold));
            painter.setPen(sel ? Qt::white : QColor(150, 150, 160));
            painter.drawText(QRect(60, y, 280, 36), Qt::AlignCenter, opts[i]);
        }
        painter.setFont(QFont("Arial", 6));
        painter.setPen(QColor(170, 170, 175));
        painter.drawText(QRect(0, 260, BUFFER_WIDTH, 16), Qt::AlignCenter, "[W/S] Select  •  [ENTER] Confirm  •  [ESC] Back");
    } else if (multiLobbyState == MultiLobbyState::JOIN_INPUT) {
        painter.setFont(QFont("Arial", 8, QFont::Bold));
        painter.setPen(QColor(235, 165, 45));
        painter.drawText(QRect(40, 72, 320, 18), Qt::AlignCenter, "ENTER SURVIVOR CALLSIGN & CONVOY CODE");

        auto drawInput = [&](int y, const QString &lbl, const QString &val, bool active) {
            painter.fillRect(QRect(60, y, 280, 32), QColor(18, 17, 22));
            painter.setPen(QPen(active ? QColor(245, 175, 45) : QColor(60, 60, 70), 1));
            painter.drawRect(QRect(60, y, 280, 32));
            painter.setFont(QFont("Arial", 6, QFont::Bold));
            painter.setPen(QColor(170, 170, 180));
            painter.drawText(QRect(68, y + 4, 260, 10), Qt::AlignLeft, lbl);
            painter.setFont(QFont("Courier New", 9, QFont::Bold));
            painter.setPen(Qt::white);
            painter.drawText(QRect(68, y + 15, 260, 14), Qt::AlignLeft, val + (active ? "_" : ""));
        };
        drawInput(105, "CALLSIGN", inputPlayerName, activeInputField == 0);
        drawInput(150, "SECRET CODE", inputSecretCode, activeInputField == 1);

        painter.setFont(QFont("Arial", 6));
        painter.setPen(QColor(170, 170, 175));
        painter.drawText(QRect(0, 260, BUFFER_WIDTH, 16), Qt::AlignCenter, "[TAB] Switch Field  •  [ENTER] Connect  •  [ESC] Back");
    } else {
        bool isHost = (multiLobbyState == MultiLobbyState::HOST_SETUP);
        painter.setFont(QFont("Courier New", 9, QFont::Bold));
        painter.setPen(QColor(245, 185, 55));
        painter.drawText(QRect(16, 42, 368, 16), Qt::AlignCenter,
                         QString("CODE: %1  |  IP: %2")
                             .arg(networkManager ? networkManager->getSecretCode() : currentSecretCode)
                             .arg(networkManager ? networkManager->getLocalIP() : "127.0.0.1"));

        int stg = networkManager ? networkManager->getSelectedTrack() : static_cast<int>(currentStage);
        QString stgName = (stg == 0) ? "GARIAHAT (MEDIUM)" : (stg == 1 ? "PARK ST (HARD)" : "HOWRAH BRIDGE (INSANE)");
        painter.setFont(QFont("Arial", 7, QFont::Bold));
        painter.setPen(QColor(220, 140, 50));
        painter.drawText(QRect(16, 62, 368, 14), Qt::AlignCenter, "START SECTOR: " + stgName);

        if (networkManager) {
            const auto &plist = networkManager->getPlayers();
            for (size_t i = 0; i < plist.size() && i < 4; ++i) {
                int py = 86 + static_cast<int>(i) * 32;
                painter.fillRect(QRect(36, py, 328, 26), QColor(20, 19, 25));
                painter.setPen(QPen(QColor(70, 68, 78), 1));
                painter.drawRect(QRect(36, py, 328, 26));
                painter.setFont(QFont("Arial", 7, QFont::Bold));
                painter.setPen(Qt::white);
                QString rName = (plist[i].characterId >= 0 && plist[i].characterId < static_cast<int>(vehicles.size()))
                    ? vehicles[plist[i].characterId].name : "RIG";
                painter.drawText(QRect(44, py + 6, 170, 14), Qt::AlignLeft, QString("%1 (%2)").arg(plist[i].name, rName));
                painter.setPen(plist[i].isReady ? QColor(90, 225, 110) : QColor(235, 165, 45));
                painter.drawText(QRect(240, py + 6, 116, 14), Qt::AlignRight, plist[i].isReady ? "READY" : "PREPARING");
            }
        }
        painter.setFont(QFont("Arial", 6));
        painter.setPen(QColor(180, 180, 185));
        painter.drawText(QRect(0, 260, BUFFER_WIDTH, 16), Qt::AlignCenter,
                         isHost ? "[A/D] Rig  •  [1-3] Stage  •  [ENTER] Launch Convoy  •  [ESC] Cancel"
                                : "[A/D] Rig  •  [SPACE] Ready  •  [ESC] Leave");
    }
}

void GameCanvas::renderMultiplayerRankings()
{
    QPainter painter(&screenBuffer);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(QRect(30, 50, BUFFER_WIDTH - 60, 200), QColor(12, 10, 14, 240));
    painter.setPen(QPen(QColor(245, 165, 35), 2));
    painter.drawRect(QRect(30, 50, BUFFER_WIDTH - 60, 200));

    painter.setFont(QFont("Courier New", 11, QFont::Bold));
    painter.setPen(QColor(245, 185, 50));
    painter.drawText(QRect(30, 58, BUFFER_WIDTH - 60, 20), Qt::AlignCenter, "EVACUATION CONVOY RANKINGS");

    if (networkManager) {
        auto plist = networkManager->getPlayers();
        std::sort(plist.begin(), plist.end(), [](const NetPlayer &a, const NetPlayer &b) {
            if (a.isEliminated != b.isEliminated) return !a.isEliminated;
            if (a.raceFinished != b.raceFinished) return a.raceFinished;
            if (a.raceFinished && b.raceFinished) return a.finishTime < b.finishTime;
            return a.progressMeters > b.progressMeters;
        });

        for (size_t i = 0; i < plist.size() && i < 4; ++i) {
            int y = 88 + static_cast<int>(i) * 30;
            painter.fillRect(QRect(48, y, BUFFER_WIDTH - 96, 24), QColor(22, 20, 26));
            painter.setFont(QFont("Arial", 7, QFont::Bold));
            painter.setPen(Qt::white);
            painter.drawText(QRect(56, y + 5, 160, 14), Qt::AlignLeft,
                             QString("#%1  %2").arg(i + 1).arg(plist[i].name));
            painter.setPen(plist[i].isEliminated ? QColor(255, 70, 60) : QColor(110, 235, 120));
            QString status = plist[i].isEliminated ? "ELIMINATED"
                : (plist[i].raceFinished ? QString("%1s").arg(QString::number(plist[i].finishTime, 'f', 1)) : "ESCAPING...");
            painter.drawText(QRect(230, y + 5, 110, 14), Qt::AlignRight, status);
        }
    }

    painter.setFont(QFont("Arial", 6, QFont::Bold));
    painter.setPen(QColor(245, 175, 45));
    painter.drawText(QRect(30, 224, BUFFER_WIDTH - 60, 16), Qt::AlignCenter, "[ENTER / ESC] RETURN TO LOBBY");
}

void GameCanvas::handleMultiplayerLobbyInput(QKeyEvent *event)
{
    int key = event->key();
    if (!networkManager) return;

    if (multiLobbyState == MultiLobbyState::MODE_CHOICE) {
        if (key == Qt::Key_W || key == Qt::Key_Up || key == Qt::Key_S || key == Qt::Key_Down) {
            multiChoiceIndex = 1 - multiChoiceIndex;
        } else if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) {
            if (multiChoiceIndex == 0) {
                currentSecretCode = generateSecretCode();
                networkManager->startHosting(currentSecretCode, hostPlayerName, static_cast<int>(currentStage));
                multiLobbyState = MultiLobbyState::HOST_SETUP;
            } else {
                multiLobbyState = MultiLobbyState::JOIN_INPUT;
                activeInputField = 1;
            }
        } else if (key == Qt::Key_Escape) {
            currentState = GameState::TITLE_MENU;
        }
    } else if (multiLobbyState == MultiLobbyState::JOIN_INPUT) {
        if (key == Qt::Key_Tab) {
            activeInputField = 1 - activeInputField;
        } else if (key == Qt::Key_Backspace) {
            QString &target = (activeInputField == 0) ? inputPlayerName : inputSecretCode;
            if (!target.isEmpty()) target.chop(1);
        } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            if (!inputSecretCode.trimmed().isEmpty()) {
                networkManager->startJoining(inputSecretCode.trimmed().toUpper(), inputPlayerName.trimmed());
                multiLobbyState = MultiLobbyState::WAITING_ROOM;
            }
        } else if (key == Qt::Key_Escape) {
            multiLobbyState = MultiLobbyState::MODE_CHOICE;
        } else if (!event->text().isEmpty() && event->text().at(0).isPrint()) {
            QString &target = (activeInputField == 0) ? inputPlayerName : inputSecretCode;
            if (target.length() < 12) target += event->text().toUpper();
        }
    } else {
        bool isHost = (multiLobbyState == MultiLobbyState::HOST_SETUP);
        if (key == Qt::Key_A || key == Qt::Key_Left) {
            selectedVehicleP1 = (selectedVehicleP1 - 1 + 4) % 4;
            networkManager->setCharacter(selectedVehicleP1);
        } else if (key == Qt::Key_D || key == Qt::Key_Right) {
            selectedVehicleP1 = (selectedVehicleP1 + 1) % 4;
            networkManager->setCharacter(selectedVehicleP1);
        } else if (isHost && (key == Qt::Key_1 || key == Qt::Key_2 || key == Qt::Key_3)) {
            int stg = (key == Qt::Key_1) ? 0 : (key == Qt::Key_2 ? 1 : 2);
            currentStage = static_cast<KolkataStage>(stg);
            networkManager->setTrackLevel(stg);
        } else if (isHost && (key == Qt::Key_Return || key == Qt::Key_Enter)) {
            networkManager->startRace();
        } else if (!isHost && key == Qt::Key_Space) {
            const NetPlayer *me = networkManager->getPlayer(networkManager->getLocalPlayerId());
            networkManager->setReady(me ? !me->isReady : true);
        } else if (key == Qt::Key_Escape) {
            networkManager->disconnectNetwork();
            multiLobbyState = MultiLobbyState::MODE_CHOICE;
        }
    }
    update();
}
