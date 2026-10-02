#pragma once

#include <cstdint>
#include <mutex>
#include <vector>
#include <cstring>

class DataInputStream;
class DataOutputStream;
class TileCompressData_SPU;

class CompressedTileStorage {
    friend class TileCompressData_SPU;

private:
    // BUFFER PLANO DIRECTO DE 32 KB (16x16x128 bloques)
    uint8_t m_blocks[32768];
    uint8_t* unpackedCache;
    bool isDirty;

public:
    int allocatedSize;

private:
    static const int INDEX_OFFSET_MASK = 0x7ffe;
    static const int INDEX_OFFSET_SHIFT = 1;
    static const int INDEX_TILE_MASK = 0x00ff;
    static const int INDEX_TILE_SHIFT = 8;
    static const int INDEX_TYPE_MASK = 0x0003;
    static const int INDEX_TYPE_1_BIT = 0x0000;
    static const int INDEX_TYPE_2_BIT = 0x0001;
    static const int INDEX_TYPE_4_BIT = 0x0002;
    static const int INDEX_TYPE_0_OR_8_BIT = 0x0003;
    static const int INDEX_TYPE_0_BIT_FLAG = 0x0004;

public:
    CompressedTileStorage();
    CompressedTileStorage(CompressedTileStorage* copyFrom);
    CompressedTileStorage(std::vector<uint8_t>& dataIn, unsigned int initOffset);
    CompressedTileStorage(bool isEmpty);
    ~CompressedTileStorage();

    bool isSameAs(CompressedTileStorage* other);
    bool isRenderChunkEmpty(int y);

    inline static int getIndex(int block, int tile) {
        int index = ((block & 0x180) << 6) | ((block & 0x060) << 4) | ((block & 0x01f) << 2);
        index |= ((tile & 0x30) << 7) | ((tile & 0x0c) << 5) | (tile & 0x03);
        return index;
    }

    inline static void getBlockAndTile(int* block, int* tile, int x, int y, int z) {
        *block = ((x & 0x0c) << 5) | ((z & 0x0c) << 3) | (y >> 2);
        *tile = ((x & 0x03) << 4) | ((z & 0x03) << 2) | (y & 0x03);
    }

    inline static void getBlock(int* block, int x, int y, int z) {
        *block = ((x & 0x0c) << 5) | ((z & 0x0c) << 3) | (y >> 2);
    }

    // ACCESO INLINE O(1) NATIVO
    inline int get(int x, int y, int z) const noexcept {
        return m_blocks[(x << 11) | (z << 7) | y];
    }

    inline void set(int x, int y, int z, int val) noexcept {
        m_blocks[(x << 11) | (z << 7) | y] = static_cast<uint8_t>(val);
    }

    inline uint8_t* getUnpackedBuffer() noexcept {
        return m_blocks;
    }

    inline void copyTo(uint8_t* dst) const noexcept {
        std::memcpy(dst, m_blocks, 32768);
    }

    inline void updateCache() noexcept {
        isDirty = false;
    }

    void setData(std::vector<uint8_t>& dataIn, unsigned int inOffset);
    void getData(std::vector<uint8_t>& retArray, unsigned int retOffset);

    typedef void (*tileUpdatedCallback)(int x, int y, int z, void* param, int yparam);
    int setDataRegion(std::vector<uint8_t>& dataIn, int x0, int y0, int z0, int x1, int y1, int z1, int offset, tileUpdatedCallback callback, void* param, int yparam);
    bool testSetDataRegion(std::vector<uint8_t>& dataIn, int x0, int y0, int z0, int x1, int y1, int z1, int offset);
    int getDataRegion(std::vector<uint8_t>& dataInOut, int x0, int y0, int z0, int x1, int y1, int z1, int offset);

    static void staticCtor();
    void compress(int upgradeBlock = -1);

    void queueForDelete(unsigned char* data);
    static void tick();
    static unsigned char compressBuffer[32768 + 256];
    static std::recursive_mutex cs_write;

    int getAllocatedSize(int* count0, int* count1, int* count2, int* count4, int* count8);
    int getHighestNonEmptyY();
    bool isCompressed();

    void write(DataOutputStream* dos);
    void read(DataInputStream* dis);
    void reverseIndices(unsigned char* indices);
};