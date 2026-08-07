#include "blocktypeworker.h"
#include "terrain.h"

void BlockTypeWorker::run() {
    // 1. 填充 16 个 Chunk 的地形数据
    for(int cz = 0; cz < 4; ++cz) {
        for(int cx = 0; cx < 4; ++cx) {
            int idx = cz * 4 + cx;
            int chunkX = m_zoneX + cx * 16;
            int chunkZ = m_zoneZ + cz * 16;
            Terrain::fillChunkWithTerrain(m_chunks[idx], chunkX, chunkZ);
        }
    }

    // 2. 将所有 Chunk 指针推入共享队列（加锁）
    {
        QMutexLocker lock(&m_mutex);
        for(Chunk* c : m_chunks) {
            m_pendingVBO.push_back(c);
        }
    }
}