#include "Region.h"

#include <stdlib.h>
#include <string.h>

#include <memory>
#include <vector>

#include "Level.h"
#include "minecraft/world/level/chunk/LevelChunk.h"
#include "minecraft/world/level/dimension/Dimension.h"
#include "minecraft/world/level/material/Material.h"
#include "minecraft/world/level/redstone/Redstone.h"
#include "minecraft/world/level/tile/Tile.h"

Region::~Region() {
    // 0 allocations en constructor = 0 deallocations en destructor. ¡Rendimiento instantáneo!
    if (CachedTiles) {
        free(CachedTiles);
    }
}

Region::Region(Level* level, int x1, int y1, int z1, int x2, int y2, int z2, int r) {
    this->level = level;
    this->chunks = nullptr; // Forzamos a nulo para evitar asignación del vector lento

    xc1 = (x1 - r) >> 4;
    zc1 = (z1 - r) >> 4;
    int xc2 = (x2 + r) >> 4;
    int zc2 = (z2 + r) >> 4;

    m_width = xc2 - xc1 + 1;
    m_height = zc2 - zc1 + 1;

    // Límite de seguridad para nuestra matriz estática
    if (m_width > 16) m_width = 16;
    if (m_height > 16) m_height = 16;

    // Limpiamos la matriz estática
    memset(m_chunks, 0, sizeof(m_chunks));

    // Rellenamos la matriz estática directamente de la memoria física
    for (int lx = 0; lx < m_width; lx++) {
        int xc = xc1 + lx;
        for (int lz = 0; lz < m_height; lz++) {
            int zc = zc1 + lz;
            LevelChunk* chunk = level->getChunk(xc, zc);
            if (chunk != nullptr) {
                m_chunks[lx][lz] = chunk;
            }
        }
    }

    // Calculamos si la sección está vacía reproduciendo fielmente la lógica de Xbox 360
    allEmpty = true;
    int startX = (x1 >> 4) - xc1;
    int endX = (x2 >> 4) - xc1;
    int startZ = (z1 >> 4) - zc1;
    int endZ = (z2 >> 4) - zc1;

    for (int lx = startX; lx <= endX; lx++) {
        for (int lz = startZ; lz <= endZ; lz++) {
            if (lx >= 0 && lx < m_width && lz >= 0 && lz < m_height) {
                LevelChunk* chunk = m_chunks[lx][lz];
                if (chunk != nullptr) {
                    if (!chunk->isYSpaceEmpty(y1, y2)) {
                        allEmpty = false;
                    }
                }
            }
        }
    }

    // Legacy Xbox 360
    xcCached = -1;
    zcCached = -1;
    CachedTiles = nullptr;
}

void Region::setCachedTiles(unsigned char* tiles, int xc, int zc) {
    xcCached = xc;
    zcCached = zc;
    int size = 16 * 16 * Level::maxBuildHeight;
    if (CachedTiles == nullptr) {
        CachedTiles = (unsigned char*)malloc(size);
    }
    memcpy(CachedTiles, tiles, size);
}

bool Region::isAllEmpty() { return allEmpty; }

int Region::getTile(int x, int y, int z) {
    if (y < 0 || y >= Level::maxBuildHeight) return 0;

    int lx = (x >> 4) - xc1;
    int lz = (z >> 4) - zc1;

    if (lx >= 0 && lx < m_width && lz >= 0 && lz < m_height) {
        LevelChunk* lc = m_chunks[lx][lz];
        if (lc != nullptr) {
            return lc->getTile(x & 15, y, z & 15);
        }
    }
    return 0;
}

int Region::getData(int x, int y, int z) {
    if (y < 0 || y >= Level::maxBuildHeight) return 0;

    int lx = (x >> 4) - xc1;
    int lz = (z >> 4) - zc1;

    if (lx >= 0 && lx < m_width && lz >= 0 && lz < m_height) {
        LevelChunk* lc = m_chunks[lx][lz];
        if (lc != nullptr) {
            return lc->getData(x & 15, y, z & 15);
        }
    }
    return 0;
}

