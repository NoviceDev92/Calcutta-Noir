#ifndef KOLKATA_TYPES_H
#define KOLKATA_TYPES_H

#include <QString>
#include <QColor>
#include <QPointF>
#include <vector>
#include <cmath>

// ============================================================================
// KOLKATA: LAST EXIT — Core Type Definitions
// All Mario Kart types are replaced with apocalyptic survival equivalents.
// ============================================================================

// --- Game State Flow ---
enum class GameState {
    STORY_INTRO,        // Boot animated pixel-art story cutscene
    TITLE_MENU,
    GARAGE,             // Vehicle selection + upgrade shop
    RACING,             // Active Mode-7 escape gameplay loop
    CHECKPOINT_PIT,     // Interactive mid-run checkpoint repair & upgrade stop
    RACE_FINISHED,      // Extraction reached OR vehicle wrecked
    MULTIPLAYER_LOBBY   // LAN Multiplayer lobby
};

enum class MultiLobbyState {
    MODE_CHOICE,
    HOST_SETUP,
    JOIN_INPUT,
    WAITING_ROOM
};

enum class GameMode {
    RANKED_ESCAPE,      // Solo vs 3 AI rival survivor rigs
    MULTIPLAYER,        // LAN 2-4 players + optional AI backfill
    TIME_TRIAL          // Solo survival time trial (no collapse wave)
};

// --- Kolkata Stage Difficulty ---
enum class KolkataStage {
    GARIAHAT_COLLAPSE = 0,  // Level 1: Medium — Gariahat → Ballygunge → Park Circus
    PARK_STREET_RUINS = 1,  // Level 2: Hard   — Park Street → Chowringhee → Victoria Memorial
    HOWRAH_BRIDGE     = 2   // Level 3: Insane — BBD Bagh → Strand Road → Howrah Bridge
};

// Backward compatibility aliases for existing test suite
using TrackLevel = KolkataStage;
namespace TrackLevelCompat {
    static constexpr KolkataStage DONUT_PLAINS  = KolkataStage::GARIAHAT_COLLAPSE;
    static constexpr KolkataStage CHOCO_VALLEY  = KolkataStage::PARK_STREET_RUINS;
    static constexpr KolkataStage BOWSER_CASTLE = KolkataStage::HOWRAH_BRIDGE;
}

// --- Escape Vehicle Definitions ---
struct VehicleDef {
    int id;
    QString name;           // "KAALI-PEELI MK-IV", "SHIBPUR DOZER RIG", etc.
    QString archetype;      // "BALANCED", "HEAVYWEIGHT", "AGILITY", "ACCELERATION"
    QString description;
    QColor primaryColor;    // Main chassis/hood color
    QColor secondaryColor;  // Accent stripe / detail
    QColor altColor;        // Alt palette for duplicate picks (multiplayer)
    double maxSpeed;        // Base top speed (km/h)
    double accel;           // Forward acceleration (units/s²)
    double turnRate;        // Base steering rotation (rad/s)
    double driftFactor;     // Drift lateral slide multiplier
    double weight;          // Collision pushback resistance & ramming weight
    double armorRating;     // Base hull damage reduction multiplier (0.0 = no reduction, 1.0 = immune)
    int speedPips;          // 1–5 stat display
    int accelPips;
    int handlingPips;
    int armorPips;
};
// Backward compat alias
using CharacterDef = VehicleDef;

// --- On-Road Collectibles & Salvaged Gear ---
enum class SurvivalGear {
    NONE,
    NITROUS,        // Manual speed boost (SPACE/SHIFT)
    REPAIR_KIT,     // Immediately restores +25% Hull
    DOZER_RAM,      // 4s reinforced front bumper — smash wrecks with zero damage
    SHOCKWAVE_FLARE // Fires forward to detonate/clear a wreckage cluster
};
// Backward compat alias
using PlayerItem = SurvivalGear;
namespace PlayerItemCompat {
    static constexpr SurvivalGear NONE        = SurvivalGear::NONE;
    static constexpr SurvivalGear MUSHROOM    = SurvivalGear::NITROUS;
    static constexpr SurvivalGear ROCKET      = SurvivalGear::DOZER_RAM;
    static constexpr SurvivalGear BANANA      = SurvivalGear::NONE;
    static constexpr SurvivalGear GREEN_SHELL = SurvivalGear::SHOCKWAVE_FLARE;
    static constexpr SurvivalGear INK_BLOOPER = SurvivalGear::NONE;
}

