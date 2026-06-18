#include "McRegionLevelStorage.h"
#include <stdint.h>
#include <format>
#include <vector>

#include "LevelData.h"
#include "java/File.h"
#include "minecraft/IGameServices.h"
#include "minecraft/util/Log.h"
#include "minecraft/world/level/chunk/storage/McRegionChunkStorage.h"
#include "minecraft/world/level/chunk/storage/RegionFileCache.h"
#include "minecraft/world/level/dimension/Dimension.h"
#include "minecraft/world/level/dimension/HellDimension.h"
#include "minecraft/world/level/dimension/TheEndDimension.h"
#include "minecraft/world/level/storage/ConsoleSaveFileIO/ConsoleSaveFile.h"
#include "minecraft/world/level/storage/ConsoleSaveFileIO/FileHeader.h"
#include "minecraft/world/level/storage/DirectoryLevelStorage.h"
#include "minecraft/world/level/storage/LevelStorage.h"

McRegionLevelStorage::McRegionLevelStorage(ConsoleSaveFile* saveFile, File dir,
                                           const std::string& levelName,
                                           bool createPlayerDir)
    : DirectoryLevelStorage(saveFile, dir, levelName, createPlayerDir) {
    RegionFileCache::clear();
}

McRegionLevelStorage::~McRegionLevelStorage() {
    RegionFileCache::clear();
    if (m_saveFile != nullptr) {
        delete m_saveFile;
        m_saveFile = nullptr;
    }
}

ChunkStorage* McRegionLevelStorage::createChunkStorage(Dimension* dimension) {
    // --- CASO 1: NETHER ---
    if (dynamic_cast<HellDimension*>(dimension) != nullptr) {
        if (gameServices().getResetNether()) {
#ifdef SPLIT_SAVES
            std::vector<FileEntry*> netherFiles = m_saveFile->getRegionFilesByDimension(1);
            if (!netherFiles.empty()) {
                uint32_t bytesWritten = 0;
                for (auto it = netherFiles.begin(); it != netherFiles.end(); ++it) {
                    m_saveFile->zeroFile(*it, (*it)->getFileSize(), &bytesWritten);
                }
            }
#else
            std::vector<FileEntry*> netherFiles = m_saveFile->getFilesWithPrefix(LevelStorage::NETHER_FOLDER);
            if (!netherFiles.empty()) {
                for (auto it = netherFiles.begin(); it != netherFiles.end(); ++it) {
                    m_saveFile->deleteFile(*it);
                }
            }
#endif
            resetNetherPlayerPositions();
        }
        return new McRegionChunkStorage(m_saveFile, LevelStorage::NETHER_FOLDER);
    }

    // --- CASO 2: THE END ---
    if (dynamic_cast<TheEndDimension*>(dimension) != nullptr) {
        int iSaveVersion = m_saveFile->getSaveVersion();

        if ((iSaveVersion != 0) && (iSaveVersion < SAVE_FILE_VERSION_NEW_END)) {
            Log::info("Loaded save version number is: %d, required to keep The End is: %d\n",
                      m_saveFile->getSaveVersion(), SAVE_FILE_VERSION_NEW_END);

            std::vector<FileEntry*> endFiles = m_saveFile->getFilesWithPrefix(LevelStorage::ENDER_FOLDER);

            if (!endFiles.empty()) {
                for (auto it = endFiles.begin(); it != endFiles.end(); ++it) {
                    m_saveFile->deleteFile(*it);
                }
            }
        }
        return new McRegionChunkStorage(m_saveFile, LevelStorage::ENDER_FOLDER);
    }

    // --- CASO 3: OVERWORLD / DEFAULT ---
    return new McRegionChunkStorage(m_saveFile, "");
}

void McRegionLevelStorage::saveLevelData(
    LevelData* levelData, std::vector<std::shared_ptr<Player> >* players) {
    levelData->setVersion(MCREGION_VERSION_ID);
    DirectoryLevelStorage::saveLevelData(levelData, players);
}

void McRegionLevelStorage::closeAll() { 
    RegionFileCache::clear(); 
}