// ============================================================================
// KOLKATA: LAST EXIT — Hazard System Implementation
// ============================================================================

#include "hazard_system.h"

#include <algorithm>
#include <cmath>

// ============================================================================
// Construction / Reset
// ============================================================================

HazardSystem::HazardSystem()
{
    reset();
}

void HazardSystem::reset()
{
    m_structures.clear();
    m_hazards.clear();
    m_craterStamps.clear();

    m_seismic = SeismicState{};
    m_seismic.timer = nextQuakeInterval();

    m_stage = KolkataStage::GARIAHAT_COLLAPSE;
    m_difficultyScale = 1.0;
}

void HazardSystem::addCollapsingStructure(const CollapsingStructure& s)
{
    m_structures.push_back(s);
}

void HazardSystem::addTrackHazard(const TrackHazard& h)
{
    m_hazards.push_back(h);
}

void HazardSystem::setStageDifficulty(KolkataStage stage)
{
    m_stage = stage;
    switch (stage) {
    case KolkataStage::GARIAHAT_COLLAPSE: m_difficultyScale = 1.0;  break;
    case KolkataStage::PARK_STREET_RUINS: m_difficultyScale = 1.35; break;
    case KolkataStage::HOWRAH_BRIDGE:     m_difficultyScale = 1.70; break;
    }
}

// ============================================================================
// 1. Collapsing Structure State Machine
// ============================================================================

void HazardSystem::updateCollapsing(double dt, double playerX, double playerZ)
{
    for (auto& s : m_structures) {
        switch (s.state) {

        // ----- INTACT → WARNING when player enters trigger radius ----------
        case CollapseState::INTACT: {
            double d = dist(playerX, playerZ, s.x, s.z);
            if (d < s.triggerRadius) {
                s.state = CollapseState::WARNING;
                s.warningTimer = 0.0;
            }
            break;
        }

        // ----- WARNING (~1.5 s): structure shudders, sparks ----------------
        case CollapseState::WARNING: {
            s.warningTimer += dt;

            // Small random X jitter for rendering (±2 px visual shudder)
            // The renderer reads `s.x` directly; we leave the base position
            // unchanged and let the renderer add sin-based shudder using
            // warningTimer so the jitter is deterministic per-frame.

            if (s.warningTimer >= 1.5) {
                s.state = CollapseState::FALLING;
                s.fallTimer = 0.0;
            }
            break;
        }

        // ----- FALLING: gravity drop from baseY to 0 -----------------------
        case CollapseState::FALLING: {
            s.fallTimer += dt;

            // Quadratic ease-in: y = baseY * (1 - (t/dur)^2)
            double t = std::min(s.fallTimer / s.fallDuration, 1.0);
            s.y = s.baseY * (1.0 - t * t);

            if (s.fallTimer >= s.fallDuration) {
                s.state = CollapseState::WRECKED;
                s.y = 0.0;
                s.hasStampedCrater = false; // will be stamped on next query

                // Register a crater stamp for the game_canvas
                CraterStamp stamp;
                stamp.x = s.x;
                stamp.z = s.z;
                stamp.radius = s.width * 0.6; // Crater slightly smaller than structure width
                stamp.stamped = false;
                m_craterStamps.push_back(stamp);
            }
            break;
        }

        // ----- WRECKED: permanent ground obstacle (no per-frame logic) ------
        case CollapseState::WRECKED:
            s.hasStampedCrater = true;
            break;
        }
    }
}

// ============================================================================
// 2. Track Hazard Updates (per-type behaviours)
// ============================================================================

