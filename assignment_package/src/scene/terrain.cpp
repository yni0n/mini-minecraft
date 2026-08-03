#include "terrain.h"
#include "cube.h"
#include <stdexcept>
#include <iostream>

Terrain::Terrain(OpenGLContext *context)
    : m_chunks(), m_generatedTerrain(), m_geomCube(context),
      m_chunkVBOsNeedUpdating(true), mp_context(context)
{}

Terrain::~Terrain() {
    m_geomCube.destroyVBOdata();
}

// Combine two 32-bit ints into one 64-bit int
// where the upper 32 bits are X and the lower 32 bits are Z
int64_t toKey(int x, int z) {
    int64_t xz = 0xffffffffffffffff;
    int64_t x64 = x;
    int64_t z64 = z;

    // Set all lower 32 bits to 1 so we can & with Z later
    xz = (xz & (x64 << 32)) | 0x00000000ffffffff;

    // Set all upper 32 bits to 1 so we can & with XZ
    z64 = z64 | 0xffffffff00000000;

    // Combine
    xz = xz & z64;
    return xz;
}

glm::ivec2 toCoords(int64_t k) {
    // Z is lower 32 bits
    int64_t z = k & 0x00000000ffffffff;
    // If the most significant bit of Z is 1, then it's a negative number
    // so we have to set all the upper 32 bits to 1.
    // Note the 8    V
    if(z & 0x0000000080000000) {
        z = z | 0xffffffff00000000;
    }
    int64_t x = (k >> 32);

    return glm::ivec2(x, z);
}

// Surround calls to this with try-catch if you don't know whether
// the coordinates at x, y, z have a corresponding Chunk
BlockType Terrain::getGlobalBlockAt(int x, int y, int z) const
{
    if(hasChunkAt(x, z)) {
        // Just disallow action below or above min/max height,
        // but don't crash the game over it.
        if(y < 0 || y >= 256) {
            return EMPTY;
        }
        const uPtr<Chunk> &c = getChunkAt(x, z);
        glm::vec2 chunkOrigin = glm::vec2(floor(x / 16.f) * 16, floor(z / 16.f) * 16);
        return c->getLocalBlockAt(static_cast<unsigned int>(x - chunkOrigin.x),
                                  static_cast<unsigned int>(y),
                                  static_cast<unsigned int>(z - chunkOrigin.y));
    }
    else {
        throw std::out_of_range("Coordinates " + std::to_string(x) +
                                " " + std::to_string(y) + " " +
                                std::to_string(z) + " have no Chunk!");
    }
}

BlockType Terrain::getGlobalBlockAt(glm::vec3 p) const {
    return getGlobalBlockAt(p.x, p.y, p.z);
}

bool Terrain::hasChunkAt(int x, int z) const {
    // Map x and z to their nearest Chunk corner
    // By flooring x and z, then multiplying by 16,
    // we clamp (x, z) to its nearest Chunk-space corner,
    // then scale back to a world-space location.
    // Note that floor() lets us handle negative numbers
    // correctly, as floor(-1 / 16.f) gives us -1, as
    // opposed to (int)(-1 / 16.f) giving us 0 (incorrect!).
    int xFloor = static_cast<int>(glm::floor(x / 16.f));
    int zFloor = static_cast<int>(glm::floor(z / 16.f));
    return m_chunks.find(toKey(16 * xFloor, 16 * zFloor)) != m_chunks.end();
}


uPtr<Chunk>& Terrain::getChunkAt(int x, int z) {
    int xFloor = static_cast<int>(glm::floor(x / 16.f));
    int zFloor = static_cast<int>(glm::floor(z / 16.f));
    return m_chunks[toKey(16 * xFloor, 16 * zFloor)];
}


const uPtr<Chunk>& Terrain::getChunkAt(int x, int z) const {
    int xFloor = static_cast<int>(glm::floor(x / 16.f));
    int zFloor = static_cast<int>(glm::floor(z / 16.f));
    return m_chunks.at(toKey(16 * xFloor, 16 * zFloor));
}

void Terrain::setGlobalBlockAt(int x, int y, int z, BlockType t)
{
    if(hasChunkAt(x, z)) {
        uPtr<Chunk> &c = getChunkAt(x, z);
        glm::vec2 chunkOrigin = glm::vec2(floor(x / 16.f) * 16, floor(z / 16.f) * 16);
        c->setLocalBlockAt(static_cast<unsigned int>(x - chunkOrigin.x),
                           static_cast<unsigned int>(y),
                           static_cast<unsigned int>(z - chunkOrigin.y),
                           t);
    }
    else {
        throw std::out_of_range("Coordinates " + std::to_string(x) +
                                " " + std::to_string(y) + " " +
                                std::to_string(z) + " have no Chunk!");
    }
}

