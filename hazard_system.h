// ============================================================================
// KOLKATA: LAST EXIT — Hazard System
// Manages all environmental threats: collapsing structures, track hazards,
// seismic tremors, crater stamping, player collision, and AI avoidance.
// ============================================================================

#ifndef HAZARD_SYSTEM_H
#define HAZARD_SYSTEM_H

#include "kolkata_types.h"

#include <QImage>
#include <QRandomGenerator>
#include <vector>
#include <cmath>

// ---------------------------------------------------------------------------
// Crater stamp data — used by game_canvas to burn craters into track visuals
// ---------------------------------------------------------------------------
struct CraterStamp {
    double x = 0.0;
    double z = 0.0;
    double radius = 20.0;
    bool stamped = false;   // true once the game_canvas has consumed this stamp
};

// ---------------------------------------------------------------------------
// Collision result returned by checkPlayerCollision()
// ---------------------------------------------------------------------------
struct CollisionResult {
    bool hit = false;
    double speedMultiplier = 1.0;   // e.g. 0.0 = full stop, -0.2 = bounce-back
    double pushX = 0.0;             // World-space displacement to apply
    double pushZ = 0.0;
    bool spinOut = false;
    double spinDuration = 0.0;
    bool resetToCheckpoint = false; // For sinkholes / fissures
    double damageDealt = 0.0;
};

// ---------------------------------------------------------------------------
// HazardSystem — central controller for every environmental threat
// ---------------------------------------------------------------------------
class HazardSystem
{
public:
    explicit HazardSystem();
    ~HazardSystem() = default;

    // -----------------------------------------------------------------------
    // Initialization — call once per stage
    // -----------------------------------------------------------------------

    /// Clear all hazards and reset seismic state for a new stage.
    void reset();

    /// Populate structures and hazards from level data.
    void addCollapsingStructure(const CollapsingStructure& s);
    void addTrackHazard(const TrackHazard& h);

    /// Set the current stage difficulty (scales seismic intensity, etc.).
    void setStageDifficulty(KolkataStage stage);

    // -----------------------------------------------------------------------
    // Per-frame updates
    // -----------------------------------------------------------------------

    /// Advance collapsing structure state machines.
    void updateCollapsing(double dt, double playerX, double playerZ);

    /// Advance all grounded track hazards (anim timers, eruption cycles).
    void updateHazards(double dt);

    /// Advance seismic tremor controller (decay + new quake scheduling).
    void updateSeismic(double dt);

    // -----------------------------------------------------------------------
    // Collision & interaction
    // -----------------------------------------------------------------------

    /// Full player-vs-hazard collision sweep; mutates hull on hit.
    CollisionResult checkPlayerCollision(double playerX, double playerZ,
                                         double playerAngle, double playerSpeed,
                                         HullState& hull);

    // -----------------------------------------------------------------------
    // AI hazard awareness
    // -----------------------------------------------------------------------

    /// Returns a lateral lane offset in [-1.0, +1.0] the AI should steer
    /// toward to dodge upcoming hazards along its look-ahead vector.
    double getHazardAvoidanceOffset(double aiX, double aiZ,
                                    double lookAheadX, double lookAheadZ) const;

    // -----------------------------------------------------------------------
    // Crater stamping interface
    // -----------------------------------------------------------------------

    /// Returns mutable reference so game_canvas can mark stamps as consumed.
    std::vector<CraterStamp>& getCraterStamps();
    const std::vector<CraterStamp>& getCraterStamps() const;

    // -----------------------------------------------------------------------
    // Seismic queries (for renderer)
    // -----------------------------------------------------------------------

    /// Manually trigger a quake of given intensity (0.0–1.0).
    void triggerQuake(double intensity);

    const SeismicState& getSeismicState() const;

    // -----------------------------------------------------------------------
    // Accessors for rendering
    // -----------------------------------------------------------------------

    const std::vector<CollapsingStructure>& getStructures() const;
    const std::vector<TrackHazard>& getTrackHazards() const;

private:
    // --- Internal helpers ---

    /// Distance² between two 2-D positions (avoids sqrt where possible).
    static double distSq(double x1, double z1, double x2, double z2);

    /// Euclidean distance helper.
    static double dist(double x1, double z1, double x2, double z2);

    /// Closest-point-on-segment to a point; returns the parametric t [0,1].
    static double pointSegmentT(double px, double pz,
                                 double ax, double az,
                                 double bx, double bz);

    /// Randomised quake interval (8–20 s) scaled by difficulty.
    double nextQuakeInterval() const;

    /// Randomised quake intensity (0.3–0.8) scaled by difficulty.
    double nextQuakeIntensity() const;

    // --- Data ---
    std::vector<CollapsingStructure> m_structures;
    std::vector<TrackHazard>         m_hazards;
    std::vector<CraterStamp>         m_craterStamps;
    SeismicState                     m_seismic;

    KolkataStage m_stage = KolkataStage::GARIAHAT_COLLAPSE;
    double       m_difficultyScale = 1.0;   // 1.0 / 1.35 / 1.7 per stage

    // GAS_VENT eruption cycle constants
    static constexpr double GAS_DORMANT_END  = 3.0;
    static constexpr double GAS_WARN_END     = 4.0;
    static constexpr double GAS_ERUPT_END    = 5.5;

    // Collision radii / thresholds
    static constexpr double FISSURE_CENTRE_RADIUS = 6.0;
    static constexpr double WIRE_SPARK_RADIUS     = 12.0;
};

#endif // HAZARD_SYSTEM_H
