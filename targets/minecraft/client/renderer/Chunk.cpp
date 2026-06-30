#include "Chunk.h"
#include "minecraft/world/Icon.h"  

#include <string.h>
#include <cmath>

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "LevelRenderer.h"
#include "TileRenderer.h"
#include "minecraft/client/renderer/Tesselator.h"
#include "minecraft/client/renderer/culling/Culler.h"
#include "minecraft/client/renderer/tileentity/TileEntityRenderDispatcher.h"
#include "minecraft/world/entity/Entity.h"
#include "minecraft/world/level/Level.h"
#include "minecraft/world/level/LevelSource.h"
#include "minecraft/world/level/Region.h"
#include "minecraft/world/level/chunk/LevelChunk.h"
#include "minecraft/world/level/tile/Tile.h"
#include "minecraft/world/level/tile/entity/TileEntity.h"
#include "minecraft/world/phys/AABB.h"
#include "platform/renderer/renderer.h"
#include "platform/stubs.h"
#include "util/FrameProfiler.h"



#define GREEDY_MESH_TILING 0

int Chunk::updates = 0;

#if defined(_LARGE_WORLDS)
thread_local uint8_t* Chunk::m_tlsTileIds = nullptr;

void Chunk::CreateNewThreadStorage() {
    m_tlsTileIds = new unsigned char[16 * 16 * Level::maxBuildHeight];
}

void Chunk::ReleaseThreadStorage() { delete m_tlsTileIds; }

uint8_t* Chunk::GetTileIdsStorage() { return m_tlsTileIds; }
#else
// 4J Stu - Don't want this when multi-threaded
Tesselator* Chunk::t = Tesselator::getInstance();
#endif
LevelRenderer* Chunk::levelRenderer;

void Chunk::reconcileRenderableTileEntities(
    const std::vector<std::shared_ptr<TileEntity> >& renderableTileEntities) {
    int key =
        levelRenderer->getGlobalIndexForChunk(this->x, this->y, this->z, level);
    auto it = globalRenderableTileEntities->find(key);
    if (!renderableTileEntities.empty()) {
        std::unordered_set<TileEntity*> currentRenderableTileEntitySet;
        currentRenderableTileEntitySet.reserve(renderableTileEntities.size());
        for (size_t i = 0; i < renderableTileEntities.size(); i++) {
            currentRenderableTileEntitySet.insert(
                renderableTileEntities[i].get());
        }

        if (it != globalRenderableTileEntities->end()) {
            LevelRenderer::RenderableTileEntityBucket& existingBucket =
                it->second;

            for (auto it2 = existingBucket.tiles.begin();
                 it2 != existingBucket.tiles.end(); it2++) {
                TileEntity* tileEntity = (*it2).get();
                if (currentRenderableTileEntitySet.find(tileEntity) ==
                    currentRenderableTileEntitySet.end()) {
                    (*it2)->setRenderRemoveStage(
                        TileEntity::e_RenderRemoveStageFlaggedAtChunk);
                    levelRenderer->queueRenderableTileEntityForRemoval_Locked(
                        key, tileEntity);
                } else {
                    (*it2)->setRenderRemoveStage(
                        TileEntity::e_RenderRemoveStageKeep);
                }
            }

            for (size_t i = 0; i < renderableTileEntities.size(); i++) {
                renderableTileEntities[i]->setRenderRemoveStage(
                    TileEntity::e_RenderRemoveStageKeep);
                if (existingBucket.indexByTile.find(
                        renderableTileEntities[i].get()) ==
                    existingBucket.indexByTile.end()) {
                    levelRenderer->addRenderableTileEntity_Locked(
                        key, renderableTileEntities[i]);
                }
            }
        } else {
            for (size_t i = 0; i < renderableTileEntities.size(); i++) {
                renderableTileEntities[i]->setRenderRemoveStage(
                    TileEntity::e_RenderRemoveStageKeep);
                levelRenderer->addRenderableTileEntity_Locked(
                    key, renderableTileEntities[i]);
            }
        }
    } else if (it != globalRenderableTileEntities->end()) {
        for (auto it2 = it->second.tiles.begin(); it2 != it->second.tiles.end();
             it2++) {
            (*it2)->setRenderRemoveStage(
                TileEntity::e_RenderRemoveStageFlaggedAtChunk);
            levelRenderer->queueRenderableTileEntityForRemoval_Locked(
                key, (*it2).get());
        }
    }
}

// TODO - 4J see how input entity vector is set up and decide what way is best
// to pass this to the function
Chunk::Chunk(Level* level, LevelRenderer::rteMap& globalRenderableTileEntities,
             std::shared_mutex& globalRenderableTileEntities_cs, int x, int y, int z,
             ClipChunk* clipChunk)
    : globalRenderableTileEntities(&globalRenderableTileEntities),
      globalRenderableTileEntities_cs(&globalRenderableTileEntities_cs) {
    clipChunk->visible = false;
    const double g = 6;
    bb = AABB(-g, -g, -g, XZSIZE + g, SIZE + g, XZSIZE + g);
    id = 0;

    this->level = level;
    // this->globalRenderableTileEntities = globalRenderableTileEntities;

    assigned = false;
    this->clipChunk = clipChunk;
    setPos(x, y, z);
}

