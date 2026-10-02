#include "CompressedTileStorage.h"

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>

#include "java/InputOutputStream/DataInputStream.h"
#include "java/InputOutputStream/DataOutputStream.h"

unsigned char CompressedTileStorage::compressBuffer[32768 + 256];
std::recursive_mutex CompressedTileStorage::cs_write;

CompressedTileStorage::CompressedTileStorage() {
    std::memset(m_blocks, 0, sizeof(m_blocks));
    unpackedCache = m_blocks;
    isDirty = false;
    allocatedSize = 32768;
}

CompressedTileStorage::CompressedTileStorage(CompressedTileStorage* copyFrom) {
    if (copyFrom) {
        std::memcpy(m_blocks, copyFrom->m_blocks, sizeof(m_blocks));
    } else {
        std::memset(m_blocks, 0, sizeof(m_blocks));
    }
    unpackedCache = m_blocks;
    isDirty = false;
    allocatedSize = 32768;
}

CompressedTileStorage::CompressedTileStorage(std::vector<uint8_t>& initFrom, unsigned int initOffset) {
    allocatedSize = 32768;
    unpackedCache = m_blocks;
    isDirty = false;
    if (!initFrom.empty() && (initFrom.size() >= initOffset + 32768)) {
        std::memcpy(m_blocks, initFrom.data() + initOffset, 32768);
    } else {
        std::memset(m_blocks, 0, sizeof(m_blocks));
    }
}

CompressedTileStorage::CompressedTileStorage(bool isEmpty) {
    std::memset(m_blocks, 0, sizeof(m_blocks));
    unpackedCache = m_blocks;
    isDirty = false;
    allocatedSize = 32768;
}

CompressedTileStorage::~CompressedTileStorage() {}

bool CompressedTileStorage::isCompressed() {
    return false;
}

bool CompressedTileStorage::isRenderChunkEmpty(int y) {
    int yStart = y;
    for (int x = 0; x < 16; x++) {
        int xShift = x << 11;
        for (int z = 0; z < 16; z++) {
            int base = xShift | (z << 7);
            const uint64_t* p64 = reinterpret_cast<const uint64_t*>(&m_blocks[base | yStart]);
            if (p64[0] != 0 || p64[1] != 0) {
                return false;
            }
        }
    }
    return true;
}

bool CompressedTileStorage::isSameAs(CompressedTileStorage* other) {
    if (!other) return false;
    return std::memcmp(m_blocks, other->m_blocks, sizeof(m_blocks)) == 0;
}

void CompressedTileStorage::setData(std::vector<uint8_t>& dataIn, unsigned int inOffset) {
    if (dataIn.size() >= inOffset + 32768) {
        std::memcpy(m_blocks, dataIn.data() + inOffset, 32768);
    }
}

void CompressedTileStorage::getData(std::vector<uint8_t>& retArray, unsigned int retOffset) {
    if (retArray.size() < 32768 + retOffset) {
        retArray.resize(32768 + retOffset);
    }
    std::memcpy(&retArray[retOffset], m_blocks, 32768);
}

int CompressedTileStorage::setDataRegion(std::vector<uint8_t>& dataIn, int x0,
                                         int y0, int z0, int x1, int y1, int z1,
                                         int offset,
                                         tileUpdatedCallback callback,
                                         void* param, int yparam) {
    unsigned char* pucIn = &dataIn.data()[offset];

    if (callback) {
        for (int x = x0; x < x1; x++) {
            int xShift = x << 11;
            for (int z = z0; z < z1; z++) {
                int zShift = z << 7;
                int base = xShift | zShift;
                for (int y = y0; y < y1; y++) {
                    uint8_t val = *pucIn++;
                    if (m_blocks[base | y] != val) {
                        m_blocks[base | y] = val;
                        callback(x, y, z, param, yparam);
                    }
                }
            }
        }
    } else {
        for (int x = x0; x < x1; x++) {
            int xShift = x << 11;
            for (int z = z0; z < z1; z++) {
                int zShift = z << 7;
                int base = xShift | zShift;
                for (int y = y0; y < y1; y++) {
                    m_blocks[base | y] = *pucIn++;
                }
            }
        }
    }
    return static_cast<int>(pucIn - &dataIn.data()[offset]);
}

