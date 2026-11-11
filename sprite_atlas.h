#ifndef SPRITE_ATLAS_H
#define SPRITE_ATLAS_H

#include <QImage>
#include <QColor>
#include <map>
#include <vector>
#include "kolkata_types.h"

// ============================================================================
// KOLKATA: LAST EXIT — Sprite Atlas
// All sprites generated procedurally using pixel manipulation.
// No external assets. Dark apocalyptic Kolkata aesthetic.
// ============================================================================

class SpriteAtlas
{
public:
    static SpriteAtlas& instance();

    const QImage& getSprite(SpriteType type) const;

    // Animation support: multi-frame sprites (burning vehicles, etc.)
    const QImage& getAnimFrame(SpriteType type, int frame) const;
    const QImage& getVehicleFrame(SpriteType type, int leanDir) const;
    int getFrameCount(SpriteType type) const;

    bool isPickup(SpriteType type) const;
    bool isObstacle(SpriteType type) const;

private:
    SpriteAtlas();
    void generateAllSprites();

    // --- Pickup generators (32×32) ---
    void generateSupplyCrate();
    void generateRepairKit();
    void generateNitrousCell();
    void generateScrapCache();

    // --- Obstacle generators (64×48) ---
    void generateBurningTaxi();      // 3-frame fire animation
    void generateWreckedBus();
    void generateDerailedTram();
    void generateBurningAuto();      // 2-frame fire animation
    void generateSteelGirder();
    void generateConcreteRubble();
    void generateRoadBarrier();

    // --- Monument billboard generators (128×96) ---
    void generateVictoriaMemorial();
    void generateShaheedMinar();
    void generateWritersBuilding();
    void generateHowrahBridgeTower();
    void generateGariahatFlyoverPier();
    void generateEsplanadeMetro();
    void generateStPaulsCathedral();
    void generateThe42Tower();

    // --- Overhead span generators (160×64) ---
    void generateFlyoverBeamOverhead();
    void generateBridgeTrussOverhead();
    void generateScaffoldOverhead();

    // --- Falling variant generators (48×64) ---
    void generateGirderFalling();
    void generateScaffoldFalling();
    void generateBridgeTrussFalling();

    // --- Vehicle billboard generators (48×48) ---
    void generateRigKaaliPeeli();
    void generateRigDozer();
    void generateRigRunner();
    void generateRigMedvan();
    void generateRigAlts();

    // --- Legacy stub mappings ---
    void mapLegacySprites();

    // --- Drawing helpers ---
    void setPixel(QImage& img, int x, int y, QRgb color);
    void fillCircle(QImage& img, int cx, int cy, int r, QRgb fill, QRgb outline);
    void fillRect(QImage& img, int x, int y, int w, int h, QRgb color);
    void drawLine(QImage& img, int x1, int y1, int x2, int y2, QRgb color);
    void fillTriangle(QImage& img, int x1, int y1, int x2, int y2, int x3, int y3, QRgb color);

    // Color interpolation helper
    QRgb lerpColor(QRgb a, QRgb b, double t);

    // Sprite storage
    std::map<SpriteType, QImage> sprites;
    std::map<SpriteType, std::vector<QImage>> animatedSprites;
    std::map<SpriteType, std::vector<QImage>> tiltedVehicles; // [0]=left, [1]=center, [2]=right
};

#endif // SPRITE_ATLAS_H