void Chunk::setPos(int x, int y, int z) {
    // El wrapper público se encarga del LOCK
    std::unique_lock<std::shared_mutex> lock(levelRenderer->m_csDirtyChunks);
    setPos_Internal(x, y, z);
}

void Chunk::setPos_Internal(int x, int y, int z) {
    // ESTA FUNCIÓN NO BLOQUEA NADA.
    if (assigned && (x == this->x && y == this->y && z == this->z)) return;

    // Llamamos al reset interno (sin lock)
    reset_Internal();

    this->x = x;
    this->y = y;
    this->z = z;
    xm = x + XZSIZE / 2;
    ym = y + SIZE / 2;
    zm = z + XZSIZE / 2;
    clipChunk->xm = xm;
    clipChunk->ym = ym;
    clipChunk->zm = zm;

    clipChunk->globalIdx = LevelRenderer::getGlobalIndexForChunk(x, y, z, level);
#ifdef OCCLUSION_MODE_BFS
    levelRenderer->setGlobalChunkConnectivity(clipChunk->globalIdx, ~0ULL);
#endif

    xRenderOffs = x;
    yRenderOffs = y;
    zRenderOffs = z;
    xRender = 0;
    yRender = 0;
    zRender = 0;

    clipChunk->aabb[0] = bb.x0 + x;
    clipChunk->aabb[1] = bb.y0 + y;
    clipChunk->aabb[2] = bb.z0 + z;
    clipChunk->aabb[3] = bb.x1 + x;
    clipChunk->aabb[4] = bb.y1 + y;
    clipChunk->aabb[5] = bb.z1 + z;

    assigned = true;

    // El refCount y los flags son ATÓMICOS, no necesitan lock adicional 
    // pero como ya estamos dentro del lock de m_csDirtyChunks en resortChunks, es seguro.
    unsigned char refCount = levelRenderer->incGlobalChunkRefCount(x, y, z, level);
    if (refCount == 1) {
        levelRenderer->setGlobalChunkFlag(x, y, z, level, LevelRenderer::CHUNK_FLAG_DIRTY);
    }
}

void Chunk::translateToPos() {
    glTranslatef((float)xRenderOffs, (float)yRenderOffs, (float)zRenderOffs);
}

Chunk::Chunk() {}

void Chunk::makeCopyForRebuild(Chunk* source) {
    this->level = source->level;
    this->x = source->x;
    this->y = source->y;
    this->z = source->z;
    this->xRender = source->xRender;
    this->yRender = source->yRender;
    this->zRender = source->zRender;
    this->xRenderOffs = source->xRenderOffs;
    this->yRenderOffs = source->yRenderOffs;
    this->zRenderOffs = source->zRenderOffs;
    this->xm = source->xm;
    this->ym = source->ym;
    this->zm = source->zm;
    this->bb = source->bb;
    this->clipChunk = nullptr;
    this->id = source->id;
    this->globalRenderableTileEntities = source->globalRenderableTileEntities;
    this->globalRenderableTileEntities_cs =
        source->globalRenderableTileEntities_cs;
}


struct FaceInfo {
    uint8_t tileId = 0;
    Icon* texture = nullptr;
    int lightColor = 0;
    int tileColor = 0;
    bool merged = false;

    bool equals(const FaceInfo& other) const {
        return tileId == other.tileId &&
               texture == other.texture &&
               lightColor == other.lightColor &&
               tileColor == other.tileColor &&
               tileId != 0;
    }
};