void HazardSystem::updateHazards(double dt)
{
    for (auto& h : m_hazards) {
        switch (h.type) {

        // ---- Burning vehicles: animate fire flicker frame ------------------
        case HazardType::BURNING_TAXI:
        case HazardType::BURNING_AUTO: {
            h.animTimer += dt;
            // 3-frame fire animation looping every 0.3 s
            if (h.animTimer >= 0.3)
                h.animTimer -= 0.3;
            break;
        }

        // ---- Static lane blockers: nothing to tick -------------------------
        case HazardType::WRECKED_BUS:
        case HazardType::DERAILED_TRAM:
            break;

        // ---- Road fissure / sinkhole: static, collision only ---------------
        case HazardType::ROAD_FISSURE:
            break;

        // ---- Gas vent: timed eruption cycle --------------------------------
        // Dormant (0–3 s) → Warning hiss (3–4 s) → Erupting (4–5.5 s) → loop
        case HazardType::GAS_VENT: {
            h.animTimer += dt;
            if (h.animTimer >= GAS_ERUPT_END)
                h.animTimer -= GAS_ERUPT_END; // wrap cycle

            h.isErupting = (h.animTimer >= GAS_WARN_END
                            && h.animTimer < GAS_ERUPT_END);
            break;
        }

        // ---- Live wire: sparking downed cable (always dangerous) -----------
        case HazardType::LIVE_WIRE: {
            h.animTimer += dt;
            if (h.animTimer >= 0.25)
                h.animTimer -= 0.25; // fast spark flicker
            break;
        }

        // ---- Legacy compat hazards: no-op ----------------------------------
        default:
            break;
        }
    }
}

// ============================================================================
// 3. Seismic Tremor Controller
// ============================================================================

void HazardSystem::updateSeismic(double dt)
{
    // Decay existing shake toward zero
    const double decayRate = 4.0; // units/s — fast but visible
    m_seismic.shakeX *= std::max(0.0, 1.0 - decayRate * dt);
    m_seismic.shakeY *= std::max(0.0, 1.0 - decayRate * dt);
    m_seismic.pitchOffset *= std::max(0.0, 1.0 - decayRate * dt);
    m_seismic.intensity *= std::max(0.0, 1.0 - decayRate * dt);

    // Snap tiny residual values to zero
    if (std::abs(m_seismic.shakeX) < 0.01) m_seismic.shakeX = 0.0;
    if (std::abs(m_seismic.shakeY) < 0.01) m_seismic.shakeY = 0.0;
    if (std::abs(m_seismic.pitchOffset) < 0.001) m_seismic.pitchOffset = 0.0;

    // Count down to next scheduled quake
    if (m_seismic.cooldown > 0.0) {
        m_seismic.cooldown -= dt;
        return; // still in post-quake cooldown
    }

    m_seismic.timer -= dt;
    if (m_seismic.timer <= 0.0) {
        // Fire an aftershock
        double intensity = nextQuakeIntensity();
        triggerQuake(intensity);
        m_seismic.timer = nextQuakeInterval();
        m_seismic.cooldown = 2.0; // brief lockout after each quake
    }
}

void HazardSystem::triggerQuake(double intensity)
{
    intensity = std::clamp(intensity, 0.0, 1.0);

    // Random direction for the initial jolt
    QRandomGenerator* rng = QRandomGenerator::global();
    double angle = rng->bounded(2.0 * M_PI);

    m_seismic.shakeX = std::cos(angle) * intensity * 12.0;
    m_seismic.shakeY = std::sin(angle) * intensity * 8.0;
    m_seismic.pitchOffset = (rng->bounded(2.0) - 1.0) * intensity * 0.06;
    m_seismic.intensity = intensity;
}

// ============================================================================
// 4. Crater Stamping Interface
// ============================================================================

std::vector<CraterStamp>& HazardSystem::getCraterStamps()
{
    return m_craterStamps;
}

const std::vector<CraterStamp>& HazardSystem::getCraterStamps() const
{
    return m_craterStamps;
}

// ============================================================================
// 5. Player Collision Checking
// ============================================================================

