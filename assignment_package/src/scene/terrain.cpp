#include "terrain.h"
#include "cube.h"
#include <stdexcept>
#include <glm/gtc/noise.hpp>
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
            fillChunkWithTerrain(newChunk.get(), checkX, checkZ);  // ★ 填充噪声地形
            newChunk->createVBOdata();
        }
    }


}

// 多倍频 Perlin 噪声，返回值范围约为 [-1, 1]
float Terrain::fractalNoise(glm::vec2 p, int octaves) {
    float value = 0.f;
    float amplitude = 1.f;   // 振幅从 1 开始
    float frequency = 1.f;   // 频率从 1 开始
    float maxValue = 0.f;    // 归一化分母

    for(int i = 0; i < octaves; ++i) {
        value += amplitude * glm::perlin(p * frequency);
        maxValue += amplitude;
        amplitude *= 0.5f;   // 每次振幅减半 → 细节越来越小
        frequency *= 2.f;    // 每次频率翻倍 → 细节越来越密
    }
    return value / maxValue; // 归一化到约 [-1, 1]
}

//草原丘陵
float Terrain::getGrasslandHeight(float x, float z) {
    float h = fractalNoise(glm::vec2(x * 0.01f, z * 0.01f), 4);
    return 138.f + h * 20.f;  // 高度范围: 128 ± 20 → [108, 148]
}

//陡峭山
float Terrain::getMountainHeight(float x, float z) {
    float val = 0.0;
    float amp = 0.5;
    float freq = 0.0005;
    float persistence = 0.9f;

    for(int i = 0; i < 6; ++i) {
        float p = glm::perlin(glm::vec2(x * freq, z * freq));
        val += amp * abs(p);    // abs → 山脊效果
        amp *= persistence;
        freq *= 2.0;
    }
    return 128.f + pow(val ,1.0) * 180.f;  // [128, 308]
}

//地形属性低频噪声 + smoothstep
float Terrain::getBiomeBlend(float x, float z) {
    // 低频 Perlin → 大片区域缓慢变化
    float noise = glm::perlin(glm::vec2(x * 0.008f, z * 0.008f));
    // 从 [-1, 1] 映射到 [0, 1]
    float t = (noise + 1.f) * 0.5f;
    // smoothstep: 在 [0.25, 0.75] 之间平滑过渡
    return glm::smoothstep(0.25f, 0.75f, t);
}

void Terrain::fillChunkWithTerrain(Chunk* chunk, int MinX, int MinZ) {
    int startX = MinX;
    int startZ = MinZ;

    for(int x = 0; x < 16; ++x) {
        for(int z = 0; z < 16; ++z) {
            int worldX = startX + x;
            int worldZ = startZ + z;

            // ---- 第一步：算三种噪声 ----
            //float grassH   = getGrasslandHeight(worldX, worldZ);
            //float mountainH = getMountainHeight(worldX, worldZ);
            float blend     = getBiomeBlend(worldX, worldZ);

            // ---- 第二步：线性插值得到最终高度 ----
            //float finalH = glm::mix(grassH, mountainH, blend);
            float finalH = getHeightAt(worldX, worldZ);
            int   topY   = static_cast<int>(glm::floor(finalH));

            // ---- 第三步：逐 Y 填充方块 ----
            for(int y = 0; y <= 255; ++y) {
                BlockType block;

                if(y <= 128) {
                    // 海平面以下全是石头
                    block = STONE;
                }
                else if(y <= topY) {
                    // 在地表高度以下
                    if(blend < 0.5f) {
                        // ---- 草地区 ----
                        block = (y == topY) ? GRASS : DIRT;
                    } else {
                        // ---- 山脉区 ----
                        block = (y == topY && y >= 200) ? SNOW : STONE;
                    }
                }
                else {
                    // 在地表高度以上 → 空气
                    block = EMPTY;
                }

                // ---- 第四步：注水 ----
                // y ∈ [129, 138] 且该位置是空气 → 填水
                if(y > 128 && y <= 138 && block == EMPTY) {
                    block = WATER;
                }

                chunk->setLocalBlockAt(x, y, z, block);
            }
        }
    }
}

