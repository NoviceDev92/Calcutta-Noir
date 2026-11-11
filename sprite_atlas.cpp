#include "sprite_atlas.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// KOLKATA: LAST EXIT — Procedural Pixel-Art Sprite Atlas
// Dark, fire-lit, ash-scarred Kolkata landmarks, vehicles, wrecks, and gear.
// ============================================================================

SpriteAtlas& SpriteAtlas::instance()
{
    static SpriteAtlas atlas;
    return atlas;
}

SpriteAtlas::SpriteAtlas()
{
    generateAllSprites();
}

void SpriteAtlas::generateAllSprites()
{
    // Pickups (32x32)
    generateSupplyCrate();
    generateRepairKit();
    generateNitrousCell();
    generateScrapCache();

    // Obstacles (64x48)
    generateBurningTaxi();
    generateWreckedBus();
    generateDerailedTram();
    generateBurningAuto();
    generateSteelGirder();
    generateConcreteRubble();
    generateRoadBarrier();

    // Monuments (128x96)
    generateVictoriaMemorial();
    generateShaheedMinar();
    generateWritersBuilding();
    generateHowrahBridgeTower();
    generateGariahatFlyoverPier();
    generateEsplanadeMetro();
    generateStPaulsCathedral();
    generateThe42Tower();

    // Overhead spans (160x64)
    generateFlyoverBeamOverhead();
    generateBridgeTrussOverhead();
    generateScaffoldOverhead();

    // Falling variants (48x64)
    generateGirderFalling();
    generateScaffoldFalling();
    generateBridgeTrussFalling();

    // Survivor rigs (48x48)
    generateRigKaaliPeeli();
    generateRigDozer();
    generateRigRunner();
    generateRigMedvan();
    generateRigAlts();

    // Legacy compat stubs
    mapLegacySprites();
}

const QImage& SpriteAtlas::getSprite(SpriteType type) const
{
    static QImage fallback(32, 32, QImage::Format_ARGB32);
    auto it = sprites.find(type);
    if (it != sprites.end()) return it->second;
    return fallback;
}

const QImage& SpriteAtlas::getAnimFrame(SpriteType type, int frame) const
{
    auto ait = animatedSprites.find(type);
    if (ait != animatedSprites.end() && !ait->second.empty()) {
        int count = static_cast<int>(ait->second.size());
        int idx = ((frame % count) + count) % count;
        return ait->second[idx];
    }
    return getSprite(type);
}

const QImage& SpriteAtlas::getVehicleFrame(SpriteType type, int leanDir) const
{
    auto vit = tiltedVehicles.find(type);
    if (vit != tiltedVehicles.end() && vit->second.size() == 3) {
        int idx = (leanDir < 0) ? 0 : (leanDir > 0 ? 2 : 1);
        return vit->second[idx];
    }
    return getSprite(type);
}

int SpriteAtlas::getFrameCount(SpriteType type) const
{
    auto ait = animatedSprites.find(type);
    if (ait != animatedSprites.end() && !ait->second.empty()) {
        return static_cast<int>(ait->second.size());
    }
    return 1;
}

bool SpriteAtlas::isPickup(SpriteType type) const
{
    return type == SpriteType::SUPPLY_CRATE ||
           type == SpriteType::REPAIR_KIT ||
           type == SpriteType::NITROUS_CELL ||
           type == SpriteType::SCRAP_CACHE ||
           type == SpriteType::QUESTION_BOX ||
           type == SpriteType::MUSHROOM ||
           type == SpriteType::ROCKET ||
           type == SpriteType::COIN;
}

bool SpriteAtlas::isObstacle(SpriteType type) const
{
    return type == SpriteType::BURNING_TAXI ||
           type == SpriteType::WRECKED_BUS ||
           type == SpriteType::DERAILED_TRAM ||
           type == SpriteType::BURNING_AUTO ||
           type == SpriteType::STEEL_GIRDER ||
           type == SpriteType::CONCRETE_RUBBLE ||
           type == SpriteType::ROAD_BARRIER ||
           type == SpriteType::GIRDER_FALLING ||
           type == SpriteType::SCAFFOLD_FALLING ||
           type == SpriteType::BRIDGE_TRUSS_FALLING ||
           type == SpriteType::GREEN_PIPE ||
           type == SpriteType::THWOMP ||
           type == SpriteType::OIL_SLICK ||
           type == SpriteType::BANANA_DROPPED ||
           type == SpriteType::LAVA_GEYSER_ERUPTING;
}

// ----------------------------------------------------------------------------
// Pixel Drawing Helpers
// ----------------------------------------------------------------------------

void SpriteAtlas::setPixel(QImage& img, int x, int y, QRgb color)
{
    if (x >= 0 && x < img.width() && y >= 0 && y < img.height()) {
        img.setPixel(x, y, color);
    }
}

void SpriteAtlas::fillCircle(QImage& img, int cx, int cy, int r, QRgb fill, QRgb outline)
{
    for (int y = cy - r; y <= cy + r; ++y) {
        for (int x = cx - r; x <= cx + r; ++x) {
            int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            if (d2 <= r * r) {
                if (d2 >= (r - 1) * (r - 1)) setPixel(img, x, y, outline);
                else setPixel(img, x, y, fill);
            }
        }
    }
}

void SpriteAtlas::fillRect(QImage& img, int x, int y, int w, int h, QRgb color)
{
    for (int py = y; py < y + h; ++py) {
        for (int px = x; px < x + w; ++px) {
            setPixel(img, px, py, color);
        }
    }
}