// --- Hull / Chassis Damage System ---
struct HullState {
    double integrity = 100.0;       // 0.0 = wrecked, 100.0 = pristine
    bool isOnFire = false;          // Chassis fire debuff (tick damage)
    double fireTimer = 0.0;         // Remaining fire duration
    double overheatTimer = 0.0;     // Temporary overheat from grazing burning wrecks
    int crackLevel = 0;             // 0=none, 1=minor (<65%), 2=heavy (<30%) — windshield effect

    void takeDamage(double amount, double armorReduction = 0.0) {
        double effective = amount * (1.0 - std::clamp(armorReduction, 0.0, 0.8));
        integrity = std::max(0.0, integrity - effective);
        updateCrackLevel();
    }
    void repair(double amount) {
        integrity = std::min(100.0, integrity + amount);
        updateCrackLevel();
    }
    bool isWrecked() const { return integrity <= 0.0; }
    void updateCrackLevel() {
        if (integrity < 30.0) crackLevel = 2;
        else if (integrity < 65.0) crackLevel = 1;
        else crackLevel = 0;
    }
};

// --- Kart / Vehicle Entity (used for player, AI, and remote) ---
struct KartEntity {
    int id = 0;
    bool isHuman = true;
    bool isAI = false;
    int vehicleId = 0;          // Index into vehicles array
    QString name = "KAALI-PEELI";
    QColor kartColor = QColor(240, 200, 20);

    // Position & kinematics
    double x = 512.0;
    double z = 200.0;
    double angle = 0.0;        // radians (heading / yaw)
    double speed = 0.0;
    double steerAngle = 0.0;
    double maxSpeed = 160.0;
    double accel = 110.0;
    double brakeDecel = 240.0;
    double friction = 45.0;
    double turnRate = 2.8;
    double driftFactor = 1.0;
    double weight = 1.0;
    double armorRating = 0.0;

    // Drift mechanics
    bool isDrifting = false;
    double driftTimer = 0.0;
    double miniTurboTimer = 0.0;
    double driftHopTimer = 0.0;
    double lateralSlip = 0.0;
    int driftDirection = 0;

    // Hull & survival
    HullState hull;
    SurvivalGear heldGear = SurvivalGear::NONE;
    double nitrousBoostTimer = 0.0;
    double dozerRamTimer = 0.0;

    // Status effects
    bool isSpinning = false;
    double spinTimer = 0.0;
    bool isEliminated = false;
    double bananaGraceTimer = 0.0;  // Legacy compat

    // AI navigation
    int currentWaypoint = 0;
    int currentWaypointIndex = 0;
    double lookaheadDist = 50.0;

    // Race progression & ranking
    int lastCheckpointReached = 0;  // 0 = none, 1 = CP1, 2 = CP2
    double splineProgress = 0.0;    // 0.0 to 1.0 (monotonic route distance fraction)
    double progressMeters = 0.0;    // Absolute distance along spline
    bool raceFinished = false;
    int placement = 1;
    double totalRaceDistance = 0.0;
    double finishTime = 0.0;

    // Legacy compat fields
    int characterId = 0;   // Alias for vehicleId
    int currentLap = 1;
    int nextCheckpoint = 1;
    bool hasPassedHalfway = false;
    bool firstStartCrossed = false;
    bool isInked = false;
    double inkTimer = 0.0;
    double mushroomBoostTimer = 0.0;
    PlayerItem heldItem = PlayerItem::NONE;
};

// --- Projectile (Shockwave Flare / debris chunk) ---
struct Projectile {
    double x = 0.0;
    double z = 0.0;
    double vx = 0.0;
    double vz = 0.0;
    int bouncesLeft = 0;
    bool active = true;
    int ownerId = 0;
    double lifeTime = 4.0;
};
using ShellProjectile = Projectile;

// --- Collapsing Structure State Machine ---
enum class CollapseState {
    INTACT,     // Standing tall, no threat
    WARNING,    // Player within trigger radius — sparks, shudder, ground warning
    FALLING,    // Gravity drop animation
    WRECKED     // On the ground — permanent scorched obstacle
};

struct CollapsingStructure {
    double x = 0.0;
    double z = 0.0;
    double y = 40.0;        // Current height (falls from ~40 to 0)
    double baseY = 40.0;    // Original overhead height
    double width = 30.0;    // Collision/visual width
    int spriteIndex = 0;    // Which overhead sprite to draw
    CollapseState state = CollapseState::INTACT;
    double triggerRadius = 160.0;   // Proximity to begin WARNING
    double warningTimer = 0.0;
    double fallTimer = 0.0;
    double fallDuration = 0.8;      // Seconds to hit ground
    double hullDamage = 30.0;       // Damage dealt if hit mid-fall or on impact
    bool hasStampedCrater = false;
};

