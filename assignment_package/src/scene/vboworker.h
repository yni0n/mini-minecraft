#pragma once
#include <QRunnable>
#include <QMutex>
#include <vector>
#include "chunk.h"
#include "terrain.h"   // 需要 ChunkVBOData 的定义

class VBOWorker : public QRunnable {
private:
    Chunk* m_chunk;
    std::vector<ChunkVBOData>& m_completedVBOs;
    QMutex& m_mutex;

public:
    VBOWorker(Chunk* chunk,
              std::vector<ChunkVBOData>& completedVBOs,
              QMutex& mutex)
        : m_chunk(chunk),
        m_completedVBOs(completedVBOs),
        m_mutex(mutex)
    {}

    void run() override;  // 实现在 .cpp
};