int Region::getBrightness(LightLayer::variety layer, int x, int y, int z) {
    if (y < 0) y = 0;
    if (y >= Level::maxBuildHeight) y = Level::maxBuildHeight - 1;
    if (x < -Level::MAX_LEVEL_SIZE || z < -Level::MAX_LEVEL_SIZE ||
        x >= Level::MAX_LEVEL_SIZE || z > Level::MAX_LEVEL_SIZE) {
        return (int)layer;
    }

    int lx = (x >> 4) - xc1;
    int lz = (z >> 4) - zc1;

    if (lx >= 0 && lx < m_width && lz >= 0 && lz < m_height) {
        LevelChunk* lc = m_chunks[lx][lz];
        if (lc != nullptr) {
            return lc->getBrightness(layer, x & 15, y, z & 15);
        }
    }
    return (int)layer;
}

int Region::getBrightnessPropagate(LightLayer::variety layer, int x, int y, int z, int tileId) {
    if (y < 0) y = 0;
    if (y >= Level::maxBuildHeight) y = Level::maxBuildHeight - 1;
    if (x < -Level::MAX_LEVEL_SIZE || z < -Level::MAX_LEVEL_SIZE ||
        x >= Level::MAX_LEVEL_SIZE || z > Level::MAX_LEVEL_SIZE) {
        return (int)layer;
    }
    if (layer == LightLayer::Sky && level->dimension->hasCeiling) {
        return 0;
    }

    int id = tileId > -1 ? tileId : getTile(x, y, z);
    if (Tile::propagate[id]) {
        int br = getBrightness(layer, x, y + 1, z);
        if (br == 15) return 15;
        int br1 = getBrightness(layer, x + 1, y, z);
        if (br1 == 15) return 15;
        int br2 = getBrightness(layer, x - 1, y, z);
        if (br2 == 15) return 15;
        int br3 = getBrightness(layer, x, y, z + 1);
        if (br3 == 15) return 15;
        int br4 = getBrightness(layer, x, y, z - 1);
        if (br4 == 15) return 15;
        if (br1 > br) br = br1;
        if (br2 > br) br = br2;
        if (br3 > br) br = br3;
        if (br4 > br) br = br4;
        return br;
    }

    return getBrightness(layer, x, y, z);
}

LevelChunk* Region::getLevelChunk(int x, int y, int z) {
    if (y < 0 || y >= Level::maxBuildHeight) return nullptr;
    int lx = (x >> 4) - xc1;
    int lz = (z >> 4) - zc1;
    if (lx >= 0 && lx < m_width && lz >= 0 && lz < m_height) {
        return m_chunks[lx][lz];
    }
    return nullptr;
}

std::shared_ptr<TileEntity> Region::getTileEntity(int x, int y, int z) {
    int lx = (x >> 4) - xc1;
    int lz = (z >> 4) - zc1;
    if (lx >= 0 && lx < m_width && lz >= 0 && lz < m_height) {
        LevelChunk* lc = m_chunks[lx][lz];
        if (lc != nullptr) {
            return lc->getTileEntity(x & 15, y, z & 15);
        }
    }
    return nullptr;
}

int Region::getLightColor(int x, int y, int z, int emitt, int tileId) {
    int s = getBrightnessPropagate(LightLayer::Sky, x, y, z, tileId);
    int b = getBrightnessPropagate(LightLayer::Block, x, y, z, tileId);
    if (b < emitt) b = emitt;
    return s << 20 | b << 4;
}

float Region::getBrightness(int x, int y, int z, int emitt) {
    int n = getRawBrightness(x, y, z);
    if (n < emitt) n = emitt;
    return level->dimension->brightnessRamp[n];
}

float Region::getBrightness(int x, int y, int z) {
    return level->dimension->brightnessRamp[getRawBrightness(x, y, z)];
}