// --- Grounded Hazard Types ---
enum class HazardType {
    BURNING_TAXI,           // Yellow Ambassador on fire
    WRECKED_BUS,            // Flipped WBTC bus blocking lane
    DERAILED_TRAM,          // Kolkata tram wreckage
    BURNING_AUTO,           // Green/yellow auto-rickshaw on fire
    ROAD_FISSURE,           // Seismic crack / sinkhole
    GAS_VENT,               // Ruptured gas main (timed eruption cycle)
    LIVE_WIRE,              // Downed tram wire (spark spin-out)
    // Legacy compat
    LAVA_GEYSER = 100,
    CHOCOLATE_FOUNTAIN = 101,
    ROLLING_CANDY = 102
};

struct TrackHazard {
    double x = 0.0;
    double z = 0.0;
    HazardType type = HazardType::BURNING_TAXI;
    double animTimer = 0.0;
    bool isErupting = false;    // For GAS_VENT cycle
    double radius = 16.0;
    double hullDamage = 15.0;   // Damage on collision
    bool blocksLane = false;    // Full lane blocker (bus/tram) vs partial
};

// --- World Sprite (static placed objects in the world) ---
enum class SpriteType {
    // Survival Pickups
    SUPPLY_CRATE,           // Contains random survival gear
    REPAIR_KIT,             // Direct repair pickup
    NITROUS_CELL,           // Nitrous fuel cell
    SCRAP_CACHE,            // Currency pickup (+25-75 ₹)

    // Grounded Obstacles
    BURNING_TAXI,           // Yellow Ambassador on fire (3-frame anim)
    WRECKED_BUS,            // WBTC bus on its side
    DERAILED_TRAM,          // Kolkata tram wreckage
    BURNING_AUTO,           // Auto-rickshaw wreck
    STEEL_GIRDER,           // Fallen I-beam / scaffolding chunk
    CONCRETE_RUBBLE,        // Pile of concrete blocks
    ROAD_BARRIER,           // Steel/concrete jersey barrier

    // Monument Billboards (128x96+)
    VICTORIA_MEMORIAL,      // Cracked marble dome
    SHAHEED_MINAR,          // Leaning obelisk
    WRITERS_BUILDING,       // Burning red colonial facade
    HOWRAH_BRIDGE_TOWER,    // Cantilever tower frame
    GARIAHAT_FLYOVER_PIER,  // Broken flyover support pillar
    ESPLANADE_METRO,        // Collapsed Metro entrance
    ST_PAULS_CATHEDRAL,     // Broken Gothic spire
    THE_42_TOWER,           // Snapped skyscraper silhouette

    // Overhead Spans (160x64) — drive UNDER these
    FLYOVER_BEAM_OVERHEAD,      // Gariahat flyover cross-beam
    BRIDGE_TRUSS_OVERHEAD,      // Howrah Bridge steel cross-truss
    SCAFFOLD_OVERHEAD,          // Construction bamboo-steel scaffold

    // Falling / Collapsing variants (during WARNING/FALLING states)
    GIRDER_FALLING,
    SCAFFOLD_FALLING,
    BRIDGE_TRUSS_FALLING,

    // Vehicle Billboards (survivor rigs)
    RIG_KAALI_PEELI,        // Yellow taxi rig
    RIG_DOZER,              // Armored salvage truck
    RIG_RUNNER,             // Stripped street coupe
    RIG_MEDVAN,             // Rescue ambulance
    RIG_KAALI_PEELI_ALT,    // Alt palette
    RIG_DOZER_ALT,
    RIG_RUNNER_ALT,
    RIG_MEDVAN_ALT,

    // Legacy compat stubs (mapped to new types)
    QUESTION_BOX = 200,
    MUSHROOM,
    ROCKET,
    BANANA,
    GREEN_SHELL,
    INK_BLOOPER,
    GREEN_PIPE,
    THWOMP,
    OIL_SLICK,
    COIN,
    BANANA_DROPPED,
    SHELL_PROJECTILE,
    KART_MARIO,
    KART_BOWSER,
    KART_PEACH,
    KART_TOAD,
    KART_MARIO_ALT,
    KART_BOWSER_ALT,
    KART_PEACH_ALT,
    KART_TOAD_ALT,
    LAVA_GEYSER_BUBBLING,
    LAVA_GEYSER_ERUPTING,
    CHOCOLATE_FOUNTAIN,
    CHOCOLATE_CANDY
};