void Chunk::rebuild() {
    // ============================================================================
    // 1. LUTs DINÁMICAS (Se llenan basadas en las propiedades reales de los Tiles)
    // ============================================================================
    static bool isSolidLUT[256];
    static bool isGreedySafeLUT[256];
    static bool isInitialized = false;
    if (!isInitialized) {
        memset(isSolidLUT, 0, 256);
        memset(isGreedySafeLUT, 0, 256); // Ahora empezamos con TODO en false

        for (int i = 0; i < 256; i++) {
            Tile* tile = Tile::tiles[i];
            if (tile) {
                // Un bloque es Sólido si llena el cubo y no es transparente
                // Ajusta 'isSolidRender' o 'isOpaque' según los métodos de tu clase Tile
                if (tile->isSolidRender()) {
                    isSolidLUT[i] = true;
                }

                // Un bloque es Greedy Safe SOLO si es un cubo sólido perfecto
                // Las losas, escaleras y hierba NO deben ser Greedy Safe.
                // Aquí definimos que solo es safe si es sólido Y no es uno de los bloques complejos.
                if (tile->isSolidRender()) {
                    int id = tile->id;
                    // Lista de bloques que, aunque sean "sólidos", tienen geometría especial
                    bool isComplex = (id == 2 || id == 17 || id == 18 || id == 23 || 
                                      id == 61 || id == 62 || id == 86 || id == 91 || 
                                      id == 155 || id == 158 || id == 161 || id == 162 || id == 170);
                    
                    if (!isComplex) {
                        isGreedySafeLUT[i] = true;
                    }
                }
            }
        }
        // El bloque 255 suele ser el bloque de "ocultar" o aire sólido
        isSolidLUT[255] = true; 
        isInitialized = true;
    }

#if defined(_LARGE_WORLDS)
    Tesselator* t = Tesselator::getInstance();
#else
    Chunk::t = Tesselator::getInstance();
#endif
    updates++;

    int x0 = x, y0 = y, z0 = z;
    int x1 = x + XZSIZE, y1 = y + SIZE, z1 = z + XZSIZE;

    LevelChunk::touchedSky = false;
    std::vector<std::shared_ptr<TileEntity>> renderableTileEntities;
    int r = 1;

    int lists = levelRenderer->getGlobalIndexForChunk(this->x, this->y, this->z, level) * 2;
    lists += levelRenderer->chunkLists;

    thread_local unsigned char tileIds[16 * 16 * Level::maxBuildHeight];
    std::vector<uint8_t> tileArray(65536);
    level->getChunkAt(x, z)->getBlockData(tileArray);
    memcpy(tileIds, tileArray.data(), 16 * 16 * Level::maxBuildHeight);

    Region* region = new Region(level, x0 - r, y0 - r, z0 - r, x1 + r, y1 + r, z1 + r, r);
    TileRenderer* tileRenderer = new TileRenderer(region, this->x, this->y, this->z, tileIds);

    int offsetBaseY[Level::maxBuildHeight];
    int indexYBase[Level::maxBuildHeight];
    for (int yy = 0; yy < Level::maxBuildHeight; yy++) {
        int idxY = yy;
        int off = 0;
        if (idxY >= Level::COMPRESSED_CHUNK_SECTION_HEIGHT) {
            idxY -= Level::COMPRESSED_CHUNK_SECTION_HEIGHT;
            off = Level::COMPRESSED_CHUNK_SECTION_TILES;
        }
        indexYBase[yy] = idxY;
        offsetBaseY[yy] = off;
    }

    // ============================================================================
    // 5. FAST CULLING PREPASS (Ahora con LUT correcta)
    // ============================================================================
    bool empty = true;
    {
        FRAME_PROFILE_SCOPE(ChunkPrepass);
        for (int yy = y0; yy < y1; yy++) {
            int idxY = indexYBase[yy];
            int offset = offsetBaseY[yy];
            unsigned char* layerPtr = tileIds + offset;
            for (int zz = 0; zz < 16; zz++) {
                int zShift = zz << 7;
                for (int xx = 0; xx < 16; xx++) {
                    int xShift = xx << 11;
                    unsigned char tileId = layerPtr[xShift | zShift | idxY];
                    if (tileId > 0) empty = false;
                    if (yy == (Level::maxBuildHeight - 1) || (xx == 0) || (xx == 15) || (zz == 0) || (zz == 15)) continue;
                    if (!isSolidLUT[tileId]) continue;
                    if (!isSolidLUT[layerPtr[((xx - 1) << 11) | zShift | idxY]]) continue;
                    if (!isSolidLUT[layerPtr[((xx + 1) << 11) | zShift | idxY]]) continue;
                    if (!isSolidLUT[layerPtr[xShift | ((zz - 1) << 7) | idxY]]) continue;
                    if (!isSolidLUT[layerPtr[xShift | ((zz + 1) << 7) | idxY]]) continue;
                    if (yy > 0 && !isSolidLUT[tileIds[offsetBaseY[yy-1] + (xShift | zShift | indexYBase[yy-1])]]) continue;
                    if (!isSolidLUT[tileIds[offsetBaseY[yy+1] + (xShift | zShift | indexYBase[yy+1])]]) continue;
                    layerPtr[xShift | zShift | idxY] = 0xff;
                }
            }
        }
    }

    if (empty) {
        for (int currentLayer = 0; currentLayer < 2; currentLayer++) {
            levelRenderer->setGlobalChunkFlag(this->x, this->y, this->z, level, LevelRenderer::CHUNK_FLAG_EMPTY0, currentLayer);
            PlatformRenderer.CBuffClear(lists + currentLayer);
        }
        delete region; delete tileRenderer;
        return;
    }

    Tesselator::Bounds bounds;
    {
        float g = 6.0f;
        bounds.boundingBox[0] = -g; bounds.boundingBox[1] = -g; bounds.boundingBox[2] = -g;
        bounds.boundingBox[3] = XZSIZE + g; bounds.boundingBox[4] = SIZE + g; bounds.boundingBox[5] = XZSIZE + g;
    }

    thread_local FaceInfo gridBufferY[256];
    thread_local std::vector<FaceInfo> gridBufferZ;
    thread_local std::vector<FaceInfo> gridBufferX;
    gridBufferZ.assign(16 * (y1 - y0), FaceInfo());
    gridBufferX.assign(16 * (y1 - y0), FaceInfo());

    for (int currentLayer = 0; currentLayer < 2; currentLayer++) {
        bool renderNextLayer = false;
        bool rendered = false;
        bool started = false;

        auto startTesselatorIfNeeded = [&]() {
            if (!started) {
                started = true;
                glNewList(lists + currentLayer, GL_COMPILE);
                glDepthMask(true);
                t->useCompactVertices(false); 
                t->begin();
                t->offset((float)(-this->x), (float)(-this->y), (float)(-this->z));
            }
        };

        FRAME_PROFILE_SCOPE(ChunkGreedyMeshing);

        for (int face = 0; face < 6; face++) {
            int axis = (face == 0 || face == 1) ? 0 : ((face == 2 || face == 3) ? 1 : 2);
            if (axis == 0) {
                int yDir = (face == 1) ? 1 : -1;
                for (int y = y0; y < y1; y++) {
                    FaceInfo* grid = gridBufferY;
                    bool hasData = false;
                    int idxY = indexYBase[y], offset = offsetBaseY[y];
                    unsigned char* layerPtr = tileIds + offset;
                    for (int z = 0; z < 16; z++) {
                        int zShift = z << 7;
                        for (int x = 0; x < 16; x++) {
                            int xShift = x << 11;
                            unsigned char tileId = layerPtr[xShift | zShift | idxY];
                            if (tileId == 0 || tileId == 0xff || !isGreedySafeLUT[tileId]) continue;
                            Tile* tile = Tile::tiles[tileId];
                            if (!tile || tile->getRenderLayer() != currentLayer) continue;
                            int ny = y + yDir;
                            if (ny < 0) ny = 0; if (ny >= Level::maxBuildHeight) ny = Level::maxBuildHeight - 1;
                            bool renderFace = false;
                            if (ny >= y0 && ny < y1) {
                                unsigned char neighborId = tileIds[offsetBaseY[ny] + (xShift | zShift | indexYBase[ny])];
                                if (neighborId == 0 || neighborId == 0xff || !isSolidLUT[neighborId]) renderFace = true;
                            } else renderFace = tile->shouldRenderFace(region, x0 + x, ny, z0 + z, face);
                            if (!renderFace) continue;
                            int gridIdx = z * 16 + x;
                            grid[gridIdx].tileId = tileId;
                            grid[gridIdx].texture = tileRenderer->getTexture(tile, region, x0 + x, y, z0 + z, face);
                            grid[gridIdx].lightColor = tileRenderer->getLightColor(tile, region, x0 + x, ny, z0 + z);
                            grid[gridIdx].tileColor = tile->getColor(region, x0 + x, y, z0 + z);
                            grid[gridIdx].merged = false;
                            hasData = true;
                        }
                    }
                    if (!hasData) continue;
                    for (int v = 0; v < 16; v++) {
                        for (int u = 0; u < 16; u++) {
                            int idx = v * 16 + u;
                            if (grid[idx].tileId == 0 || grid[idx].merged) continue;
                            FaceInfo current = grid[idx];
                            int width = 1;
                            while (u + width < 16 && !grid[v * 16 + (u + width)].merged && grid[v * 16 + (u + width)].equals(current)) width++;
                            int height = 1;
                            bool ok = true;
                            while (v + height < 16) {
                                for (int cu = u; cu < u + width; cu++) {
                                    if (grid[(v + height) * 16 + cu].merged || !grid[(v + height) * 16 + cu].equals(current)) {
                                        ok = false; break;
                                    }
                                }
                                if (!ok) break;
                                height++;
                            }
                            for (int cv = v; cv < v + height; cv++) for (int cu = u; cu < u + width; cu++) grid[cv * 16 + cu].merged = true;
                            startTesselatorIfNeeded();
                            rendered = true;
                            float mult = (face == 0) ? 0.5f : 1.0f;
                            t->color(((current.tileColor >> 16) & 0xff) / 255.0f * mult, ((current.tileColor >> 8) & 0xff) / 255.0f * mult, ((current.tileColor) & 0xff) / 255.0f * mult);
                            float u0 = current.texture ? current.texture->getU0() : 0.0f, v0 = current.texture ? current.texture->getV0() : 0.0f;
                            float u1 = current.texture ? current.texture->getU1() : 0.0f, v1 = current.texture ? current.texture->getV1() : 0.0f;
                            float cellW = u1 - u0, cellH = v1 - v0, uEnd = u0 + (width * cellW), vEnd = v0 + (height * cellH);
                            float packU = std::floor(u0 * 1024.0f + 0.5f) + 1.0f, packV = std::floor(v0 * 1024.0f + 0.5f) + 1.0f;
                            float oU = packU * 10.0f, oV = packV * 10.0f, pU0 = u0 + oU, pV0 = v0 + oV, pUEnd = uEnd + oU, pVEnd = vEnd + oV;
                            t->tex2(current.lightColor);
                            float cx = (float)(x0 + u), cz = (float)(z0 + v), cy = (float)y, cw = (float)width, ch = (float)height;
                            if (face == 0) {
                                t->vertexUV(cx, cy, cz + ch, pU0, pVEnd); t->vertexUV(cx, cy, cz, pU0, pV0);
                                t->vertexUV(cx + cw, cy, cz, pUEnd, pV0); t->vertexUV(cx + cw, cy, cz + ch, pUEnd, pVEnd);
                            } else {
                                t->vertexUV(cx + cw, cy + 1.0f, cz + ch, pUEnd, pVEnd); t->vertexUV(cx + cw, cy + 1.0f, cz, pUEnd, pV0);
                                t->vertexUV(cx, cy + 1.0f, cz, pU0, pV0); t->vertexUV(cx, cy + 1.0f, cz + ch, pU0, pVEnd);
                            }
                        }
                    }
                }
            } else if (axis == 1) {
                int zDir = (face == 3) ? 1 : -1;
                int heightY = y1 - y0;
                for (int z = 0; z < 16; z++) {
                    FaceInfo* grid = gridBufferZ.data();
                    bool hasData = false;
                    for (int y = 0; y < heightY; y++) {
                        int worldY = y0 + y, idxY = indexYBase[worldY], offset = offsetBaseY[worldY];
                        unsigned char* layerPtr = tileIds + offset;
                        int zShift = z << 7;
                        for (int x = 0; x < 16; x++) {
                            int xShift = x << 11;
                            unsigned char tileId = layerPtr[xShift | zShift | idxY];
                            if (tileId == 0 || tileId == 0xff || !isGreedySafeLUT[tileId]) continue;
                            Tile* tile = Tile::tiles[tileId];
                            if (!tile || tile->getRenderLayer() != currentLayer) continue;
                            int nz = z0 + z + zDir;
                            bool renderFace = false;
                            if (nz >= z0 && nz < z1) {
                                unsigned char neighborId = tileIds[offset + ((nz - z0) << 7 | xShift | idxY)];
                                if (neighborId == 0 || neighborId == 0xff || !isSolidLUT[neighborId]) renderFace = true;
                            } else renderFace = tile->shouldRenderFace(region, x0 + x, worldY, nz, face);
                            if (!renderFace) continue;
                            int gridIdx = y * 16 + x;
                            grid[gridIdx].tileId = tileId;
                            grid[gridIdx].texture = tileRenderer->getTexture(tile, region, x0 + x, worldY, z0 + z, face);
                            grid[gridIdx].lightColor = tileRenderer->getLightColor(tile, region, x0 + x, worldY, nz);
                            grid[gridIdx].tileColor = tile->getColor(region, x0 + x, worldY, z0 + z);
                            grid[gridIdx].merged = false;
                            hasData = true;
                        }
                    }
                    if (!hasData) continue;
                    for (int v = 0; v < heightY; v++) {
                        for (int u = 0; u < 16; u++) {
                            int idx = v * 16 + u;
                            if (grid[idx].tileId == 0 || grid[idx].merged) continue;
                            FaceInfo current = grid[idx];
                            int width = 1;
                            while (u + width < 16 && !grid[v * 16 + (u + width)].merged && grid[v * 16 + (u + width)].equals(current)) width++;
                            int height = 1;
                            bool ok = true;
                            while (v + height < heightY) {
                                for (int cu = u; cu < u + width; cu++) {
                                    if (grid[(v + height) * 16 + cu].merged || !grid[(v + height) * 16 + cu].equals(current)) {
                                        ok = false; break;
                                    }
                                }
                                if (!ok) break;
                                height++;
                            }
                            for (int cv = v; cv < v + height; cv++) for (int cu = u; cu < u + width; cu++) grid[cv * 16 + cu].merged = true;
                            startTesselatorIfNeeded();
                            rendered = true;
                            float mult = 0.8f;
                            t->color(((current.tileColor >> 16) & 0xff) / 255.0f * mult, ((current.tileColor >> 8) & 0xff) / 255.0f * mult, ((current.tileColor) & 0xff) / 255.0f * mult);
                            float u0 = current.texture ? current.texture->getU0() : 0.0f, v0 = current.texture ? current.texture->getV0() : 0.0f;
                            float u1 = current.texture ? current.texture->getU1() : 0.0f, v1 = current.texture ? current.texture->getV1() : 0.0f;
                            float cellW = u1 - u0, cellH = v1 - v0, uEnd = u0 + (width * cellW), vEnd = v0 + (height * cellH);
                            float packU = std::floor(u0 * 1024.0f + 0.5f) + 1.0f, packV = std::floor(v0 * 1024.0f + 0.5f) + 1.0f;
                            float oU = packU * 10.0f, oV = packV * 10.0f, pU0 = u0 + oU, pV0 = v0 + oV, pUEnd = uEnd + oU, pVEnd = vEnd + oV;
                            t->tex2(current.lightColor);
                            float cx = (float)(x0 + u), cy = (float)(y0 + v), cz = (float)(z0 + z), cw = (float)width, ch = (float)height;
                            if (face == 2) {
                                t->vertexUV(cx, cy + ch, cz, pUEnd, pV0); t->vertexUV(cx + cw, cy + ch, cz, pU0, pV0);
                                t->vertexUV(cx + cw, cy, cz, pU0, pVEnd); t->vertexUV(cx, cy, cz, pUEnd, pVEnd);
                            } else {
                                t->vertexUV(cx, cy + ch, cz + 1.0f, pU0, pV0); t->vertexUV(cx, cy, cz + 1.0f, pU0, pVEnd);
                                t->vertexUV(cx + cw, cy, cz + 1.0f, pUEnd, pVEnd); t->vertexUV(cx + cw, cy + ch, cz + 1.0f, pUEnd, pV0);
                            }
                        }
                    }
                }
            } else {
                int xDir = (face == 5) ? 1 : -1;
                int heightY = y1 - y0;
                for (int x = 0; x < 16; x++) {
                    FaceInfo* grid = gridBufferX.data();
                    bool hasData = false;
                    int xShift = x << 11;
                    for (int y = 0; y < heightY; y++) {
                        int worldY = y0 + y, idxY = indexYBase[worldY], offset = offsetBaseY[worldY];
                        unsigned char* layerPtr = tileIds + offset;
                        for (int z = 0; z < 16; z++) {
                            int zShift = z << 7;
                            unsigned char tileId = layerPtr[xShift | zShift | idxY];
                            if (tileId == 0 || tileId == 0xff || !isGreedySafeLUT[tileId]) continue;
                            Tile* tile = Tile::tiles[tileId];
                            if (!tile || tile->getRenderLayer() != currentLayer) continue;
                            int nx = x0 + x + xDir;
                            bool renderFace = false;
                            if (nx >= x0 && nx < x1) {
                                unsigned char neighborId = tileIds[offset + ((nx - x0) << 11 | zShift | idxY)];
                                if (neighborId == 0 || neighborId == 0xff || !isSolidLUT[neighborId]) renderFace = true;
                            } else renderFace = tile->shouldRenderFace(region, nx, worldY, z0 + z, face);
                            if (!renderFace) continue;
                            int gridIdx = y * 16 + z;
                            grid[gridIdx].tileId = tileId;
                            grid[gridIdx].texture = tileRenderer->getTexture(tile, region, x0 + x, worldY, z0 + z, face);
                            grid[gridIdx].lightColor = tileRenderer->getLightColor(tile, region, nx, worldY, z0 + z);
                            grid[gridIdx].tileColor = tile->getColor(region, x0 + x, worldY, z0 + z);
                            grid[gridIdx].merged = false;
                            hasData = true;
                        }
                    }
                    if (!hasData) continue;
                    for (int v = 0; v < heightY; v++) {
                        for (int u = 0; u < 16; u++) {
                            int idx = v * 16 + u;
                            if (grid[idx].tileId == 0 || grid[idx].merged) continue;
                            FaceInfo current = grid[idx];
                            int width = 1;
                            while (u + width < 16 && !grid[v * 16 + (u + width)].merged && grid[v * 16 + (u + width)].equals(current)) width++;
                            int height = 1;
                            bool ok = true;
                            while (v + height < heightY) {
                                for (int cu = u; cu < u + width; cu++) {
                                    if (grid[(v + height) * 16 + cu].merged || !grid[(v + height) * 16 + cu].equals(current)) {
                                        ok = false; break;
                                    }
                                }
                                if (!ok) break;
                                height++;
                            }
                            for (int cv = v; cv < v + height; cv++) for (int cu = u; cu < u + width; cu++) grid[cv * 16 + cu].merged = true;
                            startTesselatorIfNeeded();
                            rendered = true;
                            float mult = 0.6f;
                            t->color(((current.tileColor >> 16) & 0xff) / 255.0f * mult, ((current.tileColor >> 8) & 0xff) / 255.0f * mult, ((current.tileColor) & 0xff) / 255.0f * mult);
                            float u0 = current.texture ? current.texture->getU0() : 0.0f, v0 = current.texture ? current.texture->getV0() : 0.0f;
                            float u1 = current.texture ? current.texture->getU1() : 0.0f, v1 = current.texture ? current.texture->getV1() : 0.0f;
                            float cellW = u1 - u0, cellH = v1 - v0, uEnd = u0 + (width * cellW), vEnd = v0 + (height * cellH);
                            float packU = std::floor(u0 * 1024.0f + 0.5f) + 1.0f, packV = std::floor(v0 * 1024.0f + 0.5f) + 1.0f;
                            float oU = packU * 10.0f, oV = packV * 10.0f, pU0 = u0 + oU, pV0 = v0 + oV, pUEnd = uEnd + oU, pVEnd = vEnd + oV;
                            t->tex2(current.lightColor);
                            float cx = (float)(x0 + x), cy = (float)(y0 + v), cz = (float)(z0 + u), cw = (float)width, ch = (float)height;
                            if (face == 4) {
                                t->vertexUV(cx, cy + ch, cz + cw, pUEnd, pV0); t->vertexUV(cx, cy + ch, cz, pU0, pV0);
                                t->vertexUV(cx, cy, cz, pU0, pVEnd); t->vertexUV(cx, cy, cz + cw, pUEnd, pVEnd);
                            } else {
                                t->vertexUV(cx + 1.0f, cy, cz + cw, pU0, pVEnd); t->vertexUV(cx + 1.0f, cy, cz, pUEnd, pVEnd);
                                t->vertexUV(cx + 1.0f, cy + ch, cz, pUEnd, pV0); t->vertexUV(cx + 1.0f, cy + ch, cz + cw, pU0, pV0);
                            }
                        }
                    }
                }
            }
        }

        for (int z = z0; z < z1; z++) {
            int zIdx = z - z0;
            for (int x = x0; x < x1; x++) {
                int xIdx = x - x0;
                for (int y = y0; y < y1; y++) {
                    unsigned char tileId = tileIds[offsetBaseY[y] + ((xIdx << 11) | (zIdx << 7) | indexYBase[y])];
                    if (tileId == 0xff) continue;
                    if (tileId > 0) {
                        Tile* tile = Tile::tiles[tileId];
                        if (!tile) continue;
                        // CORRECCIÓN: Ahora isGreedySafeLUT es precisa
                        if (tile->isSolidRender() && isGreedySafeLUT[tileId]) continue;
                        if (!started) startTesselatorIfNeeded();
                        if (currentLayer == 0 && tile->isEntityTile()) {
                            std::shared_ptr<TileEntity> et = region->getTileEntity(x, y, z);
                            if (TileEntityRenderDispatcher::instance->hasRenderer(et)) renderableTileEntities.push_back(et);
                        }
                        int renderLayer = tile->getRenderLayer();
                        if (renderLayer != currentLayer) renderNextLayer = true;
                        else if (renderLayer == currentLayer) rendered |= tileRenderer->tesselateInWorld(tile, x, y, z);
                    }
                }
            }
        }

        if (started) {
            t->end();
            bounds.addBounds(t->bounds);
            glEndList();
            t->useCompactVertices(false);
            t->offset(0, 0, 0);
        }
        if (rendered) levelRenderer->clearGlobalChunkFlag(this->x, this->y, this->z, level, LevelRenderer::CHUNK_FLAG_EMPTY0, currentLayer);
        else {
            levelRenderer->setGlobalChunkFlag(this->x, this->y, this->z, level, LevelRenderer::CHUNK_FLAG_EMPTY0, currentLayer);
            PlatformRenderer.CBuffClear(lists + currentLayer);
        }
        if ((currentLayer == 0) && (!renderNextLayer)) {
            levelRenderer->setGlobalChunkFlag(this->x, this->y, this->z, level, LevelRenderer::CHUNK_FLAG_EMPTY1);
            PlatformRenderer.CBuffClear(lists + 1);
            break;
        }
    }

    bb = {bounds.boundingBox[0], bounds.boundingBox[1], bounds.boundingBox[2],
          bounds.boundingBox[3], bounds.boundingBox[4], bounds.boundingBox[5]};

#ifdef OCCLUSION_MODE_BFS
    uint64_t conn = computeConnectivity(tileIds);
    int globalIdx = levelRenderer->getGlobalIndexForChunk(this->x, this->y, this->z, level);
    levelRenderer->setGlobalChunkConnectivity(globalIdx, conn);
#endif

    delete tileRenderer;
    delete region;

    {
        std::unique_lock<std::shared_mutex> lock(*globalRenderableTileEntities_cs);
        reconcileRenderableTileEntities(renderableTileEntities);
    }

    if (LevelChunk::touchedSky) levelRenderer->clearGlobalChunkFlag(x, y, z, level, LevelRenderer::CHUNK_FLAG_NOTSKYLIT);
    else levelRenderer->setGlobalChunkFlag(x, y, z, level, LevelRenderer::CHUNK_FLAG_NOTSKYLIT);
    levelRenderer->setGlobalChunkFlag(x, y, z, level, LevelRenderer::CHUNK_FLAG_COMPILED);
}



