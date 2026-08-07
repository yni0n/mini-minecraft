#pragma once
#include <QRunnable>
#include <QMutex>
#include <vector>
#include "chunk.h"

// BlockTypeWorker：在子线程中为一个 Zone（4×4 Chunk）填充地形方块数据
class BlockTypeWorker : public QRunnable {
private:
    int m_zoneX, m_zoneZ;
    std::vector<Chunk*> m_chunks;          // 该 Zone 的 16 个 Chunk 指针
    std::vector<Chunk*>& m_pendingVBO;     // 共享容器：待生成 VBO 的 Chunk
    QMutex& m_mutex;                       // 共享容器的互斥锁

public:
    BlockTypeWorker(int zoneX, int zoneZ,
                    const std::vector<Chunk*>& chunks,
                    std::vector<Chunk*>& pendingVBO,
                    QMutex& mutex)
        : m_zoneX(zoneX), m_zoneZ(zoneZ),
        m_chunks(chunks),
        m_pendingVBO(pendingVBO),
        m_mutex(mutex)
    {}

    void run() override ;
};