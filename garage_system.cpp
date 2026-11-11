// ============================================================================
// KOLKATA: LAST EXIT — Garage System  (Implementation)
// ============================================================================

#include "garage_system.h"
#include <algorithm>

// ============================================================================
// Singleton
// ============================================================================

GarageSystem& GarageSystem::instance()
{
    static GarageSystem s_instance;
    return s_instance;
}

// ============================================================================
// Construction / Initialization
// ============================================================================

GarageSystem::GarageSystem()
{
    initVehicleRoster();
    loadFromSettings();
}

void GarageSystem::initVehicleRoster()
{
    m_vehicles.clear();
    m_vehicles.resize(4);

    // -----------------------------------------------------------------
    // ID 0 — KAALI-PEELI MK-IV  (Yellow Ambassador Taxi)
    //         Archetype: BALANCED  |  3 SPD / 3 ACC / 4 HND / 3 ARM
    // -----------------------------------------------------------------
    {
        VehicleDef& v = m_vehicles[0];
        v.id             = 0;
        v.name           = QStringLiteral("KAALI-PEELI MK-IV");
        v.archetype      = QStringLiteral("BALANCED");
        v.description    = QStringLiteral("The immortal yellow-and-black Ambassador taxi, "
                                          "retro-fitted with salvage steel plating. "
                                          "A Kolkata survivor's first ride.");
        v.primaryColor   = QColor(240, 200, 20);   // Taxi yellow
        v.secondaryColor = QColor(20, 20, 20);      // Black bonnet stripe
        v.altColor       = QColor(200, 160, 10);    // Alt: darker gold

        v.maxSpeed       = 155.0;   // km/h  (mid-range)
        v.accel          = 105.0;   // units/s²
        v.turnRate       = 3.0;     // rad/s — slightly above average handling
        v.driftFactor    = 1.0;     // neutral drift
        v.weight         = 1.0;     // baseline
        v.armorRating    = 0.15;    // modest hull plating

        v.speedPips      = 3;
        v.accelPips      = 3;
        v.handlingPips   = 4;
        v.armorPips      = 3;
    }

    // -----------------------------------------------------------------
    // ID 1 — SHIBPUR DOZER RIG  (Armored Salvage Truck)
    //         Archetype: HEAVYWEIGHT  |  5 SPD / 1 ACC / 1 HND / 5 ARM
    // -----------------------------------------------------------------
    {
        VehicleDef& v = m_vehicles[1];
        v.id             = 1;
        v.name           = QStringLiteral("SHIBPUR DOZER RIG");
        v.archetype      = QStringLiteral("HEAVYWEIGHT");
        v.description    = QStringLiteral("Six-wheel salvage truck from the Shibpur scrapyard. "
                                          "Sluggish off the line, unstoppable at speed.");
        v.primaryColor   = QColor(80, 90, 80);      // Gunmetal grey-green
        v.secondaryColor = QColor(200, 60, 20);     // Rust-red hazard stripe
        v.altColor       = QColor(60, 60, 90);      // Alt: dark steel blue

        v.maxSpeed       = 180.0;   // Highest top speed — freight train at full tilt
        v.accel          = 60.0;    // Very sluggish off the line
        v.turnRate       = 1.8;     // Wide, ponderous steering
        v.driftFactor    = 0.6;     // Barely drifts — ploughs forward
        v.weight         = 1.8;     // Heavy — pushes rivals aside
        v.armorRating    = 0.35;    // Thick hull, shrugs off impacts

        v.speedPips      = 5;
        v.accelPips      = 1;
        v.handlingPips   = 1;
        v.armorPips      = 5;
    }

    // -----------------------------------------------------------------
    // ID 2 — PARK ST. RUNNER  (Stripped Street Coupe)
    //         Archetype: AGILITY  |  3 SPD / 4 ACC / 5 HND / 2 ARM
    // -----------------------------------------------------------------
    {
        VehicleDef& v = m_vehicles[2];
        v.id             = 2;
        v.name           = QStringLiteral("PARK ST. RUNNER");
        v.archetype      = QStringLiteral("AGILITY");
        v.description    = QStringLiteral("Lightweight coupe stripped to the roll-cage. "
                                          "Threads through wreckage like a needle.");
        v.primaryColor   = QColor(30, 140, 220);    // Electric blue
        v.secondaryColor = QColor(255, 255, 255);   // White racing stripe
        v.altColor       = QColor(220, 50, 80);     // Alt: crimson

        v.maxSpeed       = 150.0;   // Moderate top speed
        v.accel          = 115.0;   // Decent punch off the line
        v.turnRate       = 3.6;     // Razor-sharp cornering
        v.driftFactor    = 1.4;     // Loose, slidey — reward for drift mastery
        v.weight         = 0.65;    // Featherweight — gets bumped easily
        v.armorRating    = 0.08;    // Tissue-paper hull

        v.speedPips      = 3;
        v.accelPips      = 4;
        v.handlingPips   = 5;
        v.armorPips      = 2;
    }

    // -----------------------------------------------------------------
    // ID 3 — SSKM MED-VAN  (Rescue Ambulance)
    //         Archetype: ACCELERATION  |  2 SPD / 5 ACC / 4 HND / 3 ARM
    // -----------------------------------------------------------------
    {
        VehicleDef& v = m_vehicles[3];
        v.id             = 3;
        v.name           = QStringLiteral("SSKM MED-VAN");
        v.archetype      = QStringLiteral("ACCELERATION");
        v.description    = QStringLiteral("SSKM Hospital emergency van, loaded with medical "
                                          "salvage. Explosive acceleration, limited top end.");
        v.primaryColor   = QColor(230, 230, 230);   // White
        v.secondaryColor = QColor(200, 30, 30);     // Red cross stripe
        v.altColor       = QColor(40, 180, 60);     // Alt: green cross variant

        v.maxSpeed       = 140.0;   // Low ceiling — acceleration-oriented
        v.accel          = 135.0;   // Blistering burst speed
        v.turnRate       = 3.2;     // Good handling
        v.driftFactor    = 1.1;     // Slight oversteer
        v.weight         = 1.1;     // Medium-light
        v.armorRating    = 0.15;    // Decent hull

        v.speedPips      = 2;
        v.accelPips      = 5;
        v.handlingPips   = 4;
        v.armorPips      = 3;
    }
}

