#include "vboworker.h"
#include "terrain.h"

void VBOWorker::run() {
    ChunkVBOData vbo;
    vbo.chunkX = m_chunk->getMinX();
    vbo.chunkZ = m_chunk->getMinZ();

    m_chunk->computeVBOData(vbo.opaqueData, vbo.opaqueIdx,
                            vbo.transparentData, vbo.transparentIdx);

    {
        QMutexLocker lock(&m_mutex);
        m_completedVBOs.push_back(std::move(vbo));
    }
}