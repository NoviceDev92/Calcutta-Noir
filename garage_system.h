#ifndef GARAGE_SYSTEM_H
#define GARAGE_SYSTEM_H

// ============================================================================
// KOLKATA: LAST EXIT — Garage System
// Vehicle roster, salvage scrap economy, upgrade paths, field repair,
// and persistence via QSettings.
// ============================================================================

#include "kolkata_types.h"
#include <QSettings>
#include <QVector>

class GarageSystem
{
public:
    // --- Singleton Accessor ---------------------------------------------------
    static GarageSystem& instance();

    // --- Vehicle Roster -------------------------------------------------------
    // Returns the full roster of 4 escape vehicles (immutable base stats).
    const QVector<VehicleDef>& vehicleRoster() const;

    // Returns a *copy* of the selected vehicle with upgrades already baked in.
    VehicleDef selectedVehicleDef() const;

    // Vehicle selection (0–3)
    void selectVehicle(int id);
    int  selectedVehicle() const;

    // --- Salvage Scrap (₹) Economy --------------------------------------------
    void addScrap(int amount);
    int  getScrapBalance() const;
    bool canAfford(int cost) const;

    // Scrap payout for a race placement (1st–4th) plus on-road scrap collected.
    // Returns the total scrap awarded.
    int awardRaceScrap(int placement, int onRoadScrap = 0);

    // --- Upgrade System (4 paths, Lv 0–5 each) --------------------------------
    // Each returns true on success, false if max level or insufficient scrap.
    bool upgradeArmor();
    bool upgradeEngine();
    bool upgradeSuspension();
    bool upgradeNitrous();

    // Read current upgrade levels
    const GarageUpgrades& upgrades() const;

    // Progressive cost lookup (delegates to GarageState::upgradeCost)
    static int upgradeCost(int currentLevel);

    // --- Apply Upgrades to a VehicleDef ----------------------------------------
    // Modifies the referenced vehicle's stats in-place based on current levels:
    //   Armor:      −8 % hull damage per level  (armorRating += 0.08 * lv, clamped to 0.80)
    //   Engine:     +3 % maxSpeed, +5 % accel per level
    //   Suspension: off-road penalty multiplier (stored as driftFactor adjustment)
    //   Nitrous:    +15 % boost duration per level (stored as weight-encoded field)
    void applyUpgrades(VehicleDef& vehicle) const;

    // --- Field Repair ----------------------------------------------------------
    // Spend scrap to start next run at full hull. Returns true on success.
    bool fieldRepair(int scrapCost);

    // Hull state for the current run (reset at race start via fieldRepair or free)
    HullState& currentHull();
    const HullState& currentHull() const;

    // --- Persistence (QSettings) -----------------------------------------------
    void saveToSettings() const;
    void loadFromSettings();

private:
    // Private ctor / rule-of-five defaults (singleton)
    GarageSystem();
    ~GarageSystem() = default;
    GarageSystem(const GarageSystem&) = delete;
    GarageSystem& operator=(const GarageSystem&) = delete;

    // Initializes the 4-vehicle roster with base stats.
    void initVehicleRoster();

    // Internal helper: attempt an upgrade on a specific level reference.
    bool tryUpgrade(int& levelRef);

    // --- Data -----------------------------------------------------------------
    GarageState  m_state;
    QVector<VehicleDef> m_vehicles;
    HullState    m_hull;            // Carried between garage ↔ race transitions
};

#endif // GARAGE_SYSTEM_H