bool Terrain::checkPlayerCollision(glm::vec3 pos) const {
    // 玩家 AABB（始终与世界轴对齐）
    float minX = pos.x - 0.5f,  maxX = pos.x + 0.5f;
    float minY = pos.y,         maxY = pos.y + 2.0f;
    float minZ = pos.z - 0.5f,  maxZ = pos.z + 0.5f;

    // 世界边界：不允许超出 Y 范围
    if(minY < 0.f || maxY > 256.f) return true;

    // 遍历 AABB 触碰到的所有整数方块坐标
    // floor(max - epsilon) 防止 maxY=130.0 落到方块 Y=130 上(该方块实际不在 AABB 内)
    for(int x = static_cast<int>(glm::floor(minX));
         x <= static_cast<int>(glm::floor(maxX - 0.001f)); ++x) {
        for(int y = static_cast<int>(glm::floor(minY));
             y <= static_cast<int>(glm::floor(maxY - 0.001f)); ++y) {
            for(int z = static_cast<int>(glm::floor(minZ));
                 z <= static_cast<int>(glm::floor(maxZ - 0.001f)); ++z) {

                // 检查 Chunk 是否存在
                if(!hasChunkAt(x, z)) return true;   // 未生成区域 = 实心墙

                // 检查方块是否为实心
                BlockType b = getGlobalBlockAt(x, y, z);
                if(b != EMPTY) return true;
            }
        }
    }
    return false;   // 无碰撞
}

float Terrain::getHeightAt(float x, float z) const {
    float grassH     = getGrasslandHeight(x, z);
    float mountainH  = getMountainHeight(x, z);
    float blend      = getBiomeBlend(x, z);
    return glm::mix(grassH, mountainH, blend);
}

void Terrain::CreateTestScene(glm::vec3 playerPos)
{
    // TODO: DELETE THIS LINE WHEN YOU DELETE m_geomCube!
    //m_geomCube.createVBOdata();
    // ---- 玩家所在的 Zone 左下角 ----
    int playerZoneX = static_cast<int>(glm::floor(playerPos.x / 64.f)) * 64;
    int playerZoneZ = static_cast<int>(glm::floor(playerPos.z / 64.f)) * 64;

    // Create the Chunks that will
    // store the blocks for our
    // initial world space
    // ---- 生成 3×3 个 Zone（每个 Zone = 4×4 个 Chunk） ----
    for(int dz = -1; dz <= 1; ++dz) {
        for(int dx = -1; dx <= 1; ++dx) {
            int zoneX = playerZoneX + dx * 64;
            int zoneZ = playerZoneZ + dz * 64;

            int64_t zoneKey = toKey(zoneX, zoneZ);
            if(m_generatedTerrain.find(zoneKey) != m_generatedTerrain.end())
                continue;                        // 已存在，跳过

            m_generatedTerrain.insert(zoneKey);

            // 为该 Zone 创建 4×4 = 16 个 Chunk
            for(int cx = 0; cx < 64; cx += 16) {
                for(int cz = 0; cz < 64; cz += 16) {
                    instantiateChunkAt(zoneX + cx, zoneZ + cz);
                }
            }
        }
    }

    // // Create the basic terrain floor
    // for(int x = 0; x < 64; ++x) {
    //     for(int z = 0; z < 64; ++z) {
    //         if((x + z) % 2 == 0) {
    //             setGlobalBlockAt(x, 128, z, STONE);
    //         }
    //         else {
    //             setGlobalBlockAt(x, 128, z, DIRT);
    //         }
    //     }
    // }
    // // Add "walls" for collision testing
    // for(int x = 0; x < 64; ++x) {
    //     setGlobalBlockAt(x, 129, 16, GRASS);
    //     setGlobalBlockAt(x, 130, 16, GRASS);
    //     setGlobalBlockAt(x, 129, 48, GRASS);
    //     setGlobalBlockAt(16, 130, x, GRASS);
    // }
    // // Add a central column
    // for(int y = 129; y < 140; ++y) {
    //     setGlobalBlockAt(32, y, 32, GRASS);
    // }

    // ★ 放完所有方块后，统一构建每个 Chunk 的 VBO
    // ---- 对所有新创建的 Chunk 填充地形并构建 VBO ----
    for(int dz = -1; dz <= 1; ++dz) {
        for(int dx = -1; dx <= 1; ++dx) {
            int zoneX = playerZoneX + dx * 64;
            int zoneZ = playerZoneZ + dz * 64;

            for(int cx = 0; cx < 64; cx += 16) {
                for(int cz = 0; cz < 64; cz += 16) {
                    uPtr<Chunk> &c = getChunkAt(zoneX + cx, zoneZ + cz);
                    fillChunkWithTerrain(c.get(), zoneX + cx, zoneZ + cz );   // 噪声填充
                    c->createVBOdata();               // 构建 VBO
                }
            }
        }
    }
}