int Region::getRawBrightness(int x, int y, int z) {
    return getRawBrightness(x, y, z, true);
}

int Region::getRawBrightness(int x, int y, int z, bool propagate) {
    if (x < -Level::MAX_LEVEL_SIZE || z < -Level::MAX_LEVEL_SIZE ||
        x >= Level::MAX_LEVEL_SIZE || z > Level::MAX_LEVEL_SIZE) {
        return Level::MAX_BRIGHTNESS;
    }

    if (propagate) {
        int id = getTile(x, y, z);
        switch (id) {
            case Tile::stoneSlabHalf_Id:
            case Tile::woodSlabHalf_Id:
            case Tile::farmland_Id:
            case Tile::stairs_stone_Id:
            case Tile::stairs_wood_Id: {
                int br = getRawBrightness(x, y + 1, z, false);
                int br1 = getRawBrightness(x + 1, y, z, false);
                int br2 = getRawBrightness(x - 1, y, z, false);
                int br3 = getRawBrightness(x, y, z + 1, false);
                int br4 = getRawBrightness(x, y, z - 1, false);
                if (br1 > br) br = br1;
                if (br2 > br) br = br2;
                if (br3 > br) br = br3;
                if (br4 > br) br = br4;
                return br;
            } break;
        }
    }

    if (y < 0) return 0;
    if (y >= Level::maxBuildHeight) {
        int br = Level::MAX_BRIGHTNESS - level->skyDarken;
        if (br < 0) br = 0;
        return br;
    }

    int lx = (x >> 4) - xc1;
    int lz = (z >> 4) - zc1;

    if (lx >= 0 && lx < m_width && lz >= 0 && lz < m_height) {
        LevelChunk* lc = m_chunks[lx][lz];
        if (lc != nullptr) {
            return lc->getRawBrightness(x & 15, y, z & 15, level->skyDarken);
        }
    }
    return 0;
}

Material* Region::getMaterial(int x, int y, int z) {
    int t = getTile(x, y, z);
    if (t == 0) return Material::air;
    return Tile::tiles[t]->material;
}

BiomeSource* Region::getBiomeSource() { return level->getBiomeSource(); }
Biome* Region::getBiome(int x, int z) { return level->getBiome(x, z); }

bool Region::isSolidRenderTile(int x, int y, int z) {
    Tile* tile = Tile::tiles[getTile(x, y, z)];
    if (tile == nullptr) return false;
    if (tile->id == Tile::leaves_Id) {
        int axo[6] = {1, -1, 0, 0, 0, 0};
        int ayo[6] = {0, 0, 1, -1, 0, 0};
        int azo[6] = {0, 0, 0, 0, 1, -1};
        for (int i = 0; i < 6; i++) {
            int t = getTile(x + axo[i], y + ayo[i], z + azo[i]);
            if ((t != Tile::leaves_Id) && ((Tile::tiles[t] == nullptr) || !Tile::tiles[t]->isSolidRender())) {
                return false;
            }
        }
        return true;
    }
    return tile->isSolidRender();
}

bool Region::isSolidBlockingTile(int x, int y, int z) {
    Tile* tile = Tile::tiles[getTile(x, y, z)];
    if (tile == nullptr) return false;
    return tile->material->blocksMotion() && tile->isCubeShaped();
}

bool Region::isTopSolidBlocking(int x, int y, int z) {
    Tile* tile = Tile::tiles[getTile(x, y, z)];
    return level->isTopSolidBlocking(tile, getData(x, y, z));
}

bool Region::isEmptyTile(int x, int y, int z) {
    Tile* tile = Tile::tiles[getTile(x, y, z)];
    return (tile == nullptr);
}

int Region::getMaxBuildHeight() { return Level::maxBuildHeight; }

int Region::getDirectSignal(int x, int y, int z, int dir) {
    int t = getTile(x, y, z);
    if (t == 0) return Redstone::SIGNAL_NONE;
    return Tile::tiles[t]->getDirectSignal(this, x, y, z, dir);
}