bool CompressedTileStorage::testSetDataRegion(std::vector<uint8_t>& dataIn,
                                              int x0, int y0, int z0, int x1,
                                              int y1, int z1, int offset) {
    unsigned char* pucIn = &dataIn.data()[offset];
    for (int x = x0; x < x1; x++) {
        int xShift = x << 11;
        for (int z = z0; z < z1; z++) {
            int zShift = z << 7;
            int base = xShift | zShift;
            for (int y = y0; y < y1; y++) {
                if (m_blocks[base | y] != *pucIn++) {
                    return true;
                }
            }
        }
    }
    return false;
}

int CompressedTileStorage::getDataRegion(std::vector<uint8_t>& dataInOut,
                                         int x0, int y0, int z0, int x1, int y1,
                                         int z1, int offset) {
    unsigned char* pucOut = &dataInOut.data()[offset];
    for (int x = x0; x < x1; x++) {
        int xShift = x << 11;
        for (int z = z0; z < z1; z++) {
            int zShift = z << 7;
            int base = xShift | zShift;
            for (int y = y0; y < y1; y++) {
                *pucOut++ = m_blocks[base | y];
            }
        }
    }
    return static_cast<int>(pucOut - &dataInOut.data()[offset]);
}

void CompressedTileStorage::staticCtor() {}

void CompressedTileStorage::queueForDelete(unsigned char* data) {
    if (data) std::free(data);
}

void CompressedTileStorage::tick() {}

void CompressedTileStorage::compress(int upgradeBlock) {}

int CompressedTileStorage::getAllocatedSize(int* count0, int* count1,
                                            int* count2, int* count4,
                                            int* count8) {
    *count0 = 0; *count1 = 0; *count2 = 0; *count4 = 0;
    *count8 = 512;
    return 32768;
}

int CompressedTileStorage::getHighestNonEmptyY() {
    for (int y = 127; y >= 0; --y) {
        for (int x = 0; x < 16; ++x) {
            int xShift = x << 11;
            for (int z = 0; z < 16; ++z) {
                if (m_blocks[xShift | (z << 7) | y] != 0) {
                    return y + 1;
                }
            }
        }
    }
    return 0;
}

void CompressedTileStorage::write(DataOutputStream* dos) {
    dos->writeInt(32768);
    std::vector<uint8_t> wrapper(m_blocks, m_blocks + 32768);
    dos->write(wrapper);
}

// LECTURA RETROCOMPATIBLE CON MUNDOS GUARDADOS EN FORMATO 4J
void CompressedTileStorage::read(DataInputStream* dis) {
    int size = dis->readInt();
    if (size <= 0) {
        std::memset(m_blocks, 0, sizeof(m_blocks));
        return;
    }

    if (size == 32768) {
        std::vector<uint8_t> wrapper(32768);
        dis->readFully(wrapper);
        std::memcpy(m_blocks, wrapper.data(), 32768);
    } else {
        std::vector<uint8_t> raw(size);
        dis->readFully(raw);

        unsigned short* blockIndices = reinterpret_cast<unsigned short*>(raw.data());
        unsigned char* data = raw.data() + 1024;

        for (int i = 0; i < 512; i++) {
            int indexType = blockIndices[i] & INDEX_TYPE_MASK;
            if (indexType == INDEX_TYPE_0_OR_8_BIT) {
                if (blockIndices[i] & INDEX_TYPE_0_BIT_FLAG) {
                    uint8_t val = (blockIndices[i] >> INDEX_TILE_SHIFT) & INDEX_TILE_MASK;
                    for (int j = 0; j < 64; j++) {
                        m_blocks[getIndex(i, j)] = val;
                    }
                } else {
                    unsigned char* packed = data + ((blockIndices[i] >> INDEX_OFFSET_SHIFT) & INDEX_OFFSET_MASK);
                    for (int j = 0; j < 64; j++) {
                        m_blocks[getIndex(i, j)] = packed[j];
                    }
                }
            } else {
                int bitspertile = 1 << indexType;
                int tiletypecount = 1 << bitspertile;
                int tiletypemask = tiletypecount - 1;
                int indexshift = 3 - indexType;
                int indexmask_bits = 7 >> indexType;
                int indexmask_bytes = 62 >> indexshift;

                unsigned char* tile_types = data + ((blockIndices[i] >> INDEX_OFFSET_SHIFT) & INDEX_OFFSET_MASK);
                unsigned char* packed = tile_types + tiletypecount;

                for (int j = 0; j < 64; j++) {
                    int idx = (j >> indexshift) & indexmask_bytes;
                    int bit = (j & indexmask_bits) * bitspertile;
                    m_blocks[getIndex(i, j)] = tile_types[(packed[idx] >> bit) & tiletypemask];
                }
            }
        }
    }
}

void CompressedTileStorage::reverseIndices(unsigned char* indices) {}