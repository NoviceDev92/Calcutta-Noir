#ifndef KOLKATA_MAP_H
#define KOLKATA_MAP_H

#include "kolkata_types.h"
#include <QImage>
#include <QPointF>
#include <vector>

// ============================================================================
// KolkataMap — Procedural map generator for "Kolkata: Last Exit"
//
// Generates 4096×4096 non-wrapping apocalyptic escape routes for each
// KolkataStage. All painting is procedural (QPainter + QPainterPath).
// ============================================================================

class KolkataMap {
public:
    static constexpr int MAP_SIZE = 4096;

    // --- Mask palette (matches Mode-7 surface logic) ---
    static constexpr QRgb MASK_ROAD      = qRgb(255, 255, 255); // Full speed tarmac
    static constexpr QRgb MASK_OFFROAD   = qRgb(0,   255, 0);   // Rubble / shoulder (45% penalty)
    static constexpr QRgb MASK_WALL      = qRgb(255, 0,   0);   // Solid barrier (bounce)
    static constexpr QRgb MASK_TRAM      = qRgb(0,   0,   255); // Tram track (85% speed, sparks)
    static constexpr QRgb MASK_CHECKPOINT= qRgb(255, 255, 0);   // Checkpoint trigger line
    static constexpr QRgb MASK_LETHAL    = qRgb(255, 0,   255); // Abyss / river (instant reset)

    // Master entry point — dispatches to per-stage generators
    void generateStage(KolkataStage stage,
                       QImage& visualMap,
                       QImage& maskMap,
                       std::vector<WorldSprite>& sprites,
                       std::vector<CollapsingStructure>& collapsingStructures,
                       std::vector<TrackHazard>& hazards,
                       std::vector<QPointF>& waypoints,
                       std::vector<CheckpointDef>& checkpoints);

private:
    // Per-stage generators
    void generateGariahatCollapse(QImage& vis, QImage& mask,
                                  std::vector<WorldSprite>& sprites,
                                  std::vector<CollapsingStructure>& collapsingStructures,
                                  std::vector<TrackHazard>& hazards,
                                  std::vector<QPointF>& waypoints,
                                  std::vector<CheckpointDef>& checkpoints);

    void generateParkStreetRuins(QImage& vis, QImage& mask,
                                  std::vector<WorldSprite>& sprites,
                                  std::vector<CollapsingStructure>& collapsingStructures,
                                  std::vector<TrackHazard>& hazards,
                                  std::vector<QPointF>& waypoints,
                                  std::vector<CheckpointDef>& checkpoints);

    void generateHowrahBridge(QImage& vis, QImage& mask,
                               std::vector<WorldSprite>& sprites,
                               std::vector<CollapsingStructure>& collapsingStructures,
                               std::vector<TrackHazard>& hazards,
                               std::vector<QPointF>& waypoints,
                               std::vector<CheckpointDef>& checkpoints);

    // Utility: extract evenly-spaced waypoints along a QPainterPath centerline
    static std::vector<QPointF> extractWaypointsFromPath(const QPainterPath& path, int count);

    // Utility: draw a road (curb + tarmac + mask) along a path
    struct RoadStyle {
        QColor curbColor;
        QColor roadColor;
        int curbWidth;    // Total pen width for outer border
        int roadWidth;    // Total pen width for driveable surface
    };
    static void paintRoad(QPainter& visPainter, QPainter& maskPainter,
                          const QPainterPath& path, const RoadStyle& style);

    // Utility: paint tram rails along a sub-path on both visual and mask maps
    static void paintTramRails(QPainter& visPainter, QPainter& maskPainter,
                               const QPainterPath& railPath, int railGap = 24);

    // Utility: paint a checkpoint line across the road
    static void paintCheckpoint(QPainter& visPainter, QPainter& maskPainter,
                                double x1, double z1, double x2, double z2);

    // Utility: scatter scorch marks & crater visuals on the visual map
    static void paintScorchMarks(QPainter& visPainter, const std::vector<QPointF>& positions);
};

#endif // KOLKATA_MAP_H