float Chunk::distanceToSqr(std::shared_ptr<Entity> player) const {
    float xd = (float)(player->x - xm);
    float yd = (float)(player->y - ym);
    float zd = (float)(player->z - zm);
    return xd * xd + yd * yd + zd * zd;
}

float Chunk::squishedDistanceToSqr(std::shared_ptr<Entity> player) {
    float xd = (float)(player->x - xm);
    float yd = (float)(player->y - ym) * 2;
    float zd = (float)(player->z - zm);
    return xd * xd + yd * yd + zd * zd;
}

#ifdef OCCLUSION_MODE_BFS
uint64_t Chunk::computeConnectivity(const uint8_t* tileIds) {
    const int W = 16;
    const int H = 16;
    const int VOLUME = W * H * W;

    auto idx = [&](int x, int y, int z) -> int {
        return y * W * W + z * W + x;
    };

    auto isOpen = [&](int lx, int ly, int lz) -> bool {
        int worldY = this->y + ly;
        int offset = 0;
        int indexY = worldY;
        if (indexY >= Level::COMPRESSED_CHUNK_SECTION_HEIGHT) {
            indexY -= Level::COMPRESSED_CHUNK_SECTION_HEIGHT;
            offset = Level::COMPRESSED_CHUNK_SECTION_TILES;
        }

        uint8_t tileId = tileIds[offset + ((lx << 11) | (lz << 7) | indexY)];

        if (tileId == 0) return true;      // air
        if (tileId == 0xFF) return false;  // hidden tile (yeah)

        Tile* t = Tile::tiles[tileId];
        return (t == nullptr) || !t->isSolidRender();
    };

    uint8_t visited[6][512];
    memset(visited, 0, sizeof(visited));

    static const int FX[6] = {1, -1, 0, 0, 0, 0};
    static const int FY[6] = {0, 0, 1, -1, 0, 0};
    static const int FZ[6] = {0, 0, 0, 0, 1, -1};

    struct Cell {
        int8_t x, y, z;
    };
    static thread_local std::vector<Cell> queue;

    uint64_t result = 0;

    for (int entryFace = 0; entryFace < 6; entryFace++) {
        uint8_t* vis = visited[entryFace];
        queue.clear();
        int x0s, x1s, y0s, y1s, z0s, z1s;
        switch (entryFace) {
            case 0:
                x0s = W - 1;
                x1s = W - 1;
                y0s = 0;
                y1s = H - 1;
                z0s = 0;
                z1s = W - 1;
                break;  // +X
            case 1:
                x0s = 0;
                x1s = 0;
                y0s = 0;
                y1s = H - 1;
                z0s = 0;
                z1s = W - 1;
                break;  // -X
            case 2:
                x0s = 0;
                x1s = W - 1;
                y0s = H - 1;
                y1s = H - 1;
                z0s = 0;
                z1s = W - 1;
                break;  // +Y
            case 3:
                x0s = 0;
                x1s = W - 1;
                y0s = 0;
                y1s = 0;
                z0s = 0;
                z1s = W - 1;
                break;  // -Y
            case 4:
                x0s = 0;
                x1s = W - 1;
                y0s = 0;
                y1s = H - 1;
                z0s = W - 1;
                z1s = W - 1;
                break;  // +Z
            case 5:
                x0s = 0;
                x1s = W - 1;
                y0s = 0;
                y1s = H - 1;
                z0s = 0;
                z1s = 0;
                break;  // -Z
            default:
                continue;
        }

        for (int sy = y0s; sy <= y1s; sy++)
            for (int sz = z0s; sz <= z1s; sz++)
                for (int sx = x0s; sx <= x1s; sx++) {
                    if (!isOpen(sx, sy, sz)) continue;
                    int i = idx(sx, sy, sz);
                    if (vis[i >> 3] & (1 << (i & 7))) continue;
                    vis[i >> 3] |= (1 << (i & 7));
                    queue.push_back({(int8_t)sx, (int8_t)sy, (int8_t)sz});
                }

        for (int qi = 0; qi < (int)queue.size(); qi++) {
            Cell cur = queue[qi];

            for (int nb = 0; nb < 6; nb++) {
                int nx = cur.x + FX[nb];
                int ny = cur.y + FY[nb];
                int nz = cur.z + FZ[nb];

                // entry exit conn
                if (nx < 0 || nx >= W || ny < 0 || ny >= H || nz < 0 ||
                    nz >= W) {
                    // nb IS the exit face because FX,FY,FZ are aligned
                    result |= ((uint64_t)1 << (entryFace * 6 + nb));
                    continue;
                }

                if (!isOpen(nx, ny, nz)) continue;

                int i = idx(nx, ny, nz);
                if (vis[i >> 3] & (1 << (i & 7))) continue;
                vis[i >> 3] |= (1 << (i & 7));
                queue.push_back({(int8_t)nx, (int8_t)ny, (int8_t)nz});
            }
        }
    }

    return result;
}
#endif

