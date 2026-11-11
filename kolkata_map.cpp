#include "kolkata_map.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <cmath>

// ============================================================================
// KolkataMap — 4096×4096 Long-Route Apocalyptic Escape Map Generator
// Generates ~18,000–22,000 unit serpentine urban corridors per stage with
// 2 well-spaced checkpoints and zero road overlap.
// ============================================================================

void KolkataMap::generateStage(
    KolkataStage stage,
    QImage& visualMap, QImage& maskMap,
    std::vector<WorldSprite>& sprites,
    std::vector<CollapsingStructure>& collapsingStructures,
    std::vector<TrackHazard>& hazards,
    std::vector<QPointF>& waypoints,
    std::vector<CheckpointDef>& checkpoints)
{
    if (visualMap.size() != QSize(MAP_SIZE, MAP_SIZE) ||
        visualMap.format() != QImage::Format_RGB32) {
        visualMap = QImage(MAP_SIZE, MAP_SIZE, QImage::Format_RGB32);
    }
    if (maskMap.size() != QSize(MAP_SIZE, MAP_SIZE) ||
        maskMap.format() != QImage::Format_RGB32) {
        maskMap = QImage(MAP_SIZE, MAP_SIZE, QImage::Format_RGB32);
    }

    sprites.clear();
    collapsingStructures.clear();
    hazards.clear();
    waypoints.clear();
    checkpoints.clear();

    switch (stage) {
    case KolkataStage::GARIAHAT_COLLAPSE:
        generateGariahatCollapse(visualMap, maskMap, sprites, collapsingStructures,
                                 hazards, waypoints, checkpoints);
        break;
    case KolkataStage::PARK_STREET_RUINS:
        generateParkStreetRuins(visualMap, maskMap, sprites, collapsingStructures,
                                hazards, waypoints, checkpoints);
        break;
    case KolkataStage::HOWRAH_BRIDGE:
        generateHowrahBridge(visualMap, maskMap, sprites, collapsingStructures,
                             hazards, waypoints, checkpoints);
        break;
    }
}

std::vector<QPointF> KolkataMap::extractWaypointsFromPath(const QPainterPath& path, int count)
{
    std::vector<QPointF> wps;
    if (count < 2) count = 2;
    double totalLen = path.length();
    if (totalLen < 1.0) return wps;

    wps.reserve(count);
    for (int i = 0; i < count; ++i) {
        double t = path.percentAtLength((totalLen * i) / (count - 1));
        wps.push_back(path.pointAtPercent(t));
    }
    return wps;
}

