#pragma once

#include <format>
#include <vector>

#include "LevelSource.h"
#include "minecraft/world/level/LevelSource.h"
#include "minecraft/world/level/LightLayer.h"

class Material;
class TileEntity;
class BiomeSource;
class Level;
class LevelChunk;
class ProgressListener;

class Region : public LevelSource {
private:
    int xc1, zc1;
    std::vector<std::vector<LevelChunk*>>* chunks;
    Level* level;
    bool allEmpty;

    // AP - Sistema de caché original de Xbox 360 (Mantenido intacto para Steve y entidades)
    int xcCached, zcCached;
    unsigned char* CachedTiles;

    // Modern PC - Canal de caché de alto rendimiento aislado (Exclusivo para rebuild de Chunks)
    int m_pcOriginX, m_pcOriginZ;
    unsigned char* m_pcTiles;
    unsigned char* m_pcData;
    unsigned char* m_pcBlockLight;
    unsigned char* m_pcSkyLight;
    bool m_pcCacheActive;

public:
    Region(Level* level, int x1, int y1, int z1, int x2, int y2, int z2, int r);
    virtual ~Region();
    
    // Método exclusivo de PC para activar el súper-caché de reconstrucción
    void enableCache(int chunkX, int chunkZ);

    bool isAllEmpty();
    int getTile(int x, int y, int z);
    std::shared_ptr<TileEntity> getTileEntity(int x, int y, int z);
    float getBrightness(int x, int y, int z, int emitt);
    float getBrightness(int x, int y, int z);
    int getLightColor(int x, int y, int z, int emitt, int tileId = -1);
    int getRawBrightness(int x, int y, int z);
    int getRawBrightness(int x, int y, int z, bool propagate);
    int getData(int x, int y, int z);
    Material* getMaterial(int x, int y, int z);
    BiomeSource* getBiomeSource();
    Biome* getBiome(int x, int z);
    bool isSolidRenderTile(int x, int y, int z);
    bool isSolidBlockingTile(int x, int y, int z);
    bool isTopSolidBlocking(int x, int y, int z);
    bool isEmptyTile(int x, int y, int z);

    int getBrightnessPropagate(LightLayer::variety layer, int x, int y, int z, int tileId);
    int getBrightness(LightLayer::variety layer, int x, int y, int z);

    int getMaxBuildHeight();
    int getDirectSignal(int x, int y, int z, int dir);

    LevelChunk* getLevelChunk(int x, int y, int z);

    // Legacy Xbox 360 API (Preservada sin cambios para evitar bugs visuales)
    void setCachedTiles(unsigned char* tiles, int xc, int zc);
};
