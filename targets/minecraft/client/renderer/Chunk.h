#pragma once
#include <stdint.h>

#include <format>
#include <memory>
#include <mutex>
#include <shared_mutex> // Agregado para compatibilidad con Fase B
#include <vector>

#include "LevelRenderer.h"
#include "Tesselator.h"
#include "minecraft/client/renderer/culling/AllowAllCuller.h"
#include "minecraft/world/phys/AABB.h"

class Level;
class TileEntity;
class Entity;
class Chunk;
class Culler;

class ClipChunk {
public:
    Chunk* chunk;
    int globalIdx;
    bool visible;
    float aabb[6];
    int xm, ym, zm;
};

class Chunk {
    // --- CRÍTICO: Permite que LevelRenderer acceda a las funciones _Internal ---
    friend class LevelRenderer;

private:
    static const int XZSIZE = LevelRenderer::CHUNK_XZSIZE;
    static const int SIZE = LevelRenderer::CHUNK_SIZE;

public:
    Level* level;
    static LevelRenderer* levelRenderer;

private:
#if !defined(_LARGE_WORLDS)
    static Tesselator* t;
#else
    static thread_local uint8_t* m_tlsTileIds;

public:
    static void CreateNewThreadStorage();
    static void ReleaseThreadStorage();
    static uint8_t* GetTileIdsStorage();
#endif

public:
    static int updates;

    int x, y, z;
    int xRender, yRender, zRender;
    int xRenderOffs, yRenderOffs, zRenderOffs;

    int xm, ym, zm;
    AABB bb;
    ClipChunk* clipChunk;
#ifdef OCCLUSION_MODE_BFS
    uint64_t computeConnectivity(const uint8_t* tileIds);
#endif
    int id;

private:
    LevelRenderer::rteMap* globalRenderableTileEntities;
    // CAMBIO: Ahora apunta a shared_mutex para permitir lecturas concurrentes en el rebuild
    std::shared_mutex* globalRenderableTileEntities_cs;
    bool assigned;

public:
    // Constructor actualizado a shared_mutex
    Chunk(Level* level, LevelRenderer::rteMap& globalRenderableTileEntities,
          std::shared_mutex& globalRenderableTileEntities_cs, int x, int y, int z,
          ClipChunk* clipChunk);
    Chunk();

    // --- FUNCIONES PÚBLICAS (Gestionan sus propios Locks) ---
    // Úsalas cuando llames a estas funciones desde fuera de un proceso coordinado
    void setPos(int x, int y, int z);
    void reset();
    void setDirty();
    void clearDirty();

private:
    // --- FUNCIONES INTERNAS (NO gestionan Locks) ---
    // Estas son las que usa LevelRenderer::resortChunks y updateDirtyChunks
    // para evitar deadlocks (ya que el lock se toma una sola vez al principio)
    void setPos_Internal(int x, int y, int z);
    void reset_Internal();
    void setDirty_Internal();
    void clearDirty_Internal();

    void translateToPos();
    void reconcileRenderableTileEntities(
        const std::vector<std::shared_ptr<TileEntity> >& renderableTileEntities);

public:
    void makeCopyForRebuild(Chunk* source);
    void rebuild();
    float distanceToSqr(std::shared_ptr<Entity> player) const;
    float squishedDistanceToSqr(std::shared_ptr<Entity> player);
    void _delete();

    int getList(int layer);
    void cull(Culler* culler);
    void renderBB();
    bool isEmpty();
    bool emptyFlagSet(int layer);
};