CollisionResult HazardSystem::checkPlayerCollision(
    double playerX, double playerZ,
    double playerAngle, double playerSpeed,
    HullState& hull)
{
    CollisionResult result;

    // --- A) Collapsing structures (FALLING + WRECKED) ----------------------
    for (auto& s : m_structures) {
        double d = dist(playerX, playerZ, s.x, s.z);

        if (s.state == CollapseState::FALLING) {
            // Only collides when the structure is low enough to hit the car
            if (d < s.width && s.y < 8.0) {
                hull.takeDamage(s.hullDamage);
                result.hit = true;
                result.damageDealt += s.hullDamage;
                result.speedMultiplier = -0.2; // bounce back

                // Push player away from structure centre
                double dx = playerX - s.x;
                double dz = playerZ - s.z;
                double len = std::max(dist(0, 0, dx, dz), 0.1);
                result.pushX += (dx / len) * 20.0;
                result.pushZ += (dz / len) * 20.0;
            }
        }
        else if (s.state == CollapseState::WRECKED) {
            // Ground obstacle collision
            if (d < s.width * 0.5) {
                hull.takeDamage(s.hullDamage * 0.5); // less damage than active fall
                result.hit = true;
                result.damageDealt += s.hullDamage * 0.5;
                result.speedMultiplier = -0.2;

                double dx = playerX - s.x;
                double dz = playerZ - s.z;
                double len = std::max(dist(0, 0, dx, dz), 0.1);
                result.pushX += (dx / len) * 15.0;
                result.pushZ += (dz / len) * 15.0;
            }
        }
    }

    // If we already hit a structure, return early to avoid stacking
    if (result.hit) return result;

    // --- B) Track hazards --------------------------------------------------
    for (auto& h : m_hazards) {
        double d = dist(playerX, playerZ, h.x, h.z);
        if (d > h.radius) continue;

        switch (h.type) {

        // -- Burning vehicles: always active, bounce + damage ---------------
        case HazardType::BURNING_TAXI:
        case HazardType::BURNING_AUTO: {
            hull.takeDamage(h.hullDamage);
            result.hit = true;
            result.damageDealt += h.hullDamage;
            result.speedMultiplier = -0.2;

            double dx = playerX - h.x;
            double dz = playerZ - h.z;
            double len = std::max(dist(0, 0, dx, dz), 0.1);
            result.pushX += (dx / len) * 14.0;
            result.pushZ += (dz / len) * 14.0;
            return result;
        }

        // -- Static lane blockers: full collision bounce + damage -----------
        case HazardType::WRECKED_BUS:
        case HazardType::DERAILED_TRAM: {
            hull.takeDamage(h.hullDamage);
            result.hit = true;
            result.damageDealt += h.hullDamage;
            result.speedMultiplier = 0.0; // full stop

            double dx = playerX - h.x;
            double dz = playerZ - h.z;
            double len = std::max(dist(0, 0, dx, dz), 0.1);
            result.pushX += (dx / len) * 18.0;
            result.pushZ += (dz / len) * 18.0;
            return result;
        }

        // -- Road fissure: chassis drop — damage + checkpoint reset ---------
        case HazardType::ROAD_FISSURE: {
            // Only triggers when driving over the very centre
            if (d < FISSURE_CENTRE_RADIUS) {
                hull.takeDamage(h.hullDamage);
                result.hit = true;
                result.damageDealt += h.hullDamage;
                result.speedMultiplier = 0.0;
                result.resetToCheckpoint = true; // respawn + 1.5 s time loss
            }
            return result;
        }

        // -- Gas vent: only dangerous while erupting ------------------------
        case HazardType::GAS_VENT: {
            if (!h.isErupting) break; // safe during dormant/warning phases

            hull.takeDamage(h.hullDamage);
            result.hit = true;
            result.damageDealt += h.hullDamage;
            result.speedMultiplier = 0.3; // big speed loss

            // Blast car sideways (perpendicular to player heading)
            double perpX = std::cos(playerAngle + M_PI * 0.5);
            double perpZ = std::sin(playerAngle + M_PI * 0.5);
            double blastDir = (QRandomGenerator::global()->bounded(2) == 0) ? 1.0 : -1.0;
            result.pushX += perpX * blastDir * 25.0;
            result.pushZ += perpZ * blastDir * 25.0;
            return result;
        }

        // -- Live wire: spin-out + minor shock damage -----------------------
        case HazardType::LIVE_WIRE: {
            if (d < WIRE_SPARK_RADIUS) {
                hull.takeDamage(h.hullDamage * 0.5); // minor shock
                result.hit = true;
                result.damageDealt += h.hullDamage * 0.5;
                result.spinOut = true;
                result.spinDuration = 0.8;
                result.speedMultiplier = 0.4;
            }
            return result;
        }

        default:
            break;
        }
    }

    return result;
}

// ============================================================================
// 6. AI Hazard Awareness
// ============================================================================