void Chunk::reset() {
    // El wrapper público se encarga del LOCK
    std::unique_lock<std::shared_mutex> lock(levelRenderer->m_csDirtyChunks);
    
    int oldKey = -1;
    bool retireRenderableTileEntities = false;

    if (assigned) {
        oldKey = levelRenderer->getGlobalIndexForChunk(x, y, z, level);
        
        // Llamamos a la lógica interna que NO bloquea
        reset_Internal();
        
        // El refCount se maneja aquí para decidir si retiramos entidades
        unsigned char refCount = levelRenderer->decGlobalChunkRefCount(x, y, z, level);
        if (refCount == 0 && oldKey != -1) {
            retireRenderableTileEntities = true;
            int lists = oldKey * 2 + levelRenderer->chunkLists;
            for (int i = 0; i < 2; i++) {
                PlatformRenderer.CBuffClear(lists + i);
            }
            levelRenderer->setGlobalChunkFlags(x, y, z, level, 0);
        }
    }

    if (retireRenderableTileEntities) {
        levelRenderer->retireRenderableTileEntitiesForChunkKey(oldKey);
    }
    clipChunk->visible = false;
}

void Chunk::reset_Internal() {
    // ESTA FUNCIÓN NO BLOQUEA NADA. 
    // Se asume que quien la llama ya tiene el lock de m_csDirtyChunks.
    assigned = false;
}