// ============================================================================
// Vehicle Roster Accessors
// ============================================================================

const QVector<VehicleDef>& GarageSystem::vehicleRoster() const
{
    return m_vehicles;
}

VehicleDef GarageSystem::selectedVehicleDef() const
{
    int idx = std::clamp(m_state.selectedVehicle, 0, static_cast<int>(m_vehicles.size()) - 1);
    VehicleDef copy = m_vehicles[idx];
    const_cast<GarageSystem*>(this)->applyUpgrades(copy);   // bake upgrades into the copy
    return copy;
}

void GarageSystem::selectVehicle(int id)
{
    if (id >= 0 && id < m_vehicles.size())
        m_state.selectedVehicle = id;
}

int GarageSystem::selectedVehicle() const
{
    return m_state.selectedVehicle;
}

// ============================================================================
// Salvage Scrap (₹) Economy
// ============================================================================

void GarageSystem::addScrap(int amount)
{
    if (amount > 0)
        m_state.scrapBalance += amount;
}

int GarageSystem::getScrapBalance() const
{
    return m_state.scrapBalance;
}

bool GarageSystem::canAfford(int cost) const
{
    return m_state.scrapBalance >= cost;
}

int GarageSystem::awardRaceScrap(int placement, int onRoadScrap)
{
    // Placement payouts: 1st = 500₹, 2nd = 300₹, 3rd = 150₹, 4th = 50₹
    static const int payouts[] = { 500, 300, 150, 50 };

    int payout = 0;
    if (placement >= 1 && placement <= 4)
        payout = payouts[placement - 1];

    int total = payout + std::max(0, onRoadScrap);
    addScrap(total);
    return total;
}

// ============================================================================
// Upgrade System
// ============================================================================

const GarageUpgrades& GarageSystem::upgrades() const
{
    return m_state.upgrades;
}

int GarageSystem::upgradeCost(int currentLevel)
{
    return GarageState::upgradeCost(currentLevel);
}

bool GarageSystem::tryUpgrade(int& levelRef)
{
    if (levelRef >= 5)
        return false;   // Already maxed out

    int cost = GarageState::upgradeCost(levelRef);
    if (!canAfford(cost))
        return false;   // Insufficient scrap

    m_state.scrapBalance -= cost;
    ++levelRef;
    return true;
}

bool GarageSystem::upgradeArmor()
{
    return tryUpgrade(m_state.upgrades.armorLevel);
}

bool GarageSystem::upgradeEngine()
{
    return tryUpgrade(m_state.upgrades.engineLevel);
}

bool GarageSystem::upgradeSuspension()
{
    return tryUpgrade(m_state.upgrades.suspensionLevel);
}

bool GarageSystem::upgradeNitrous()
{
    return tryUpgrade(m_state.upgrades.nitrousLevel);
}

// ============================================================================
// Apply Upgrades to Vehicle Stats
// ============================================================================