double HazardSystem::getHazardAvoidanceOffset(
    double aiX, double aiZ,
    double lookAheadX, double lookAheadZ) const
{
    // Scan for the closest hazard along the AI's look-ahead corridor.
    // Returns a signed lateral offset in [-1.0, +1.0] indicating which
    // direction the AI should steer to dodge.

    double bestThreat = 1e9;   // closest distance² to look-ahead line
    double bestOffset = 0.0;

    auto evaluateThreat = [&](double hx, double hz, double threatRadius) {
        // Parametric projection of hazard onto look-ahead segment
        double t = pointSegmentT(hx, hz, aiX, aiZ, lookAheadX, lookAheadZ);
        if (t < 0.05 || t > 1.0) return; // behind or beyond look-ahead

        // Closest point on segment
        double cx = aiX + t * (lookAheadX - aiX);
        double cz = aiZ + t * (lookAheadZ - aiZ);

        double dSq = distSq(hx, hz, cx, cz);
        double avoidRadius = threatRadius + 30.0; // safety margin

        if (dSq < avoidRadius * avoidRadius && dSq < bestThreat) {
            bestThreat = dSq;

            // Determine which side to steer: perpendicular to look-ahead
            double segDx = lookAheadX - aiX;
            double segDz = lookAheadZ - aiZ;
            // Cross product sign tells us which side the hazard is on
            double cross = segDx * (hz - aiZ) - segDz * (hx - aiX);
            // Steer AWAY from the hazard (opposite sign)
            bestOffset = (cross > 0.0) ? -0.6 : 0.6;
        }
    };

    // Evaluate collapsing structures (INTACT with small trigger radius, or WRECKED)
    for (const auto& s : m_structures) {
        if (s.state == CollapseState::WRECKED) {
            evaluateThreat(s.x, s.z, s.width);
        }
        else if (s.state == CollapseState::WARNING || s.state == CollapseState::FALLING) {
            evaluateThreat(s.x, s.z, s.width * 1.5); // bigger avoidance radius while active
        }
    }

    // Evaluate track hazards
    for (const auto& h : m_hazards) {
        double effectiveRadius = h.radius;

        // Increase avoidance radius for erupting gas vents
        if (h.type == HazardType::GAS_VENT && h.isErupting) {
            effectiveRadius *= 2.0;
        }
        // Live wires are subtle — use full spark radius
        if (h.type == HazardType::LIVE_WIRE) {
            effectiveRadius = WIRE_SPARK_RADIUS;
        }

        evaluateThreat(h.x, h.z, effectiveRadius);
    }

    return std::clamp(bestOffset, -1.0, 1.0);
}

// ============================================================================
// Accessors
// ============================================================================

const SeismicState& HazardSystem::getSeismicState() const
{
    return m_seismic;
}

const std::vector<CollapsingStructure>& HazardSystem::getStructures() const
{
    return m_structures;
}

const std::vector<TrackHazard>& HazardSystem::getTrackHazards() const
{
    return m_hazards;
}

// ============================================================================
// Internal Helpers
// ============================================================================

double HazardSystem::distSq(double x1, double z1, double x2, double z2)
{
    double dx = x2 - x1;
    double dz = z2 - z1;
    return dx * dx + dz * dz;
}

double HazardSystem::dist(double x1, double z1, double x2, double z2)
{
    return std::sqrt(distSq(x1, z1, x2, z2));
}

double HazardSystem::pointSegmentT(double px, double pz,
                                    double ax, double az,
                                    double bx, double bz)
{
    double abx = bx - ax;
    double abz = bz - az;
    double apx = px - ax;
    double apz = pz - az;

    double dot = apx * abx + apz * abz;
    double lenSq = abx * abx + abz * abz;
    if (lenSq < 1e-9) return 0.0; // degenerate segment

    return std::clamp(dot / lenSq, 0.0, 1.0);
}

double HazardSystem::nextQuakeInterval() const
{
    // Random interval 8–20 s, compressed by difficulty
    double base = 8.0 + QRandomGenerator::global()->bounded(12.0);
    return base / m_difficultyScale; // harder stages → more frequent quakes
}

double HazardSystem::nextQuakeIntensity() const
{
    // Random intensity 0.3–0.8, amplified by difficulty
    double base = 0.3 + QRandomGenerator::global()->bounded(0.5);
    return std::min(base * m_difficultyScale, 1.0); // cap at 1.0
}