void Chunk::_delete() {
    reset();
    level = nullptr;
}

int Chunk::getList(int layer) {
    if (!clipChunk->visible) return -1;

    int lists = levelRenderer->getGlobalIndexForChunk(x, y, z, level) * 2;
    lists += levelRenderer->chunkLists;

    bool empty = levelRenderer->getGlobalChunkFlag(
        x, y, z, level, LevelRenderer::CHUNK_FLAG_EMPTY0, layer);
    if (!empty) return lists + layer;
    return -1;
}

void Chunk::cull(Culler* culler) {
    if (clipChunk->visible) {
        clipChunk->visible = culler->isVisible(&bb);
    }
}

void Chunk::renderBB() {
    //	glCallList(lists + 2);	// 4J - removed - TODO put back in
}

bool Chunk::isEmpty() {
    if (!levelRenderer->getGlobalChunkFlag(x, y, z, level,
                                           LevelRenderer::CHUNK_FLAG_COMPILED))
        return false;
    return levelRenderer->getGlobalChunkFlag(
        x, y, z, level, LevelRenderer::CHUNK_FLAG_EMPTYBOTH);
}

void Chunk::setDirty() {
    // 4J - not used, but if this starts being used again then we'll need to
    // investigate how best to handle it.
    assert(0);
    levelRenderer->setGlobalChunkFlag(x, y, z, level,
                                      LevelRenderer::CHUNK_FLAG_DIRTY);
}

void Chunk::clearDirty() {
    levelRenderer->clearGlobalChunkFlag(x, y, z, level,
                                        LevelRenderer::CHUNK_FLAG_DIRTY);
#if defined(_CRITICAL_CHUNKS)
    levelRenderer->clearGlobalChunkFlag(x, y, z, level,
                                        LevelRenderer::CHUNK_FLAG_CRITICAL);
#endif
}

bool Chunk::emptyFlagSet(int layer) {
    return levelRenderer->getGlobalChunkFlag(
        x, y, z, level, LevelRenderer::CHUNK_FLAG_EMPTY0, layer);
}