struct WorldSprite {
    double x;
    double z;
    double y;               // Height offset above ground (0 = ground level)
    SpriteType type;
    bool active;
    double respawnTimer;    // Time remaining before respawn (for pickup crates)
    int ownerId = -1;
    double immunityTimer = 0.0;
    double clearanceY = 0.0;   // For overhead spans: height of the passable gap underneath
    int animFrame = 0;         // Current animation frame for multi-frame sprites
};

// --- Garage / Economy System ---
struct GarageUpgrades {
    int armorLevel = 0;         // 0–5: Reduces hull damage taken
    int engineLevel = 0;        // 0–5: Increases top speed & accel
    int suspensionLevel = 0;    // 0–5: Reduces off-road/rubble speed penalty
    int nitrousLevel = 0;       // 0–5: Increases boost capacity & duration
};

struct GarageState {
    int scrapBalance = 0;       // Total Salvage Scrap (₹) currency
    GarageUpgrades upgrades;
    int selectedVehicle = 0;    // 0-3 vehicle index

    static int upgradeCost(int currentLevel) {
        // Progressive cost: 100, 250, 500, 850, 1200
        static const int costs[] = { 100, 250, 500, 850, 1200 };
        if (currentLevel < 0 || currentLevel >= 5) return 99999;
        return costs[currentLevel];
    }
};

// --- Seismic / Atmospheric State ---
struct SeismicState {
    double shakeX = 0.0;        // Screen-space horizontal jitter
    double shakeY = 0.0;        // Screen-space vertical jitter
    double pitchOffset = 0.0;   // Dynamic horizon tilt
    double intensity = 0.0;     // Current quake strength (0.0 – 1.0)
    double timer = 0.0;         // Time until next quake event
    double cooldown = 0.0;      // Delay between quakes
};

// --- Checkpoint Definition ---
struct CheckpointDef {
    double x1, z1;  // Line segment start
    double x2, z2;  // Line segment end
    int index;      // 1 = CP1, 2 = CP2
    double timeBonus; // Seconds added to collapse timer on crossing
};

// --- Continuous Campaign Checkpoint Save State ---
struct CheckpointSave {
    bool valid = false;
    KolkataStage stage = KolkataStage::GARIAHAT_COLLAPSE;
    int checkpointIdx = 0;      // 0 = stage start, 1 = CP1, 2 = CP2
    double x = 512.0;
    double z = 3800.0;
    double angle = -1.5708;
    double hullIntegrity = 100.0;
    double evacTimer = 180.0;
    double nitrousFuel = 60.0;
    double collapseWaveProgress = -0.08;
    double cumulativeTime = 0.0;
    int scrapThisRun = 0;
};

// --- 3D World-Space Particle (Mode-7 projected smoke, sparks, concrete dust) ---
enum class WorldParticleKind {
    SMOKE,
    EMBER,
    DUST,
    SPARK
};

struct WorldParticle {
    double x = 0.0;
    double y = 0.0;     // Height above ground
    double z = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double vz = 0.0;
    double life = 0.0;
    double maxLife = 1.0;
    double baseSize = 4.0;
    QRgb color = qRgb(160, 160, 160);
    WorldParticleKind kind = WorldParticleKind::SMOKE;
};

// --- Minimap HUD ---
#ifndef MINIMAP_HUD_DEFINED
#define MINIMAP_HUD_DEFINED
struct MinimapHUD {
    static constexpr int RADAR_X = 8;
    static constexpr int RADAR_Y = 8;
    static constexpr int RADAR_SIZE = 72;
    static constexpr int MAP_WORLD_SIZE = 4096;

    static QPointF worldToRadar(double worldX, double worldZ) {
        double clampedX = std::clamp(worldX, 0.0, (double)MAP_WORLD_SIZE);
        double clampedZ = std::clamp(worldZ, 0.0, (double)MAP_WORLD_SIZE);
        double rx = RADAR_X + (clampedX / (double)MAP_WORLD_SIZE) * (double)RADAR_SIZE;
        double ry = RADAR_Y + (clampedZ / (double)MAP_WORLD_SIZE) * (double)RADAR_SIZE;
        return QPointF(rx, ry);
    }
};
#endif

#endif // KOLKATA_TYPES_H