void SpriteAtlas::drawLine(QImage& img, int x1, int y1, int x2, int y2, QRgb color)
{
    int dx = std::abs(x2 - x1), sx = (x1 < x2) ? 1 : -1;
    int dy = -std::abs(y2 - y1), sy = (y1 < y2) ? 1 : -1;
    int err = dx + dy;
    while (true) {
        setPixel(img, x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void SpriteAtlas::fillTriangle(QImage& img, int x1, int y1, int x2, int y2, int x3, int y3, QRgb color)
{
    int minX = std::max(0, std::min({x1, x2, x3}));
    int maxX = std::min(img.width() - 1, std::max({x1, x2, x3}));
    int minY = std::max(0, std::min({y1, y2, y3}));
    int maxY = std::min(img.height() - 1, std::max({y1, y2, y3}));

    auto edge = [](int ax, int ay, int bx, int by, int cx, int cy) {
        return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
    };

    int area = edge(x1, y1, x2, y2, x3, y3);
    if (area == 0) return;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            int w0 = edge(x2, y2, x3, y3, x, y);
            int w1 = edge(x3, y3, x1, y1, x, y);
            int w2 = edge(x1, y1, x2, y2, x, y);
            if ((area > 0 && w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                (area < 0 && w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                setPixel(img, x, y, color);
            }
        }
    }
}

QRgb SpriteAtlas::lerpColor(QRgb a, QRgb b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    int r = static_cast<int>(qRed(a)   + t * (qRed(b)   - qRed(a)));
    int g = static_cast<int>(qGreen(a) + t * (qGreen(b) - qGreen(a)));
    int bl = static_cast<int>(qBlue(a) + t * (qBlue(b)  - qBlue(a)));
    int al = static_cast<int>(qAlpha(a)+ t * (qAlpha(b) - qAlpha(a)));
    return qRgba(r, g, bl, al);
}

// ----------------------------------------------------------------------------
// Survival Pickups (32x32)
// ----------------------------------------------------------------------------

void SpriteAtlas::generateSupplyCrate()
{
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb darkOlive = qRgba(42, 54, 36, 255);
    QRgb olive     = qRgba(68, 86, 54, 255);
    QRgb trim      = qRgba(125, 130, 120, 255);
    QRgb outline   = qRgba(15, 18, 12, 255);
    QRgb amber     = qRgba(245, 165, 30, 255);

    fillRect(img, 4, 4, 24, 24, olive);
    fillRect(img, 4, 20, 24, 8, darkOlive);
    // Steel corner braces & frame
    fillRect(img, 4, 4, 24, 2, outline);
    fillRect(img, 4, 26, 24, 2, outline);
    fillRect(img, 4, 4, 2, 24, outline);
    fillRect(img, 26, 4, 2, 24, outline);
    drawLine(img, 6, 6, 25, 25, darkOlive);
    drawLine(img, 25, 6, 6, 25, darkOlive);

    // Metallic rivets
    fillRect(img, 6, 6, 2, 2, trim);
    fillRect(img, 24, 6, 2, 2, trim);
    fillRect(img, 6, 24, 2, 2, trim);
    fillRect(img, 24, 24, 2, 2, trim);

    // Emergency supply star / cross in amber
    fillRect(img, 14, 9, 4, 14, amber);
    fillRect(img, 9, 14, 14, 4, amber);

    sprites[SpriteType::SUPPLY_CRATE] = img;
}

void SpriteAtlas::generateRepairKit()
{
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb caseWhite = qRgba(210, 215, 215, 255);
    QRgb caseShade = qRgba(150, 155, 160, 255);
    QRgb outline   = qRgba(25, 28, 32, 255);
    QRgb redCross  = qRgba(215, 35, 35, 255);
    QRgb handle    = qRgba(70, 75, 82, 255);

    // Handle on top
    fillRect(img, 11, 3, 10, 3, handle);
    fillRect(img, 13, 4, 6, 2, Qt::transparent);

    // Medical box
    fillRect(img, 4, 6, 24, 21, caseWhite);
    fillRect(img, 4, 21, 24, 6, caseShade);
    fillRect(img, 4, 6, 24, 1, outline);
    fillRect(img, 4, 26, 24, 1, outline);
    fillRect(img, 4, 6, 1, 21, outline);
    fillRect(img, 27, 6, 1, 21, outline);

    // Bright green/red emergency cross
    fillRect(img, 14, 10, 4, 13, redCross);
    fillRect(img, 10, 14, 12, 4, redCross);

    sprites[SpriteType::REPAIR_KIT] = img;
}

void SpriteAtlas::generateNitrousCell()
{
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb steelDark = qRgba(40, 50, 62, 255);
    QRgb cyanBody  = qRgba(20, 165, 225, 255);
    QRgb cyanGlow  = qRgba(110, 235, 255, 255);
    QRgb valve     = qRgba(210, 70, 30, 255);
    QRgb outline   = qRgba(15, 20, 28, 255);

    // Valve cap
    fillRect(img, 13, 3, 6, 4, valve);
    fillRect(img, 11, 6, 10, 2, steelDark);

    // Cylinder body
    fillRect(img, 9, 8, 14, 20, cyanBody);
    fillRect(img, 11, 9, 4, 18, cyanGlow);
    fillRect(img, 19, 8, 4, 20, steelDark);

    // Steel straps & outline
    fillRect(img, 9, 13, 14, 2, steelDark);
    fillRect(img, 9, 21, 14, 2, steelDark);
    fillRect(img, 8, 8, 1, 20, outline);
    fillRect(img, 23, 8, 1, 20, outline);
    fillRect(img, 9, 28, 14, 1, outline);

    sprites[SpriteType::NITROUS_CELL] = img;
}

void SpriteAtlas::generateScrapCache()
{
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb gold      = qRgba(240, 175, 35, 255);
    QRgb bronze    = qRgba(155, 95, 20, 255);
    QRgb shine     = qRgba(255, 235, 130, 255);
    QRgb outline   = qRgba(35, 22, 8, 255);

    // Cogwheel outer teeth
    fillRect(img, 14, 3, 4, 26, bronze);
    fillRect(img, 3, 14, 26, 4, bronze);
    drawLine(img, 6, 6, 25, 25, gold);
    drawLine(img, 25, 6, 6, 25, gold);

    fillCircle(img, 16, 16, 10, gold, outline);
    fillCircle(img, 14, 14, 5, shine, gold);
    fillCircle(img, 16, 16, 3, qRgba(20, 15, 10, 255), outline);

    sprites[SpriteType::SCRAP_CACHE] = img;
}

// ----------------------------------------------------------------------------
// Grounded Obstacles (64x48)
// ----------------------------------------------------------------------------

void SpriteAtlas::generateBurningTaxi()
{
    std::vector<QImage> frames;
    QRgb yellow    = qRgba(235, 180, 18, 255);
    QRgb darkYel   = qRgba(165, 115, 12, 255);
    QRgb charred   = qRgba(28, 24, 22, 255);
    QRgb soot      = qRgba(45, 42, 40, 220);
    QRgb glass     = qRgba(20, 25, 30, 255);
    QRgb tire      = qRgba(18, 18, 18, 255);
    QRgb fireRed   = qRgba(210, 45, 12, 255);
    QRgb fireOrg   = qRgba(255, 135, 20, 255);
    QRgb fireYel   = qRgba(255, 225, 65, 255);

    for (int f = 0; f < 5; ++f) {
        QImage img(64, 48, QImage::Format_ARGB32);
        img.fill(Qt::transparent);

        // Tires
        fillRect(img, 10, 38, 10, 8, tire);
        fillRect(img, 44, 38, 10, 8, tire);

        // Ambassador lower chassis (yellow & charred hood)
        fillRect(img, 8, 26, 48, 14, yellow);
        fillRect(img, 8, 34, 48, 6, darkYel);
        // Black bumper & crushed grille
        fillRect(img, 6, 36, 52, 4, charred);
        fillRect(img, 20, 28, 24, 7, charred);

        // Cabin & roof (Kolkata black roof / yellow body)
        fillRect(img, 14, 17, 36, 10, yellow);
        fillRect(img, 16, 16, 32, 3, charred);
        // Shattered windshield
        fillRect(img, 17, 19, 30, 7, glass);
        drawLine(img, 24, 19, 29, 25, qRgba(120, 125, 130, 255));

        // Charred hood patch
        fillRect(img, 14, 26, 20, 6, charred);

        // 5-frame animated roaring fire, rising smoke & flying embers
        int shift = static_cast<int>(std::sin(f * 1.25) * 3.0);
        int hBoost = (f % 3 == 0) ? 4 : ((f % 2 == 0) ? 2 : 0);
        int smokeRise = (f * 2) % 8;
        fillCircle(img, 22 + shift, 9 - smokeRise / 2, 6, soot, soot);
        fillCircle(img, 34 - shift, 6 - (smokeRise + 3) % 6, 5, soot, soot);

        fillTriangle(img, 13, 27, 22 + shift, 9 - hBoost, 31, 27, fireRed);
        fillTriangle(img, 25, 27, 36 - shift, 10 - hBoost, 45, 27, fireRed);
        fillTriangle(img, 17, 27, 23 + shift, 13 - hBoost, 28, 27, fireOrg);
        fillTriangle(img, 29, 27, 35 - shift, 14 - hBoost, 41, 27, fireOrg);
        fillTriangle(img, 20, 27, 24 + shift, 18, 27, 27, fireYel);
        fillTriangle(img, 32, 27, 36 - shift, 18, 39, 27, fireYel);

        // Flying sparks
        setPixel(img, 18 + f * 3, 5 + (f % 3), fireYel);
        setPixel(img, 42 - f * 2, 4 + ((f + 1) % 3), fireOrg);

        frames.push_back(img);
    }

    sprites[SpriteType::BURNING_TAXI] = frames[0];
    animatedSprites[SpriteType::BURNING_TAXI] = frames;
}

void SpriteAtlas::generateWreckedBus()
{
    QImage img(64, 48, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb busBlue  = qRgba(35, 82, 145, 255);
    QRgb busWhite = qRgba(195, 198, 200, 255);
    QRgb rust     = qRgba(95, 42, 22, 255);
    QRgb dark     = qRgba(22, 22, 26, 255);
    QRgb window   = qRgba(15, 18, 22, 255);

    // Flipped/tilted WBTC blue-white bus body
    fillRect(img, 4, 14, 56, 28, busWhite);
    fillRect(img, 4, 26, 56, 16, busBlue);
    // Scorch & rust patches
    fillRect(img, 12, 16, 16, 10, rust);
    fillRect(img, 40, 28, 18, 12, dark);
    // Dark outline
    fillRect(img, 4, 14, 56, 2, dark);
    fillRect(img, 4, 40, 56, 2, dark);
    fillRect(img, 4, 14, 2, 28, dark);
    fillRect(img, 58, 14, 2, 28, dark);

    // Broken windows along side
    for (int wx = 8; wx < 52; wx += 9) {
        fillRect(img, wx, 18, 7, 7, window);
    }

    // Exposed axle/wheels on underside
    fillCircle(img, 16, 42, 5, dark, qRgba(55, 55, 60, 255));
    fillCircle(img, 48, 42, 5, dark, qRgba(55, 55, 60, 255));

    sprites[SpriteType::WRECKED_BUS] = img;
}

void SpriteAtlas::generateDerailedTram()
{
    QImage img(64, 48, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb cream    = qRgba(190, 185, 155, 255);
    QRgb tramGrn  = qRgba(35, 95, 55, 255);
    QRgb roof     = qRgba(70, 72, 75, 255);
    QRgb dark     = qRgba(20, 22, 20, 255);
    QRgb spark    = qRgba(120, 220, 255, 255);

    // Broken pantograph / overhead wire on roof
    drawLine(img, 24, 12, 32, 3, roof);
    drawLine(img, 32, 3, 42, 12, roof);
    fillCircle(img, 32, 3, 2, spark, spark);

    // Tilted Kolkata tram carriage
    fillRect(img, 6, 12, 52, 6, roof);
    fillRect(img, 6, 18, 52, 12, cream);
    fillRect(img, 6, 30, 52, 12, tramGrn);
    fillRect(img, 6, 42, 52, 3, dark);

    // Arched tram windows
    for (int wx = 10; wx < 52; wx += 8) {
        fillRect(img, wx, 20, 5, 8, dark);
    }
    // Headlight lens (cracked)
    fillCircle(img, 32, 36, 3, qRgba(235, 190, 60, 255), dark);

    sprites[SpriteType::DERAILED_TRAM] = img;
}

void SpriteAtlas::generateBurningAuto()
{
    std::vector<QImage> frames;
    QRgb green   = qRgba(30, 120, 48, 255);
    QRgb yellow  = qRgba(225, 185, 25, 255);
    QRgb canopy  = qRgba(25, 25, 28, 255);
    QRgb fireRed = qRgba(220, 50, 15, 255);
    QRgb fireYel = qRgba(255, 210, 50, 255);

    for (int f = 0; f < 4; ++f) {
        QImage img(64, 48, QImage::Format_ARGB32);
        img.fill(Qt::transparent);

        // 3 wheels
        fillCircle(img, 20, 41, 4, canopy, qRgba(50, 50, 55, 255));
        fillCircle(img, 44, 41, 4, canopy, qRgba(50, 50, 55, 255));

        // Compact auto-rickshaw silhouette
        fillRect(img, 18, 28, 28, 12, green);
        fillRect(img, 20, 18, 24, 10, yellow);
        fillRect(img, 19, 15, 26, 4, canopy);
        fillRect(img, 24, 20, 16, 7, qRgba(18, 20, 24, 255));

        // 4-frame Flames & smoke
        int dx = static_cast<int>(std::sin(f * 1.5) * 3.0);
        int dy = (f % 2 == 0) ? 0 : 2;
        fillCircle(img, 31 + dx, 5, 4, qRgba(45, 42, 40, 200), qRgba(45, 42, 40, 200));
        fillTriangle(img, 22, 20, 30 + dx, 6 - dy, 38, 20, fireRed);
        fillTriangle(img, 25, 20, 31 + dx, 11 - dy, 36, 20, fireYel);

        frames.push_back(img);
    }

    sprites[SpriteType::BURNING_AUTO] = frames[0];
    animatedSprites[SpriteType::BURNING_AUTO] = frames;
}

void SpriteAtlas::generateSteelGirder()
{
    QImage img(64, 48, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb rustDark = qRgba(75, 35, 24, 255);
    QRgb rustMid  = qRgba(125, 58, 34, 255);
    QRgb steel    = qRgba(95, 98, 105, 255);

    // Diagonal fallen I-beam
    for (int t = 0; t < 54; ++t) {
        int x = 5 + t;
        int y = 38 - (t * 24) / 54;
        fillRect(img, x, y - 4, 4, 8, rustMid);
        fillRect(img, x, y - 5, 4, 2, steel);
        fillRect(img, x, y + 3, 4, 2, rustDark);
    }
    sprites[SpriteType::STEEL_GIRDER] = img;
}

void SpriteAtlas::generateConcreteRubble()
{
    QImage img(64, 48, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb gray1 = qRgba(85, 82, 78, 255);
    QRgb gray2 = qRgba(62, 60, 56, 255);
    QRgb dark  = qRgba(35, 34, 32, 255);
    QRgb rebar = qRgba(130, 55, 30, 255);

    fillTriangle(img, 8, 44, 26, 22, 46, 44, gray2);
    fillTriangle(img, 22, 44, 38, 18, 56, 44, gray1);
    fillRect(img, 14, 32, 14, 12, gray1);
    fillRect(img, 32, 34, 18, 10, dark);
    drawLine(img, 24, 26, 18, 12, rebar);
    drawLine(img, 38, 22, 45, 10, rebar);

    sprites[SpriteType::CONCRETE_RUBBLE] = img;
}

void SpriteAtlas::generateRoadBarrier()
{
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb concrete = qRgba(95, 92, 88, 255);
    QRgb shade    = qRgba(55, 53, 50, 255);
    QRgb hazard   = qRgba(225, 145, 20, 255);

    fillRect(img, 4, 16, 24, 12, shade);
    fillRect(img, 6, 12, 20, 6, concrete);
    // Hazard stripes
    fillRect(img, 6, 18, 5, 4, hazard);
    fillRect(img, 15, 18, 5, 4, hazard);
    fillRect(img, 23, 18, 4, 4, hazard);

    sprites[SpriteType::ROAD_BARRIER] = img;
}

// ----------------------------------------------------------------------------
// Monuments (128x96)
// ----------------------------------------------------------------------------

void SpriteAtlas::generateVictoriaMemorial()
{
    QImage img(128, 96, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb marble     = qRgba(175, 172, 165, 255);
    QRgb marbleDark = qRgba(110, 108, 104, 255);
    QRgb soot       = qRgba(45, 42, 40, 255);
    QRgb domeOxid   = qRgba(85, 105, 95, 255);
    QRgb fireGlow   = qRgba(210, 85, 25, 255);

    // Base plinth & flanking colonnades
    fillRect(img, 10, 68, 108, 24, marbleDark);
    fillRect(img, 14, 52, 100, 18, marble);
    // Colonnade pillars
    for (int x = 16; x < 110; x += 8) {
        fillRect(img, x, 54, 4, 16, marbleDark);
    }

    // Central hall & cracked main dome
    fillRect(img, 40, 36, 48, 34, marble);
    fillCircle(img, 64, 36, 20, domeOxid, soot);
    fillRect(img, 42, 36, 44, 12, marble);

    // Corner cupolas (one collapsed)
    fillCircle(img, 24, 46, 7, domeOxid, soot);
    fillCircle(img, 104, 48, 6, soot, soot);

    // Cracks, soot streaks & fire inside central arch
    fillRect(img, 56, 52, 16, 18, fireGlow);
    drawLine(img, 58, 18, 66, 35, soot);
    drawLine(img, 66, 35, 62, 50, soot);

    sprites[SpriteType::VICTORIA_MEMORIAL] = img;
}

void SpriteAtlas::generateShaheedMinar()
{
    QImage img(128, 96, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb stone     = qRgba(145, 140, 132, 255);
    QRgb stoneDark = qRgba(85, 80, 75, 255);
    QRgb soot      = qRgba(35, 32, 30, 255);

    // Base pedestal
    fillRect(img, 42, 78, 44, 14, stoneDark);
    fillRect(img, 48, 70, 32, 8, stone);

    // Leaning fluted obelisk shaft
    for (int y = 14; y < 70; ++y) {
        int lean = (70 - y) / 7; // leans slightly right
        int w = 14 + (y - 14) / 8;
        fillRect(img, 57 + lean - w / 2, y, w, 1, stone);
        fillRect(img, 57 + lean + w / 4, y, w / 4, 1, stoneDark);
    }

    // Broken upper balcony
    fillRect(img, 54, 22, 24, 5, soot);
    // Shattered top
    fillTriangle(img, 59, 14, 65, 6, 72, 14, stoneDark);

    sprites[SpriteType::SHAHEED_MINAR] = img;
}

void SpriteAtlas::generateWritersBuilding()
{
    QImage img(128, 96, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb brickRed  = qRgba(125, 42, 34, 255);
    QRgb brickDark = qRgba(78, 25, 20, 255);
    QRgb pediment  = qRgba(150, 138, 125, 255);
    QRgb fire      = qRgba(245, 125, 25, 255);
    QRgb dark      = qRgba(24, 18, 16, 255);

    // Long colonial red-brick facade
    fillRect(img, 6, 32, 116, 60, brickRed);
    fillRect(img, 6, 64, 116, 28, brickDark);

    // Central classical portico & pediment
    fillRect(img, 42, 22, 44, 70, brickRed);
    fillTriangle(img, 38, 24, 64, 10, 90, 24, pediment);

    // Pillars & burning windows
    for (int x = 12; x < 114; x += 10) {
        fillRect(img, x, 36, 4, 52, brickDark);
        QRgb winCol = ((x / 10) % 3 == 0) ? fire : dark;
        fillRect(img, x + 5, 40, 4, 10, winCol);
        fillRect(img, x + 5, 60, 4, 12, dark);
    }

    sprites[SpriteType::WRITERS_BUILDING] = img;
}

void SpriteAtlas::generateHowrahBridgeTower()
{
    QImage img(128, 96, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb steel     = qRgba(105, 110, 118, 255);
    QRgb steelDark = qRgba(52, 56, 62, 255);
    QRgb rust      = qRgba(110, 52, 28, 255);
    QRgb spark     = qRgba(255, 185, 50, 255);

    // Twin main cantilever legs
    fillRect(img, 24, 10, 14, 84, steelDark);
    fillRect(img, 90, 10, 14, 84, steelDark);
    fillRect(img, 26, 10, 6, 84, steel);
    fillRect(img, 92, 10, 6, 84, steel);

    // Cross-beams & X-bracing
    fillRect(img, 22, 12, 84, 8, steel);
    fillRect(img, 22, 42, 84, 6, rust);
    drawLine(img, 36, 20, 90, 42, steel);
    drawLine(img, 90, 20, 36, 42, steel);

    // Snapped suspension cables dangling
    drawLine(img, 10, 35, 24, 14, steel);
    drawLine(img, 104, 14, 118, 48, rust);
    fillCircle(img, 118, 48, 3, spark, spark);

    sprites[SpriteType::HOWRAH_BRIDGE_TOWER] = img;
}

void SpriteAtlas::generateGariahatFlyoverPier()
{
    QImage img(128, 96, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb concrete  = qRgba(105, 102, 98, 255);
    QRgb shade     = qRgba(62, 60, 58, 255);
    QRgb rebar     = qRgba(140, 55, 28, 255);
    QRgb dark      = qRgba(25, 24, 24, 255);

    // Heavy T-shaped flyover pier
    fillRect(img, 48, 28, 32, 66, shade);
    fillRect(img, 52, 28, 14, 66, concrete);
    fillRect(img, 16, 14, 96, 16, concrete);
    fillRect(img, 16, 26, 96, 4, shade);

    // Sheared right edge with exposed rebar
    fillTriangle(img, 92, 14, 114, 14, 102, 30, Qt::transparent);
    drawLine(img, 96, 18, 118, 22, rebar);
    drawLine(img, 98, 24, 116, 28, rebar);
    drawLine(img, 54, 42, 72, 64, dark);

    sprites[SpriteType::GARIAHAT_FLYOVER_PIER] = img;
}

void SpriteAtlas::generateEsplanadeMetro()
{
    QImage img(128, 96, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb concrete = qRgba(92, 90, 88, 255);
    QRgb dark     = qRgba(16, 14, 16, 255);
    QRgb metroBlu = qRgba(30, 85, 155, 255);
    QRgb orange   = qRgba(235, 115, 25, 255);

    // Tilted metro stairwell hood
    fillRect(img, 24, 34, 80, 58, concrete);
    fillRect(img, 34, 48, 60, 44, dark);

    // Cracked Kolkata Metro "M" sign tilted
    fillRect(img, 44, 16, 40, 18, metroBlu);
    drawLine(img, 52, 30, 56, 20, orange);
    drawLine(img, 56, 20, 64, 28, orange);
    drawLine(img, 64, 28, 72, 20, orange);
    drawLine(img, 72, 20, 76, 30, orange);

    sprites[SpriteType::ESPLANADE_METRO] = img;
}

void SpriteAtlas::generateStPaulsCathedral()
{
    QImage img(128, 96, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb white = qRgba(185, 182, 175, 255);
    QRgb shade = qRgba(115, 112, 108, 255);
    QRgb dark  = qRgba(28, 26, 28, 255);

    // Cathedral nave & Gothic arches
    fillRect(img, 20, 48, 88, 44, white);
    fillRect(img, 46, 28, 36, 64, shade);
    fillTriangle(img, 48, 28, 64, 4, 80, 28, white); // Broken spire tip
    fillRect(img, 60, 4, 8, 8, Qt::transparent);

    // Pointed Gothic windows
    for (int x = 28; x <= 94; x += 18) {
        fillRect(img, x, 58, 8, 18, dark);
    }
    fillRect(img, 58, 62, 12, 28, dark);

    sprites[SpriteType::ST_PAULS_CATHEDRAL] = img;
}

void SpriteAtlas::generateThe42Tower()
{
    QImage img(128, 96, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb glass = qRgba(38, 48, 58, 255);
    QRgb steel = qRgba(75, 80, 88, 255);
    QRgb fire  = qRgba(235, 95, 20, 255);

    // Tall slender tower snapped near top
    fillRect(img, 46, 24, 36, 70, steel);
    fillRect(img, 50, 26, 28, 68, glass);
    // Sheared upper section tilting
    fillTriangle(img, 46, 24, 62, 6, 82, 20, steel);
    // Fires on middle floors
    fillRect(img, 52, 46, 10, 6, fire);
    fillRect(img, 66, 62, 8, 5, fire);

    sprites[SpriteType::THE_42_TOWER] = img;
}

// ----------------------------------------------------------------------------
// Overhead Spans (160x64) — Transparent lower center to drive under
// ----------------------------------------------------------------------------

void SpriteAtlas::generateFlyoverBeamOverhead()
{
    QImage img(160, 64, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb concrete = qRgba(95, 92, 86, 255);
    QRgb shade    = qRgba(52, 50, 48, 255);
    QRgb rebar    = qRgba(145, 60, 30, 255);

    // Top deck girder
    fillRect(img, 0, 4, 160, 18, concrete);
    fillRect(img, 0, 18, 160, 6, shade);
    // Side pillars leaving open middle
    fillRect(img, 4, 22, 16, 42, shade);
    fillRect(img, 140, 22, 16, 42, shade);
    // Dangling rebar
    drawLine(img, 60, 24, 65, 36, rebar);
    drawLine(img, 98, 24, 92, 34, rebar);

    sprites[SpriteType::FLYOVER_BEAM_OVERHEAD] = img;
}

void SpriteAtlas::generateBridgeTrussOverhead()
{
    QImage img(160, 64, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb steel = qRgba(95, 100, 108, 255);
    QRgb dark  = qRgba(45, 48, 54, 255);

    fillRect(img, 0, 4, 160, 6, steel);
    fillRect(img, 0, 22, 160, 5, dark);
    for (int x = 0; x < 140; x += 20) {
        drawLine(img, x, 6, x + 20, 24, steel);
        drawLine(img, x + 20, 6, x, 24, dark);
    }
    fillRect(img, 4, 24, 12, 40, dark);
    fillRect(img, 144, 24, 12, 40, dark);

    sprites[SpriteType::BRIDGE_TRUSS_OVERHEAD] = img;
}

void SpriteAtlas::generateScaffoldOverhead()
{
    QImage img(160, 64, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb bamboo = qRgba(145, 112, 62, 255);
    QRgb pipe   = qRgba(110, 112, 118, 255);

    fillRect(img, 0, 8, 160, 4, pipe);
    fillRect(img, 0, 18, 160, 3, bamboo);
    fillRect(img, 8, 8, 6, 56, bamboo);
    fillRect(img, 146, 8, 6, 56, bamboo);
    drawLine(img, 14, 12, 36, 36, bamboo);
    drawLine(img, 146, 12, 124, 36, bamboo);

    sprites[SpriteType::SCAFFOLD_OVERHEAD] = img;
}

// ----------------------------------------------------------------------------
// Falling Variants (48x64)
// ----------------------------------------------------------------------------

void SpriteAtlas::generateGirderFalling()
{
    QImage img(48, 64, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb rust  = qRgba(145, 62, 32, 255);
    QRgb steel = qRgba(95, 98, 105, 255);
    QRgb spark = qRgba(255, 210, 60, 255);

    for (int i = 0; i < 48; ++i) {
        fillRect(img, i, 8 + i / 2, 3, 12, rust);
        fillRect(img, i, 8 + i / 2, 3, 3, steel);
    }
    fillCircle(img, 10, 12, 2, spark, spark);
    fillCircle(img, 38, 36, 2, spark, spark);

    sprites[SpriteType::GIRDER_FALLING] = img;
}

void SpriteAtlas::generateScaffoldFalling()
{
    QImage img(48, 64, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb bamboo = qRgba(150, 115, 62, 255);
    QRgb metal  = qRgba(100, 104, 110, 255);

    drawLine(img, 6, 8, 42, 54, bamboo);
    drawLine(img, 40, 10, 8, 52, bamboo);
    drawLine(img, 4, 28, 44, 36, metal);
    drawLine(img, 16, 4, 32, 58, metal);

    sprites[SpriteType::SCAFFOLD_FALLING] = img;
}

void SpriteAtlas::generateBridgeTrussFalling()
{
    QImage img(48, 64, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb steel = qRgba(100, 106, 114, 255);
    QRgb dark  = qRgba(45, 48, 54, 255);
    QRgb spark = qRgba(255, 175, 40, 255);

    fillRect(img, 6, 14, 36, 6, steel);
    fillRect(img, 6, 40, 36, 6, dark);
    drawLine(img, 8, 18, 40, 42, steel);
    drawLine(img, 40, 18, 8, 42, dark);
    fillCircle(img, 24, 30, 3, spark, spark);

    sprites[SpriteType::BRIDGE_TRUSS_FALLING] = img;
}

// ----------------------------------------------------------------------------
// Survivor Rigs (48x48) — Rear-view billboards
// ----------------------------------------------------------------------------

void SpriteAtlas::generateRigKaaliPeeli()
{
    QImage img(48, 48, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb yellow  = qRgba(235, 185, 20, 255);
    QRgb black   = qRgba(24, 24, 26, 255);
    QRgb glass   = qRgba(45, 55, 68, 255);
    QRgb tailRed = qRgba(235, 45, 35, 255);
    QRgb steel   = qRgba(125, 128, 134, 255);

    // Tires
    fillRect(img, 7, 34, 8, 10, black);
    fillRect(img, 33, 34, 8, 10, black);
    // Body & black taxi roof
    fillRect(img, 8, 22, 32, 14, yellow);
    fillRect(img, 12, 12, 24, 11, black);
    fillRect(img, 15, 14, 18, 7, glass);
    // Welded steel bull-bar / bumper
    fillRect(img, 6, 33, 36, 4, steel);
    // Taillights
    fillRect(img, 10, 25, 5, 4, tailRed);
    fillRect(img, 33, 25, 5, 4, tailRed);
    // Roof taxi sign
    fillRect(img, 20, 8, 8, 4, qRgba(240, 180, 40, 255));

    sprites[SpriteType::RIG_KAALI_PEELI] = img;
}

void SpriteAtlas::generateRigDozer()
{
    QImage img(48, 48, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb olive = qRgba(58, 74, 52, 255);
    QRgb dark  = qRgba(26, 30, 28, 255);
    QRgb rust  = qRgba(185, 105, 25, 255);
    QRgb steel = qRgba(110, 114, 120, 255);

    // Heavy wide tires
    fillRect(img, 4, 32, 10, 13, dark);
    fillRect(img, 34, 32, 10, 13, dark);
    // Armored truck rear
    fillRect(img, 6, 12, 36, 24, olive);
    fillRect(img, 10, 16, 28, 16, steel);
    // Hazard stripes & rear ram bar
    fillRect(img, 6, 32, 36, 5, rust);
    fillRect(img, 12, 34, 24, 3, dark);

    sprites[SpriteType::RIG_DOZER] = img;
}

void SpriteAtlas::generateRigRunner()
{
    QImage img(48, 48, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb red    = qRgba(195, 50, 30, 255);
    QRgb gold   = qRgba(245, 195, 45, 255);
    QRgb dark   = qRgba(20, 20, 24, 255);
    QRgb glass  = qRgba(40, 48, 60, 255);

    // Wide low tires
    fillRect(img, 6, 34, 9, 9, dark);
    fillRect(img, 33, 34, 9, 9, dark);
    // Sleek coupe body + rear spoiler
    fillRect(img, 8, 24, 32, 12, red);
    fillRect(img, 10, 18, 28, 3, dark); // spoiler
    fillRect(img, 14, 15, 20, 9, red);
    fillRect(img, 16, 17, 16, 5, glass);
    // Racing stripe
    fillRect(img, 22, 15, 4, 21, gold);

    sprites[SpriteType::RIG_RUNNER] = img;
}

void SpriteAtlas::generateRigMedvan()
{
    QImage img(48, 48, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QRgb white  = qRgba(212, 216, 222, 255);
    QRgb red    = qRgba(205, 35, 35, 255);
    QRgb dark   = qRgba(28, 30, 34, 255);
    QRgb blue   = qRgba(45, 135, 240, 255);

    // Tires
    fillRect(img, 7, 34, 8, 10, dark);
    fillRect(img, 33, 34, 8, 10, dark);
    // Tall ambulance box
    fillRect(img, 8, 10, 32, 26, white);
    fillRect(img, 8, 26, 32, 4, red);
    // Rear cross
    fillRect(img, 22, 14, 4, 10, red);
    fillRect(img, 19, 17, 10, 4, red);
    // Roof emergency lightbar
    fillRect(img, 14, 6, 8, 4, red);
    fillRect(img, 26, 6, 8, 4, blue);

    sprites[SpriteType::RIG_MEDVAN] = img;
}

void SpriteAtlas::generateRigAlts()
{
    auto tint = [](const QImage& src, QRgb fromHint, QRgb toColor) {
        QImage out = src.copy();
        for (int y = 0; y < out.height(); ++y) {
            for (int x = 0; x < out.width(); ++x) {
                QRgb c = out.pixel(x, y);
                if (qAlpha(c) < 32) continue;
                int dr = std::abs(qRed(c) - qRed(fromHint));
                int dg = std::abs(qGreen(c) - qGreen(fromHint));
                if (dr < 50 && dg < 50) {
                    out.setPixel(x, y, toColor);
                }
            }
        }
        return out;
    };

    sprites[SpriteType::RIG_KAALI_PEELI_ALT] = tint(sprites[SpriteType::RIG_KAALI_PEELI], qRgba(235, 185, 20, 255), qRgba(175, 120, 25, 255));
    sprites[SpriteType::RIG_DOZER_ALT]       = tint(sprites[SpriteType::RIG_DOZER], qRgba(58, 74, 52, 255), qRgba(45, 55, 68, 255));
    sprites[SpriteType::RIG_RUNNER_ALT]      = tint(sprites[SpriteType::RIG_RUNNER], qRgba(195, 50, 30, 255), qRgba(130, 40, 90, 255));
    sprites[SpriteType::RIG_MEDVAN_ALT]      = tint(sprites[SpriteType::RIG_MEDVAN], qRgba(212, 216, 222, 255), qRgba(165, 175, 188, 255));

    // Generate 3-angle lean frames (-1 left, 0 center, +1 right) for every survivor rig
    auto makeSheared = [](const QImage& src, int dir) {
        QImage out(src.width(), src.height(), QImage::Format_ARGB32);
        out.fill(Qt::transparent);
        for (int y = 0; y < src.height(); ++y) {
            // Top leans further than tires at bottom
            int shift = dir * ((src.height() - y) / 9);
            for (int x = 0; x < src.width(); ++x) {
                int nx = x + shift;
                if (nx >= 0 && nx < out.width()) {
                    QRgb c = src.pixel(x, y);
                    if (qAlpha(c) >= 32) out.setPixel(nx, y, c);
                }
            }
        }
        return out;
    };

    const SpriteType rigTypes[] = {
        SpriteType::RIG_KAALI_PEELI, SpriteType::RIG_DOZER,
        SpriteType::RIG_RUNNER,      SpriteType::RIG_MEDVAN,
        SpriteType::RIG_KAALI_PEELI_ALT, SpriteType::RIG_DOZER_ALT,
        SpriteType::RIG_RUNNER_ALT,      SpriteType::RIG_MEDVAN_ALT
    };
    for (SpriteType rt : rigTypes) {
        const QImage &base = sprites[rt];
        tiltedVehicles[rt] = { makeSheared(base, -1), base, makeSheared(base, 1) };
    }
}

// ----------------------------------------------------------------------------
// Legacy Compat Mappings
// ----------------------------------------------------------------------------

void SpriteAtlas::mapLegacySprites()
{
    sprites[SpriteType::QUESTION_BOX]         = sprites[SpriteType::SUPPLY_CRATE];
    sprites[SpriteType::MUSHROOM]             = sprites[SpriteType::NITROUS_CELL];
    sprites[SpriteType::ROCKET]               = sprites[SpriteType::REPAIR_KIT];
    sprites[SpriteType::BANANA]               = sprites[SpriteType::ROAD_BARRIER];
    sprites[SpriteType::GREEN_SHELL]          = sprites[SpriteType::NITROUS_CELL];
    sprites[SpriteType::INK_BLOOPER]          = sprites[SpriteType::SUPPLY_CRATE];
    sprites[SpriteType::GREEN_PIPE]           = sprites[SpriteType::ROAD_BARRIER];
    sprites[SpriteType::THWOMP]               = sprites[SpriteType::STEEL_GIRDER];
    sprites[SpriteType::OIL_SLICK]            = sprites[SpriteType::CONCRETE_RUBBLE];
    sprites[SpriteType::COIN]                 = sprites[SpriteType::SCRAP_CACHE];
    sprites[SpriteType::BANANA_DROPPED]       = sprites[SpriteType::CONCRETE_RUBBLE];
    sprites[SpriteType::SHELL_PROJECTILE]     = sprites[SpriteType::NITROUS_CELL];

    sprites[SpriteType::KART_MARIO]           = sprites[SpriteType::RIG_KAALI_PEELI];
    sprites[SpriteType::KART_BOWSER]          = sprites[SpriteType::RIG_DOZER];
    sprites[SpriteType::KART_PEACH]           = sprites[SpriteType::RIG_RUNNER];
    sprites[SpriteType::KART_TOAD]            = sprites[SpriteType::RIG_MEDVAN];
    sprites[SpriteType::KART_MARIO_ALT]       = sprites[SpriteType::RIG_KAALI_PEELI_ALT];
    sprites[SpriteType::KART_BOWSER_ALT]      = sprites[SpriteType::RIG_DOZER_ALT];
    sprites[SpriteType::KART_PEACH_ALT]       = sprites[SpriteType::RIG_RUNNER_ALT];
    sprites[SpriteType::KART_TOAD_ALT]        = sprites[SpriteType::RIG_MEDVAN_ALT];

    sprites[SpriteType::LAVA_GEYSER_BUBBLING] = sprites[SpriteType::BURNING_AUTO];
    sprites[SpriteType::LAVA_GEYSER_ERUPTING] = sprites[SpriteType::BURNING_TAXI];
    sprites[SpriteType::CHOCOLATE_FOUNTAIN]   = sprites[SpriteType::CONCRETE_RUBBLE];
    sprites[SpriteType::CHOCOLATE_CANDY]      = sprites[SpriteType::STEEL_GIRDER];
}