void GarageSystem::applyUpgrades(VehicleDef& vehicle) const
{
    const GarageUpgrades& u = m_state.upgrades;

    // --- Armor: −8 % hull damage taken per level ---
    // Additive on base armorRating, hard-clamped to the engine limit of 0.80.
    if (u.armorLevel > 0) {
        vehicle.armorRating += 0.08 * u.armorLevel;
        vehicle.armorRating = std::min(vehicle.armorRating, 0.80);
    }

    // --- Engine: +3 % maxSpeed and +5 % accel per level ---
    if (u.engineLevel > 0) {
        vehicle.maxSpeed *= (1.0 + 0.03 * u.engineLevel);
        vehicle.accel    *= (1.0 + 0.05 * u.engineLevel);
    }

    // --- Suspension: reduces off-road / rubble speed penalty ---
    // We encode this as a driftFactor reduction: lower driftFactor → less
    // lateral slide on loose rubble surfaces.  Each level multiplies by 0.88
    // (≈ 12 % penalty reduction).
    if (u.suspensionLevel > 0) {
        double suspMult = 1.0;
        for (int i = 0; i < u.suspensionLevel; ++i)
            suspMult *= 0.88;
        vehicle.driftFactor *= suspMult;
    }

    // --- Nitrous: +15 % boost duration per level ---
    // There's no dedicated "boost duration" field on VehicleDef, so we
    // encode the multiplier in the weight field's fractional part.
    // At race start the race system reads:
    //     nitrousDurationMult = 1.0 + 0.15 * nitrousLevel
    // We also store the multiplier directly here for systems that query it.
    // (weight is intentionally left untouched)
    // The multiplier is consumed by the race loop:
    //     double nitrousDuration = BASE_NITROUS_SEC * (1.0 + 0.15 * nitrousLevel);
    // Since VehicleDef has no nitrous field, callers should read upgrades()
    // directly.  We keep this section as a documentation anchor.
    Q_UNUSED(u.nitrousLevel);
}

// ============================================================================
// Field Repair
// ============================================================================

bool GarageSystem::fieldRepair(int scrapCost)
{
    if (scrapCost <= 0) {
        // Free repair (e.g. first run of the session)
        m_hull.integrity  = 100.0;
        m_hull.crackLevel = 0;
        m_hull.isOnFire   = false;
        m_hull.fireTimer  = 0.0;
        return true;
    }

    if (!canAfford(scrapCost))
        return false;

    m_state.scrapBalance -= scrapCost;
    m_hull.integrity  = 100.0;
    m_hull.crackLevel = 0;
    m_hull.isOnFire   = false;
    m_hull.fireTimer  = 0.0;
    return true;
}

HullState& GarageSystem::currentHull()
{
    return m_hull;
}

const HullState& GarageSystem::currentHull() const
{
    return m_hull;
}

// ============================================================================
// Persistence — QSettings
// ============================================================================

static const QString kSettingsGroup   = QStringLiteral("Garage");
static const QString kScrapBalance    = QStringLiteral("scrapBalance");
static const QString kSelectedVehicle = QStringLiteral("selectedVehicle");
static const QString kArmorLevel      = QStringLiteral("armorLevel");
static const QString kEngineLevel     = QStringLiteral("engineLevel");
static const QString kSuspensionLevel = QStringLiteral("suspensionLevel");
static const QString kNitrousLevel    = QStringLiteral("nitrousLevel");

void GarageSystem::saveToSettings() const
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);

    settings.setValue(kScrapBalance,    m_state.scrapBalance);
    settings.setValue(kSelectedVehicle, m_state.selectedVehicle);
    settings.setValue(kArmorLevel,      m_state.upgrades.armorLevel);
    settings.setValue(kEngineLevel,     m_state.upgrades.engineLevel);
    settings.setValue(kSuspensionLevel, m_state.upgrades.suspensionLevel);
    settings.setValue(kNitrousLevel,    m_state.upgrades.nitrousLevel);

    settings.endGroup();
    settings.sync();
}

void GarageSystem::loadFromSettings()
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);

    m_state.scrapBalance    = settings.value(kScrapBalance,    0).toInt();
    m_state.selectedVehicle = std::clamp(settings.value(kSelectedVehicle, 0).toInt(), 0, 3);
    m_state.upgrades.armorLevel      = std::clamp(settings.value(kArmorLevel,      0).toInt(), 0, 5);
    m_state.upgrades.engineLevel     = std::clamp(settings.value(kEngineLevel,     0).toInt(), 0, 5);
    m_state.upgrades.suspensionLevel = std::clamp(settings.value(kSuspensionLevel, 0).toInt(), 0, 5);
    m_state.upgrades.nitrousLevel    = std::clamp(settings.value(kNitrousLevel,    0).toInt(), 0, 5);

    settings.endGroup();
}