Chunk* Terrain::instantiateChunkAt(int x, int z) {
    uPtr<Chunk> chunk = mkU<Chunk>(mp_context, x, z);
    Chunk *cPtr = chunk.get();
    m_chunks[toKey(x, z)] = move(chunk);
    // 给新生成的chunk链接邻居+临邻居链接它
    if(hasChunkAt(x, z + 16)) {
        auto &chunkNorth = m_chunks[toKey(x, z + 16)];
        cPtr->linkNeighbor(chunkNorth, ZPOS);
    }
    if(hasChunkAt(x, z - 16)) {
        auto &chunkSouth = m_chunks[toKey(x, z - 16)];
        cPtr->linkNeighbor(chunkSouth, ZNEG);
    }
    if(hasChunkAt(x + 16, z)) {
        auto &chunkEast = m_chunks[toKey(x + 16, z)];
        cPtr->linkNeighbor(chunkEast, XPOS);
    }
    if(hasChunkAt(x - 16, z)) {
        auto &chunkWest = m_chunks[toKey(x - 16, z)];
        cPtr->linkNeighbor(chunkWest, XNEG);
    }
    return cPtr;
    return cPtr;
}

// TODO: When you make Chunk inherit from Drawable, change this code so
// it draws each Chunk with the given ShaderProgram
void Terrain::draw(int minX, int maxX, int minZ, int maxZ, ShaderProgram *shaderProgram) {

    for(int x = minX; x < maxX; x += 16) {
        for(int z = minZ; z < maxZ; z += 16) {
            if(!hasChunkAt(x, z)) continue;       // ★ 跳过不存在的区块
            const uPtr<Chunk> &chunk = getChunkAt(x, z);
            shaderProgram->drawInterleaved(*chunk);
        }
    }
}

void Terrain::expandTerrain(glm::vec3 playerPos) {
    // 玩家所在区块的 min corner
    int playerChunkX = static_cast<int>(glm::floor(playerPos.x / 16.f)) * 16;
    int playerChunkZ = static_cast<int>(glm::floor(playerPos.z / 16.f)) * 16;

    // 检查玩家区块周围的 3×3 网格（9 个位置）
    for(int dx = -1; dx <= 1; ++dx) {
        for(int dz = -1; dz <= 1; ++dz) {
            int checkX = playerChunkX + dx * 16;
            int checkZ = playerChunkZ + dz * 16;

            // 已存在 → 跳过
            if(hasChunkAt(checkX, checkZ)) continue;

            // 计算玩家到"缺失区块范围"的距离（切比雪夫距离）
            float dX = 0.f, dZ = 0.f;
            if(playerPos.x < checkX) {
                dX = checkX - playerPos.x;                 // 玩家在区块左边
            } else if(playerPos.x >= checkX + 16) {
                dX = playerPos.x - (checkX + 16);          // 玩家在区块右边
            }
            // else: 玩家在区块的 X 范围内 → dX = 0

            if(playerPos.z < checkZ) {
                dZ = checkZ - playerPos.z;                 // 玩家在区块上方
            } else if(playerPos.z >= checkZ + 16) {
                dZ = playerPos.z - (checkZ + 16);          // 玩家在区块下方
            }

            // 任一分量超过 16 格 → 还不够近，跳过
            if(glm::max(dX, dZ) > 16.f) continue;

            // ---- 生成新区块 ----
            instantiateChunkAt(checkX, checkZ);
            uPtr<Chunk> &newChunk = getChunkAt(checkX, checkZ);
            // TODO: 后续可以在这里填充方块（地形生成）
            newChunk->createVBOdata();
        }
    }
}

void Terrain::CreateTestScene()
{
    // TODO: DELETE THIS LINE WHEN YOU DELETE m_geomCube!
    //m_geomCube.createVBOdata();

    // Create the Chunks that will
    // store the blocks for our
    // initial world space
    for(int x = 0; x < 64; x += 16) {
        for(int z = 0; z < 64; z += 16) {
            instantiateChunkAt(x, z);
        }
    }
    // Tell our existing terrain set that
    // the "generated terrain zone" at (0,0)
    // now exists.
    m_generatedTerrain.insert(toKey(0, 0));

    // Create the basic terrain floor
    for(int x = 0; x < 64; ++x) {
        for(int z = 0; z < 64; ++z) {
            if((x + z) % 2 == 0) {
                setGlobalBlockAt(x, 128, z, STONE);
            }
            else {
                setGlobalBlockAt(x, 128, z, DIRT);
            }
        }
    }
    // Add "walls" for collision testing
    for(int x = 0; x < 64; ++x) {
        setGlobalBlockAt(x, 129, 16, GRASS);
        setGlobalBlockAt(x, 130, 16, GRASS);
        setGlobalBlockAt(x, 129, 48, GRASS);
        setGlobalBlockAt(16, 130, x, GRASS);
    }
    // Add a central column
    for(int y = 129; y < 140; ++y) {
        setGlobalBlockAt(32, y, 32, GRASS);
    }

    // ★ 放完所有方块后，统一构建每个 Chunk 的 VBO
    for(int x = 0; x < 64; x += 16) {
        for(int z = 0; z < 64; z += 16) {
            uPtr<Chunk> &c = getChunkAt(x, z);
            c->createVBOdata();
        }
    }
}