void KolkataMap::paintRoad(QPainter& visPainter, QPainter& maskPainter,
                           const QPainterPath& path, const RoadStyle& style)
{
    visPainter.setPen(QPen(style.curbColor, style.curbWidth,
                           Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    visPainter.setBrush(Qt::NoBrush);
    visPainter.drawPath(path);

    maskPainter.setPen(QPen(QColor::fromRgba(MASK_WALL), style.curbWidth,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    maskPainter.setBrush(Qt::NoBrush);
    maskPainter.drawPath(path);

    visPainter.setPen(QPen(style.roadColor, style.roadWidth,
                           Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    visPainter.drawPath(path);

    maskPainter.setPen(QPen(QColor::fromRgba(MASK_ROAD), style.roadWidth,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    maskPainter.drawPath(path);

    // Subtle faded yellow center divider on tarmac
    visPainter.setPen(QPen(QColor(155, 125, 40, 120), 2, Qt::DashLine));
    visPainter.drawPath(path);
}

void KolkataMap::paintTramRails(QPainter& visPainter, QPainter& maskPainter,
                                const QPainterPath& railPath, int railGap)
{
    maskPainter.setPen(QPen(QColor::fromRgba(MASK_TRAM), railGap,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    maskPainter.setBrush(Qt::NoBrush);
    maskPainter.drawPath(railPath);

    visPainter.setPen(QPen(QColor(145, 150, 155), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    visPainter.setBrush(Qt::NoBrush);
    visPainter.drawPath(railPath);

    visPainter.setPen(QPen(QColor(95, 90, 82), 2, Qt::DashLine, Qt::FlatCap));
    visPainter.drawPath(railPath);
}

void KolkataMap::paintCheckpoint(QPainter& visPainter, QPainter& maskPainter,
                                 double x1, double z1, double x2, double z2)
{
    visPainter.setPen(QPen(QColor(245, 130, 30), 8, Qt::DashLine));
    visPainter.drawLine(QPointF(x1, z1), QPointF(x2, z2));

    maskPainter.setPen(QPen(QColor::fromRgba(MASK_CHECKPOINT), 12));
    maskPainter.drawLine(QPointF(x1, z1), QPointF(x2, z2));
}

void KolkataMap::paintScorchMarks(QPainter& visPainter,
                                  const std::vector<QPointF>& positions)
{
    visPainter.setPen(Qt::NoPen);
    for (const auto& p : positions) {
        visPainter.setBrush(QColor(18, 15, 13, 175));
        visPainter.drawEllipse(p, 22, 16);
        visPainter.setBrush(QColor(8, 6, 5, 140));
        visPainter.drawEllipse(p, 11, 8);
    }
}

// ---------------------------------------------------------------------------
//  STAGE 1 : GARIAHAT COLLAPSE (Medium) — ~20,000 unit serpentine route
//  Gariahat Crossing -> Rashbehari -> Golpark (CP1) -> Ballygunge -> Park Circus (CP2) -> AJC Bose
// ---------------------------------------------------------------------------

void KolkataMap::generateGariahatCollapse(
    QImage& vis, QImage& mask,
    std::vector<WorldSprite>& sprites,
    std::vector<CollapsingStructure>& collapsingStructures,
    std::vector<TrackHazard>& hazards,
    std::vector<QPointF>& waypoints,
    std::vector<CheckpointDef>& checkpoints)
{
    vis.fill(qRgb(16, 14, 13));
    mask.fill(QColor::fromRgba(MASK_OFFROAD));

    QPainter vp(&vis);
    QPainter mp(&mask);
    vp.setRenderHint(QPainter::Antialiasing, true);
    mp.setRenderHint(QPainter::Antialiasing, false);

    // Ruined city block footprints between corridors
    vp.setPen(Qt::NoPen);
    vp.setBrush(QColor(26, 22, 20));
    for (int bz = 580; bz < 3650; bz += 480) {
        for (int bx = 550; bx < 3500; bx += 520) {
            vp.drawRect(bx, bz, 180, 120);
        }
    }

    QPainterPath road;
    // Sweep 1: Gariahat Crossing (SW) -> Rashbehari Ave (East)
    road.moveTo(350, 3800);
    road.cubicTo(1100, 3820, 2400, 3760, 3600, 3680);
    // Hairpin North onto Golpark Connector
    road.cubicTo(3860, 3640, 3880, 3260, 3550, 3220);
    // Sweep 2: West through Golpark / Southern Ave
    road.cubicTo(2500, 3180, 1400, 3260, 480, 3160);
    // Hairpin North at Lake Gardens edge
    road.cubicTo(220, 3120, 220, 2720, 520, 2680);
    // Sweep 3: East through Ballygunge Phari (CP1 at x=2100, z=2660)
    road.cubicTo(1400, 2640, 2700, 2680, 3580, 2580);
    // Hairpin North toward Bondel Road
    road.cubicTo(3850, 2540, 3860, 2140, 3520, 2100);
    // Sweep 4: West through Ballygunge Circular Rd
    road.cubicTo(2500, 2060, 1300, 2140, 460, 2040);
    // Hairpin North toward Beckbagan
    road.cubicTo(210, 2000, 220, 1600, 540, 1560);
    // Sweep 5: East toward Park Circus 7-Point (CP2 at x=2300, z=1520)
    road.cubicTo(1450, 1500, 2750, 1560, 3600, 1460);
    // Hairpin North onto AJC Bose Flyover Approach
    road.cubicTo(3860, 1420, 3840, 1020, 3500, 980);
    // Sweep 6: West along AJC Bose Flyover
    road.cubicTo(2400, 940, 1200, 1000, 480, 900);
    // Final S-curve North to Extraction Ramp
    road.cubicTo(240, 860, 240, 460, 560, 420);
    road.cubicTo(1400, 360, 2500, 420, 3550, 360);

    RoadStyle gariahatRoad;
    gariahatRoad.curbColor = QColor(98, 42, 32);
    gariahatRoad.roadColor = QColor(48, 46, 44);
    gariahatRoad.curbWidth = 104;
    gariahatRoad.roadWidth = 84;
    paintRoad(vp, mp, road, gariahatRoad);

    // Tram tracks along Sweep 1 & Sweep 3
    QPainterPath tram1;
    tram1.moveTo(650, 3780);
    tram1.cubicTo(1300, 3790, 2400, 3750, 3450, 3685);
    paintTramRails(vp, mp, tram1, 20);

    // Checkpoints (well-spaced at ~35% and ~70%)
    double cp1x = 2050.0, cp1z = 2660.0;
    paintCheckpoint(vp, mp, cp1x, cp1z - 45, cp1x, cp1z + 45);
    checkpoints.push_back({cp1x, cp1z - 45, cp1x, cp1z + 45, 1, 35.0});

    double cp2x = 2300.0, cp2z = 1525.0;
    paintCheckpoint(vp, mp, cp2x, cp2z - 45, cp2x, cp2z + 45);
    checkpoints.push_back({cp2x, cp2z - 45, cp2x, cp2z + 45, 2, 35.0});

    paintScorchMarks(vp, {
        {900, 3780}, {2400, 3740}, {3650, 3400}, {1800, 3210},
        {420, 2900}, {1600, 2660}, {3600, 2320}, {1800, 2090},
        {420, 1780}, {2100, 1530}, {3620, 1200}, {1600, 960},
        {1400, 390}, {2900, 380}
    });

    // Pickups & Salvage Scrap along the 20k route
    sprites.push_back({1100.0, 3785.0, 0.0, SpriteType::SCRAP_CACHE, true, 0.0});
    sprites.push_back({2400.0, 3735.0, 0.0, SpriteType::NITROUS_CELL, true, 0.0});
    sprites.push_back({2800.0, 3210.0, 0.0, SpriteType::REPAIR_KIT, true, 0.0});
    sprites.push_back({1200.0, 3190.0, 0.0, SpriteType::SUPPLY_CRATE, true, 0.0});
    sprites.push_back({1400.0, 2655.0, 0.0, SpriteType::SCRAP_CACHE, true, 0.0});
    sprites.push_back({3000.0, 2610.0, 0.0, SpriteType::REPAIR_KIT, true, 0.0});
    sprites.push_back({2000.0, 2095.0, 0.0, SpriteType::NITROUS_CELL, true, 0.0});
    sprites.push_back({900.0, 2070.0, 0.0, SpriteType::SCRAP_CACHE, true, 0.0});
    sprites.push_back({1600.0, 1530.0, 0.0, SpriteType::SUPPLY_CRATE, true, 0.0});
    sprites.push_back({3100.0, 1490.0, 0.0, SpriteType::REPAIR_KIT, true, 0.0});
    sprites.push_back({2100.0, 965.0, 0.0, SpriteType::NITROUS_CELL, true, 0.0});
    sprites.push_back({1800.0, 395.0, 0.0, SpriteType::SCRAP_CACHE, true, 0.0});

    // Monuments & Drive-Under Overhead Spans
    sprites.push_back({750.0, 3700.0, 0.0, SpriteType::GARIAHAT_FLYOVER_PIER, true, 0.0});
    sprites.push_back({1800.0, 3760.0, 0.0, SpriteType::FLYOVER_BEAM_OVERHEAD, true, 0.0});
    sprites.push_back({2200.0, 3200.0, 0.0, SpriteType::FLYOVER_BEAM_OVERHEAD, true, 0.0});
    sprites.push_back({1700.0, 2090.0, 0.0, SpriteType::GARIAHAT_FLYOVER_PIER, true, 0.0});
    sprites.push_back({2800.0, 960.0, 0.0, SpriteType::FLYOVER_BEAM_OVERHEAD, true, 0.0});

    // Static obstacles
    sprites.push_back({1550.0, 3765.0, 0.0, SpriteType::BURNING_TAXI, true, 0.0});
    sprites.push_back({3200.0, 3210.0, 0.0, SpriteType::WRECKED_BUS, true, 0.0});
    sprites.push_back({950.0, 2665.0, 0.0, SpriteType::DERAILED_TRAM, true, 0.0});
    sprites.push_back({2650.0, 2085.0, 0.0, SpriteType::BURNING_AUTO, true, 0.0});
    sprites.push_back({1200.0, 955.0, 0.0, SpriteType::STEEL_GIRDER, true, 0.0});

    // Dynamic hazards
    hazards.push_back({1950.0, 3760.0, HazardType::BURNING_TAXI, 0.0, false, 18.0, 14.0, false});
    hazards.push_back({2500.0, 3205.0, HazardType::GAS_VENT, 0.0, false, 20.0, 12.0, false});
    hazards.push_back({750.0, 3175.0, HazardType::BURNING_AUTO, 0.4, false, 16.0, 12.0, false});
    hazards.push_back({2700.0, 2625.0, HazardType::ROAD_FISSURE, 0.0, false, 20.0, 18.0, true});
    hazards.push_back({1550.0, 2095.0, HazardType::BURNING_TAXI, 0.8, false, 18.0, 14.0, false});
    hazards.push_back({2950.0, 1510.0, HazardType::GAS_VENT, 1.2, false, 20.0, 12.0, false});
    hazards.push_back({1900.0, 965.0, HazardType::LIVE_WIRE, 0.0, false, 14.0, 10.0, false});
    hazards.push_back({2600.0, 385.0, HazardType::BURNING_TAXI, 0.2, false, 18.0, 14.0, false});

    // Collapsing flyover girders
    collapsingStructures.push_back({2200.0, 3755.0, 42.0, 42.0, 32.0, 0, CollapseState::INTACT, 165.0, 0.0, 0.0, 0.85, 28.0, false});
    collapsingStructures.push_back({1650.0, 3210.0, 42.0, 42.0, 30.0, 0, CollapseState::INTACT, 160.0, 0.0, 0.0, 0.85, 28.0, false});
    collapsingStructures.push_back({3100.0, 2600.0, 42.0, 42.0, 32.0, 0, CollapseState::INTACT, 155.0, 0.0, 0.0, 0.80, 30.0, false});
    collapsingStructures.push_back({1150.0, 1545.0, 42.0, 42.0, 30.0, 0, CollapseState::INTACT, 155.0, 0.0, 0.0, 0.80, 28.0, false});
    collapsingStructures.push_back({2300.0, 960.0, 42.0, 42.0, 32.0, 0, CollapseState::INTACT, 150.0, 0.0, 0.0, 0.78, 30.0, false});
    collapsingStructures.push_back({2950.0, 380.0, 42.0, 42.0, 30.0, 0, CollapseState::INTACT, 150.0, 0.0, 0.0, 0.78, 28.0, false});

    waypoints = extractWaypointsFromPath(road, 90);
    vp.end();
    mp.end();
}

// ---------------------------------------------------------------------------
//  STAGE 2 : PARK STREET & MAIDAN (Hard) — ~21,000 unit serpentine route
//  Park Street -> Chowringhee / The 42 -> Esplanade (CP1) -> Maidan / Victoria Memorial (CP2) -> Prinsep Ghat
// ---------------------------------------------------------------------------

void KolkataMap::generateParkStreetRuins(
    QImage& vis, QImage& mask,
    std::vector<WorldSprite>& sprites,
    std::vector<CollapsingStructure>& collapsingStructures,
    std::vector<TrackHazard>& hazards,
    std::vector<QPointF>& waypoints,
    std::vector<CheckpointDef>& checkpoints)
{
    vis.fill(qRgb(14, 12, 11));
    mask.fill(QColor::fromRgba(MASK_OFFROAD));

    QPainter vp(&vis);
    QPainter mp(&mask);
    vp.setRenderHint(QPainter::Antialiasing, true);
    mp.setRenderHint(QPainter::Antialiasing, false);

    // Flooded Maidan mud flats in central-north area
    vp.setPen(Qt::NoPen);
    vp.setBrush(QColor(24, 26, 20));
    vp.drawEllipse(QPointF(2100, 1550), 1150, 520);

    QPainterPath road;
    // Sweep 1: Park Street Eastbound
    road.moveTo(380, 3780);
    road.cubicTo(1200, 3760, 2500, 3720, 3620, 3660);
    // Hairpin onto Camac / Chowringhee
    road.cubicTo(3880, 3620, 3880, 3200, 3550, 3160);
    // Sweep 2: Chowringhee Westbound past St. Paul's & The 42
    road.cubicTo(2400, 3120, 1300, 3200, 460, 3100);
    road.cubicTo(210, 3060, 220, 2660, 520, 2620);
    // Sweep 3: Esplanade Metro Ruin Eastbound (CP1 at x=2150, z=2600)
    road.cubicTo(1400, 2580, 2700, 2640, 3580, 2540);
    road.cubicTo(3860, 2500, 3840, 2080, 3500, 2040);
    // Sweep 4: Maidan Mud & Victoria Memorial Perimeter Westbound
    road.cubicTo(2450, 2000, 1350, 2060, 480, 1960);
    road.cubicTo(220, 1920, 230, 1500, 540, 1460);
    // Sweep 5: Shaheed Minar Eastbound (CP2 at x=2300, z=1420)
    road.cubicTo(1450, 1400, 2750, 1480, 3600, 1380);
    road.cubicTo(3860, 1340, 3840, 920, 3500, 880);
    // Sweep 6: Fort William Approach Westbound
    road.cubicTo(2450, 840, 1250, 900, 480, 800);
    // Final sweep to Prinsep Ghat Ramp
    road.cubicTo(220, 760, 230, 380, 560, 360);
    road.cubicTo(1500, 320, 2550, 380, 3600, 320);

    RoadStyle parkRoad;
    parkRoad.curbColor = QColor(90, 36, 26);
    parkRoad.roadColor = QColor(42, 40, 38);
    parkRoad.curbWidth = 88;
    parkRoad.roadWidth = 68; // Narrower colonial lanes
    paintRoad(vp, mp, road, parkRoad);

    // Tram tracks along Chowringhee
    QPainterPath tram2;
    tram2.moveTo(3450, 3160);
    tram2.cubicTo(2400, 3120, 1300, 3200, 520, 3100);
    paintTramRails(vp, mp, tram2, 18);

    // Metro sinkhole pits
    mp.setPen(Qt::NoPen);
    mp.setBrush(QColor::fromRgba(MASK_LETHAL));
    vp.setBrush(QColor(10, 8, 8));
    vp.drawEllipse(QPointF(1450, 2595), 22, 16);
    mp.drawEllipse(QPointF(1450, 2595), 22, 16);
    vp.drawEllipse(QPointF(2850, 1435), 20, 15);
    mp.drawEllipse(QPointF(2850, 1435), 20, 15);

    double cp1x = 2150.0, cp1z = 2600.0;
    paintCheckpoint(vp, mp, cp1x, cp1z - 40, cp1x, cp1z + 40);
    checkpoints.push_back({cp1x, cp1z - 40, cp1x, cp1z + 40, 1, 35.0});

    double cp2x = 2300.0, cp2z = 1435.0;
    paintCheckpoint(vp, mp, cp2x, cp2z - 40, cp2x, cp2z + 40);
    checkpoints.push_back({cp2x, cp2z - 40, cp2x, cp2z + 40, 2, 35.0});

    paintScorchMarks(vp, {
        {1000, 3760}, {2600, 3700}, {1900, 3150}, {1200, 2590},
        {3000, 2560}, {1750, 2020}, {1500, 1420}, {2400, 865}, {2100, 350}
    });

    // Pickups
    sprites.push_back({1050.0, 3760.0, 0.0, SpriteType::SCRAP_CACHE, true, 0.0});
    sprites.push_back({2500.0, 3710.0, 0.0, SpriteType::NITROUS_CELL, true, 0.0});
    sprites.push_back({2400.0, 3140.0, 0.0, SpriteType::REPAIR_KIT, true, 0.0});
    sprites.push_back({1000.0, 3140.0, 0.0, SpriteType::SUPPLY_CRATE, true, 0.0});
    sprites.push_back({1700.0, 2595.0, 0.0, SpriteType::REPAIR_KIT, true, 0.0});
    sprites.push_back({3150.0, 2565.0, 0.0, SpriteType::SCRAP_CACHE, true, 0.0});
    sprites.push_back({1850.0, 2020.0, 0.0, SpriteType::NITROUS_CELL, true, 0.0});
    sprites.push_back({1150.0, 1425.0, 0.0, SpriteType::REPAIR_KIT, true, 0.0});
    sprites.push_back({2650.0, 860.0, 0.0, SpriteType::SCRAP_CACHE, true, 0.0});
    sprites.push_back({1550.0, 350.0, 0.0, SpriteType::NITROUS_CELL, true, 0.0});

    // Iconic Monuments & Overhead Scaffolding
    sprites.push_back({2750.0, 3040.0, 0.0, SpriteType::ST_PAULS_CATHEDRAL, true, 0.0});
    sprites.push_back({1100.0, 3040.0, 0.0, SpriteType::THE_42_TOWER, true, 0.0});
    sprites.push_back({1550.0, 2490.0, 0.0, SpriteType::ESPLANADE_METRO, true, 0.0});
    sprites.push_back({2000.0, 1900.0, 0.0, SpriteType::VICTORIA_MEMORIAL, true, 0.0});
    sprites.push_back({1850.0, 1320.0, 0.0, SpriteType::SHAHEED_MINAR, true, 0.0});
    sprites.push_back({1650.0, 3745.0, 0.0, SpriteType::SCAFFOLD_OVERHEAD, true, 0.0});
    sprites.push_back({2500.0, 2010.0, 0.0, SpriteType::SCAFFOLD_OVERHEAD, true, 0.0});
    sprites.push_back({1400.0, 865.0, 0.0, SpriteType::FLYOVER_BEAM_OVERHEAD, true, 0.0});

    // Hazards & Wrecks
    hazards.push_back({1400.0, 3750.0, HazardType::BURNING_TAXI, 0.0, false, 18.0, 16.0, false});
    hazards.push_back({2950.0, 3690.0, HazardType::WRECKED_BUS, 0.0, false, 22.0, 18.0, true});
    hazards.push_back({1950.0, 3150.0, HazardType::DERAILED_TRAM, 0.0, false, 24.0, 20.0, true});
    hazards.push_back({950.0, 2600.0, HazardType::GAS_VENT, 0.3, false, 20.0, 14.0, false});
    hazards.push_back({2850.0, 2580.0, HazardType::BURNING_AUTO, 0.7, false, 16.0, 14.0, false});
    hazards.push_back({1500.0, 2020.0, HazardType::ROAD_FISSURE, 0.0, false, 20.0, 18.0, true});
    hazards.push_back({1600.0, 1420.0, HazardType::BURNING_TAXI, 1.1, false, 18.0, 16.0, false});
    hazards.push_back({2100.0, 870.0, HazardType::LIVE_WIRE, 0.0, false, 14.0, 12.0, false});
    hazards.push_back({2800.0, 345.0, HazardType::GAS_VENT, 0.9, false, 20.0, 15.0, false});

    collapsingStructures.push_back({2150.0, 3730.0, 42.0, 42.0, 32.0, 2, CollapseState::INTACT, 160.0, 0.0, 0.0, 0.80, 30.0, false});
    collapsingStructures.push_back({1450.0, 3160.0, 42.0, 42.0, 30.0, 2, CollapseState::INTACT, 155.0, 0.0, 0.0, 0.80, 30.0, false});
    collapsingStructures.push_back({2600.0, 2590.0, 42.0, 42.0, 32.0, 0, CollapseState::INTACT, 150.0, 0.0, 0.0, 0.75, 32.0, false});
    collapsingStructures.push_back({2200.0, 2010.0, 42.0, 42.0, 30.0, 2, CollapseState::INTACT, 150.0, 0.0, 0.0, 0.75, 30.0, false});
    collapsingStructures.push_back({1950.0, 1425.0, 42.0, 42.0, 32.0, 0, CollapseState::INTACT, 145.0, 0.0, 0.0, 0.75, 32.0, false});
    collapsingStructures.push_back({1650.0, 865.0, 42.0, 42.0, 32.0, 2, CollapseState::INTACT, 145.0, 0.0, 0.0, 0.72, 32.0, false});
    collapsingStructures.push_back({3050.0, 340.0, 42.0, 42.0, 32.0, 0, CollapseState::INTACT, 145.0, 0.0, 0.0, 0.72, 34.0, false});

    waypoints = extractWaypointsFromPath(road, 95);
    vp.end();
    mp.end();
}

// ---------------------------------------------------------------------------
//  STAGE 3 : HOWRAH BRIDGE APOCALYPSE (Insane) — ~19,500 unit route
//  BBD Bagh / Writers' -> Strand Rd Docks (CP1) -> Long Howrah Cantilever Span (CP2) -> West Bank Bunker
// ---------------------------------------------------------------------------

void KolkataMap::generateHowrahBridge(
    QImage& vis, QImage& mask,
    std::vector<WorldSprite>& sprites,
    std::vector<CollapsingStructure>& collapsingStructures,
    std::vector<TrackHazard>& hazards,
    std::vector<QPointF>& waypoints,
    std::vector<CheckpointDef>& checkpoints)
{
    vis.fill(qRgb(12, 10, 12));
    mask.fill(QColor::fromRgba(MASK_OFFROAD));

    QPainter vp(&vis);
    QPainter mp(&mask);
    vp.setRenderHint(QPainter::Antialiasing, true);
    mp.setRenderHint(QPainter::Antialiasing, false);

    // Paint the Hooghly River across the northern/western bridge basin (z: 950..1850)
    // Special RGB signature (12, 26, 36) detected by renderGroundMode7 for animated rippling water!
    vp.setPen(Qt::NoPen);
    vp.setBrush(QColor(12, 26, 36));
    vp.drawRect(0, 820, MAP_SIZE, 1080);

    mp.setPen(Qt::NoPen);
    mp.setBrush(QColor::fromRgba(MASK_LETHAL));
    mp.drawRect(0, 820, MAP_SIZE, 1080);

    QPainterPath road;
    // Sweep 1: BBD Bagh / Writers' Building Eastbound
    road.moveTo(400, 3780);
    road.cubicTo(1300, 3760, 2500, 3720, 3600, 3660);
    road.cubicTo(3860, 3620, 3860, 3220, 3520, 3180);
    // Sweep 2: Dalhousie / GPO Westbound
    road.cubicTo(2400, 3140, 1300, 3200, 480, 3100);
    road.cubicTo(220, 3060, 220, 2660, 540, 2620);
    // Sweep 3: Strand Road Docks Eastbound along southern bank (CP1 at x=2200, z=2590)
    road.cubicTo(1450, 2580, 2700, 2620, 3560, 2520);
    road.cubicTo(3840, 2480, 3840, 2060, 3500, 2020);
    // Strand Road North-Westbound toward Howrah Bridge Approach
    road.cubicTo(2400, 1980, 1300, 2040, 620, 1940);
    // --- HOWRAH BRIDGE CANTILEVER SPAN CROSSING OVER THE HOOGHLY ---
    // Snakes diagonally & horizontally over lethal river water (z = 1850 .. 840)
    road.cubicTo(420, 1880, 420, 1620, 820, 1580);
    road.cubicTo(1600, 1540, 2500, 1600, 3320, 1500);
    road.cubicTo(3680, 1460, 3680, 1100, 3260, 1060);
    // Bridge Mid-Span Return West (CP2 at x=1900, z=1040)
    road.cubicTo(2300, 1020, 1300, 1080, 560, 980);
    // West Bank Landing & Bunker Approach
    road.cubicTo(300, 940, 260, 520, 620, 460);
    road.cubicTo(1550, 400, 2650, 440, 3580, 380);

    RoadStyle cityRoad;
    cityRoad.curbColor = QColor(88, 34, 24);
    cityRoad.roadColor = QColor(42, 40, 38);
    cityRoad.curbWidth = 82;
    cityRoad.roadWidth = 64;
    paintRoad(vp, mp, road, cityRoad);

    // Overpaint the Howrah Bridge span over river with narrow steel deck (no forgiving curb!)
    QPainterPath bridgeSpan;
    bridgeSpan.moveTo(620, 1940);
    bridgeSpan.cubicTo(420, 1880, 420, 1620, 820, 1580);
    bridgeSpan.cubicTo(1600, 1540, 2500, 1600, 3320, 1500);
    bridgeSpan.cubicTo(3680, 1460, 3680, 1100, 3260, 1060);
    bridgeSpan.cubicTo(2300, 1020, 1300, 1080, 560, 980);

    // Surround bridge deck with river water on mask
    mp.setPen(QPen(QColor::fromRgba(MASK_LETHAL), 120, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    mp.drawPath(bridgeSpan);
    vp.setPen(QPen(QColor(12, 26, 36), 120, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    vp.drawPath(bridgeSpan);

    RoadStyle bridgeStyle;
    bridgeStyle.curbColor = QColor(68, 70, 76);
    bridgeStyle.roadColor = QColor(52, 54, 60);
    bridgeStyle.curbWidth = 62;
    bridgeStyle.roadWidth = 50; // Tight steel deck!
    paintRoad(vp, mp, bridgeSpan, bridgeStyle);

    // Missing bridge deck gaps over Hooghly River
    mp.setPen(Qt::NoPen);
    mp.setBrush(QColor::fromRgba(MASK_LETHAL));
    vp.setPen(Qt::NoPen);
    vp.setBrush(QColor(12, 26, 36));
    const QPointF gaps[] = { {1450, 1565}, {2750, 1535}, {2550, 1035}, {1100, 1025} };
    for (const auto &g : gaps) {
        vp.drawEllipse(g, 13, 9);
        mp.drawEllipse(g, 13, 9);
    }

    double cp1x = 2200.0, cp1z = 2595.0;
    paintCheckpoint(vp, mp, cp1x, cp1z - 38, cp1x, cp1z + 38);
    checkpoints.push_back({cp1x, cp1z - 38, cp1x, cp1z + 38, 1, 40.0});

    double cp2x = 1900.0, cp2z = 1045.0;
    paintCheckpoint(vp, mp, cp2x, cp2z - 34, cp2x, cp2z + 34);
    checkpoints.push_back({cp2x, cp2z - 34, cp2x, cp2z + 34, 2, 40.0});

    paintScorchMarks(vp, {
        {1100, 3760}, {2700, 3700}, {1800, 3150}, {1400, 2590},
        {2100, 2000}, {1800, 1565}, {2200, 1040}, {2100, 415}
    });

    // Pickups
    sprites.push_back({1150.0, 3760.0, 0.0, SpriteType::SCRAP_CACHE, true, 0.0});
    sprites.push_back({2450.0, 3720.0, 0.0, SpriteType::NITROUS_CELL, true, 0.0});
    sprites.push_back({1900.0, 3150.0, 0.0, SpriteType::REPAIR_KIT, true, 0.0});
    sprites.push_back({1250.0, 2595.0, 0.0, SpriteType::SUPPLY_CRATE, true, 0.0});
    sprites.push_back({2800.0, 2000.0, 0.0, SpriteType::REPAIR_KIT, true, 0.0});
    sprites.push_back({1950.0, 1565.0, 0.0, SpriteType::NITROUS_CELL, true, 0.0});
    sprites.push_back({2850.0, 1045.0, 0.0, SpriteType::REPAIR_KIT, true, 0.0});
    sprites.push_back({1550.0, 420.0, 0.0, SpriteType::SCRAP_CACHE, true, 0.0});

    // Monuments & Cantilever Trusses
    sprites.push_back({1350.0, 3660.0, 0.0, SpriteType::WRITERS_BUILDING, true, 0.0});
    sprites.push_back({2600.0, 3040.0, 0.0, SpriteType::ESPLANADE_METRO, true, 0.0});
    sprites.push_back({750.0, 1850.0, 0.0, SpriteType::HOWRAH_BRIDGE_TOWER, true, 0.0});
    sprites.push_back({3450.0, 1280.0, 0.0, SpriteType::HOWRAH_BRIDGE_TOWER, true, 0.0});
    sprites.push_back({1150.0, 1565.0, 0.0, SpriteType::BRIDGE_TRUSS_OVERHEAD, true, 0.0});
    sprites.push_back({2350.0, 1560.0, 0.0, SpriteType::BRIDGE_TRUSS_OVERHEAD, true, 0.0});
    sprites.push_back({2350.0, 1045.0, 0.0, SpriteType::BRIDGE_TRUSS_OVERHEAD, true, 0.0});
    sprites.push_back({1350.0, 1035.0, 0.0, SpriteType::BRIDGE_TRUSS_OVERHEAD, true, 0.0});

    // Hazards
    hazards.push_back({1700.0, 3740.0, HazardType::BURNING_TAXI, 0.0, false, 18.0, 18.0, false});
    hazards.push_back({2950.0, 3690.0, HazardType::WRECKED_BUS, 0.0, false, 22.0, 20.0, true});
    hazards.push_back({1450.0, 3170.0, HazardType::DERAILED_TRAM, 0.0, false, 24.0, 22.0, true});
    hazards.push_back({1650.0, 2595.0, HazardType::ROAD_FISSURE, 0.0, false, 20.0, 20.0, true});
    hazards.push_back({2200.0, 2005.0, HazardType::GAS_VENT, 0.3, false, 20.0, 16.0, false});
    hazards.push_back({1750.0, 1565.0, HazardType::BURNING_TAXI, 0.8, false, 16.0, 18.0, false});
    hazards.push_back({2950.0, 1530.0, HazardType::LIVE_WIRE, 0.0, false, 13.0, 14.0, false});
    hazards.push_back({2150.0, 1040.0, HazardType::GAS_VENT, 1.0, false, 18.0, 16.0, false});
    hazards.push_back({950.0, 1015.0, HazardType::BURNING_AUTO, 0.4, false, 15.0, 15.0, false});
    hazards.push_back({2350.0, 410.0, HazardType::BURNING_TAXI, 0.1, false, 18.0, 18.0, false});

    collapsingStructures.push_back({2150.0, 3735.0, 44.0, 44.0, 32.0, 0, CollapseState::INTACT, 155.0, 0.0, 0.0, 0.75, 32.0, false});
    collapsingStructures.push_back({2250.0, 3155.0, 44.0, 44.0, 32.0, 2, CollapseState::INTACT, 150.0, 0.0, 0.0, 0.75, 32.0, false});
    collapsingStructures.push_back({2900.0, 2565.0, 44.0, 44.0, 32.0, 0, CollapseState::INTACT, 150.0, 0.0, 0.0, 0.75, 32.0, false});
    collapsingStructures.push_back({1350.0, 1565.0, 45.0, 45.0, 34.0, 1, CollapseState::INTACT, 145.0, 0.0, 0.0, 0.68, 36.0, false});
    collapsingStructures.push_back({2650.0, 1550.0, 45.0, 45.0, 34.0, 1, CollapseState::INTACT, 145.0, 0.0, 0.0, 0.68, 36.0, false});
    collapsingStructures.push_back({2450.0, 1040.0, 45.0, 45.0, 34.0, 1, CollapseState::INTACT, 140.0, 0.0, 0.0, 0.65, 38.0, false});
    collapsingStructures.push_back({1450.0, 1030.0, 45.0, 45.0, 34.0, 1, CollapseState::INTACT, 140.0, 0.0, 0.0, 0.65, 38.0, false});
    collapsingStructures.push_back({2750.0, 405.0, 44.0, 44.0, 32.0, 0, CollapseState::INTACT, 145.0, 0.0, 0.0, 0.72, 34.0, false});

    waypoints = extractWaypointsFromPath(road, 100);
    vp.end();
    mp.end();
}
