#include "terrain.h"
#include "cube.h"
#include "blocktypeworker.h"
#include "vboworker.h"
#include <cstdint>   // uint32_t（防御性加上，现有代码多半已间接引入）
#include <stdexcept>
#include <glm/gtc/noise.hpp>
#include <cmath>
#include <QThreadPool>
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
void Terrain::draw(int minX, int maxX, int minZ, int maxZ, ShaderProgram *shaderProgram, const glm::vec4* frustumPlanes) {

    for(int x = minX; x < maxX; x += 16) {
        for(int z = minZ; z < maxZ; z += 16) {
            if(!hasChunkAt(x, z)) continue;       // ★ 跳过不存在的区块

            // ★ 新增:Chunk 级视锥剔除 —— AABB (x, 0, z) ~ (x+16, 256, z+16)
            if(frustumPlanes) {
                bool visible = true;
                for(int i = 0; i < 6; ++i) {
                    const glm::vec4& p = frustumPlanes[i];
                    float px = (p.x > 0.f) ? (x + 16.f) : static_cast<float>(x);
                    float py = (p.y > 0.f) ? 256.f : 0.f;
                    float pz = (p.z > 0.f) ? (z + 16.f) : static_cast<float>(z);
                    if(p.x * px + p.y * py + p.z * pz + p.w < 0.f) {
                        visible = false;          // 完全在平面外侧
                        break;
                    }
                }
                if(!visible) continue;
            }
            const uPtr<Chunk> &chunk = getChunkAt(x, z);
            if(!chunk->hasVBO()) continue;
            if(chunk->elemCount(INDEX) <= 0) continue;       // ← 新增
            shaderProgram->drawInterleaved(*chunk);
        }
    }
}

void Terrain::drawTransparent(int minX, int maxX, int minZ, int maxZ, ShaderProgram *shaderProgram, const glm::vec4* frustumPlanes) {
    for(int x = minX; x < maxX; x += 16) {
        for(int z = minZ; z < maxZ; z += 16) {
            if(!hasChunkAt(x, z)) continue;

            if(frustumPlanes) {
                bool visible = true;
                for(int i = 0; i < 6; ++i) {
                    const glm::vec4& p = frustumPlanes[i];
                    float px = (p.x > 0.f) ? (x + 16.f) : static_cast<float>(x);
                    float py = (p.y > 0.f) ? 256.f : 0.f;
                    float pz = (p.z > 0.f) ? (z + 16.f) : static_cast<float>(z);
                    if(p.x * px + p.y * py + p.z * pz + p.w < 0.f) {
                        visible = false;          // 完全在平面外侧
                        break;
                    }
                }
                if(!visible) continue;
            }
            const uPtr<Chunk> &chunk = getChunkAt(x, z);
            if(!chunk->hasVBO()) continue;
            if(chunk->elemCount(INDEX_TRANSPARENT) <= 0) continue;   // ← 新增
            shaderProgram->drawInterleavedTransparent(*chunk);
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

            // ★ 重建相邻已有 Chunk 的 VBO，确保跨 Chunk 面剔除正确
            static const std::pair<int, int> neighborOffsets[] = {
                { 16,  0},   // XPOS 邻居
                {-16,  0},   // XNEG 邻居
                {  0, 16},   // ZPOS 邻居
                {  0,-16},   // ZNEG 邻居
            };
            for(auto [dx, dz] : neighborOffsets) {
                int nx = checkX + dx, nz = checkZ + dz;
                if(hasChunkAt(nx, nz)) {
                    getChunkAt(nx, nz)->createVBOdata();
                }
            }
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
    return 136.f + h * 10.f;  // 高度范围: 138 ± 10 → [128, 148]
}

//陡峭山
float Terrain::getMountainHeight(float x, float z) {
    float val = 0.0;
    float amp = 0.5;
    float freq = 0.0015;
    float persistence = 0.9f;

    for(int i = 0; i < 6; ++i) {
        float p = glm::perlin(glm::vec2(x * freq, z * freq));
        val += amp * abs(p);    // abs → 山脊效果
        amp *= persistence;
        freq *= 2.0;
    }
    return 158.f + pow(val ,1.5) * 100.f;  // [128, 308]
}

//沙漠
float Terrain::getDesertHeight(float x, float z) {
    float h = fractalNoise(glm::vec2(x * 0.008f, z * 0.008f), 3);
    return 145.f + h * 6.f;  // 沙漠更平坦：[129, 141]
}

float Terrain::getDesertBlend(float x, float z) {
    // 用不同频率/偏移的噪声，避免和草原/山地完全重合
    float n = glm::perlin(glm::vec2(x * 0.003f + 1000.f, z * 0.003f + 1000.f));
    float t = (n + 1.f) * 0.5f;
    return glm::smoothstep(0.6f, 0.85f, t);  // 沙漠只占小部分区域
}

//地形属性低频噪声 + smoothstep
float Terrain::getBiomeBlend(float x, float z) {
    // 低频 Perlin → 大片区域缓慢变化
    float noise = glm::perlin(glm::vec2(x * 0.002f, z * 0.002f));
    // 从 [-1, 1] 映射到 [0, 1]
    float t = (noise + 1.f) * 0.5f;
    // smoothstep: 在 [0.25, 0.75] 之间平滑过渡
    return glm::smoothstep(0.25f, 0.75f, t);
}

float Terrain::caveNoise(float x, float y, float z) {
    // float val = 0.f;
    // float amp = 1.f;
    // float freq = 0.03f;
    // float freqY = 0.06f;   // Y 轴频率更高 → 洞穴在垂直方向变化更快
    // float persistence = 0.5f;

    // for(int i = 0; i < 4; ++i) {
    //     val += amp * glm::perlin(glm::vec3(x * freq, y * freqY, z * freq));
    //     amp *= persistence;
    //     freq *= 2.f;
    //     freqY *= 2.f;
    // }
    float val = glm::perlin(glm::vec3(x * 0.06f, y * 0.12f, z * 0.06f));
    return val;
}

// ==================== 植被装饰 ====================
// 确定性 hash：同一世界坐标永远返回同一数值
// → 相邻 chunk / 重新生成 / 多线程下结果完全一致（无需任何同步）
static inline uint32_t hashWorld(int x, int y, int z) {
    uint32_t h = 0x9E3779B9u;                  // 黄金比例常数作种子
    h ^= static_cast<uint32_t>(x) + 0x9E3779B9u + (h << 6) + (h >> 2);
    h ^= static_cast<uint32_t>(y) + 0x9E3779B9u + (h << 6) + (h >> 2);
    h ^= static_cast<uint32_t>(z) + 0x9E3779B9u + (h << 6) + (h >> 2);
    h ^= (h >> 13);
    h *= 0x5bd1e995u;
    h ^= (h >> 15);
    return h;
}

// hash → [0, 1) 的均匀浮点，用作"概率掷骰子"
static inline float hash01(int x, int y, int z) {
    return hashWorld(x, y, z) / 4294967296.0f;
}

// ---- 植被密度参数（想调疏密只改这里） ----
static const float kGrassDensity  = 0.08f;   // 每格草原长草的概率
static const float kFlowerDensity = 0.005f;   // 每格草原长花的概率
static const float kCactusDensity = 0.002f;   // 每格沙漠长仙人掌的概率

// 单格植物装饰：草 / 花 / 仙人掌（树在 Step 5 单独加入）
static void decorateChunk(Chunk* chunk, int startX, int startZ) {
    for(int x = 0; x < 16; ++x) {
        for(int z = 0; z < 16; ++z) {
            int wx = startX + x;
            int wz = startZ + z;

            // 地表高度：与 fillChunkWithTerrain 同一函数 → 结果确定一致
            int topY = static_cast<int>(glm::floor(Terrain::getHeightAt(wx, wz)));
            if(topY < 1 || topY > 254) continue;

            // 只长在开阔地表：头顶第一格必须是空气
            // （水下沙地、被水淹的格自然跳过，杜绝植物长进水里）
            if(chunk->getLocalBlockAt(x, topY + 1, z) != EMPTY) continue;

            // 以实际地表方块类型判定生物群系（比再算 blend 更可靠）
            BlockType surface = chunk->getLocalBlockAt(x, topY, z);

            if(surface == GRASS) {
                // 草原：草 / 花。先判花、再判草；一格至多一株 → 天然不打架
                float r = hash01(wx, topY, wz);
                if(r < kFlowerDensity) {
                    chunk->setLocalBlockAt(x, topY + 1, z, FLOWER);
                } else if(r < kFlowerDensity + kGrassDensity) {
                    chunk->setLocalBlockAt(x, topY + 1, z, TALLGRASS);
                }
            }
            else if(surface == SAND && Terrain::getDesertBlend(wx, wz) > 0.5f) {
                // 沙漠：仙人掌（getDesertBlend 判定用于排除"水边沙滩"）
                float r = hash01(wx, topY, wz);
                if(r < kCactusDensity) {
                    // 高度按 MC 官方概率：1 格 11/18、2 格 5/18、3 格 2/18
                    float rh = hash01(wx, topY + 1, wz);   // 换盐 → 与位置判定独立
                    int h;
                    if(rh < 11.f / 18.f)      h = 1;
                    else if(rh < 16.f / 18.f) h = 2;
                    else                      h = 3;
                    if(topY + h > 255) h = 255 - topY;     // 防御钳制（正常不会触发）
                    for(int i = 1; i <= h; ++i) {
                        chunk->setLocalBlockAt(x, topY + i, z, CACTUS);
                    }
                }
            }
        }
    }
}

// ==================== 树的生成 ====================
// 世界按 8×8 cell 划分，每 cell 至多 1 棵树 → 无需任何距离检查
// 树落在 cell 中心 ±1 → 相邻 cell 树干距离恒 ≥ 6（树冠半径 2，永不相交）
// 树的位置/高度全部由 hash(cell 坐标) 决定
// → 相邻 chunk 各自计算得到同一棵树，各自只写落在自己 16×16 内的方块
static const float kTreeDensity = 0.15f;   // 每个 cell 出树的概率

static void spawnTrees(Chunk* chunk, int startX, int startZ) {
    // 只需处理"树冠可能与本 chunk 相交"的 cell：
    // 本 chunk 外扩 1 个 cell（8 格 ≥ 树冠半径 2 + 抖动 1），startX 是 16 的倍数，除法无余数
    const int cminX = startX / 8 - 1, cmaxX = startX / 8 + 2;
    const int cminZ = startZ / 8 - 1, cmaxZ = startZ / 8 + 2;

    for(int cx = cminX; cx <= cmaxX; ++cx) {
        for(int cz = cminZ; cz <= cmaxZ; ++cz) {
            // 1) 该 cell 是否出树（不同用途用不同 y 盐，避免数值相关）
            if(hash01(cx, 0, cz) >= kTreeDensity) continue;

            // 2) 树干世界坐标：cell 中心 (c*8 + 4) + 抖动 ±1
            int tx = cx * 8 + 4 + static_cast<int>(hash01(cx, 1, cz) * 3.f) - 1;
            int tz = cz * 8 + 4 + static_cast<int>(hash01(cx, 2, cz) * 3.f) - 1;

            // 3) 树干高度 4~6（树冠另加）
            int trunkH = 5 + static_cast<int>(hash01(cx, 3, cz) * 3.f);

            // 4) 只长草原（纯噪声判定，可对 chunk 外的坐标求值）：
            //    topY ≥ 140 排除沙滩/水边；desert/blend 排除沙漠与山地
            int gY = static_cast<int>(glm::floor(Terrain::getHeightAt(tx, tz)));
            if(gY < 140)                                  continue;
            if(Terrain::getDesertBlend(tx, tz) > 0.5f)    continue;
            if(Terrain::getBiomeBlend(tx, tz) >= 0.5f)    continue;

            // 5) 写入辅助：只写本 chunk 内的格，其余留给邻居补
            auto put = [&](int bx, int by, int bz, BlockType bt) {
                int lx = bx - startX, lz = bz - startZ;
                if(lx < 0 || lx >= 16 || lz < 0 || lz >= 16) return;
                if(by < 1 || by > 255) return;
                chunk->setLocalBlockAt(lx, by, lz, bt);
            };
            auto putLeaf = [&](int bx, int by, int bz) {
                if(bx == tx && bz == tz && by <= gY + trunkH) return; // 不覆盖树干
                put(bx, by, bz, LEAVES);
            };

            const int trunkTop = gY + trunkH;

            // 树干：立于地表之上
            for(int i = 1; i <= trunkH; ++i) {
                put(tx, gY + i, tz, LOG);
            }
            // 树冠：方形骨架 + 边缘微扰（缺角/凸出均由世界坐标 hash 决定 → 跨 chunk 一致）
            // d = max(|dx|,|dz|)（方形轮廓）
            //   d < r            → 必放（实心）
            //   d == r（外圈）   → 概率保留：四角更易缺，轮廓出现不规则缺口
            //   d == r+1（外扩） → 仅四边中点的正外方、低概率冒出一格
            auto canopyLayer = [&](int by, int r) {
                for(int dx = -r - 1; dx <= r + 1; ++dx) {
                    for(int dz = -r - 1; dz <= r + 1; ++dz) {
                        int ax = (dx < 0) ? -dx : dx;
                        int az = (dz < 0) ? -dz : dz;
                        int d  = (ax > az) ? ax : az;
                        if(d > r) {                              // 轮廓外：只许十字轴方向外扩
                            if(d != r + 1) continue;
                            if(ax != 0 && az != 0) continue;
                            if(hash01(tx + dx, by, tz + dz) < 0.12f)
                                putLeaf(tx + dx, by, tz + dz);
                            continue;
                        }
                        if(d == r) {                             // 外圈：角低保留、边中高保留
                            float keep = (ax == r && az == r) ? 0.5f : 0.85f;
                            if(hash01(tx + dx, by, tz + dz) < keep)
                                putLeaf(tx + dx, by, tz + dz);
                            continue;
                        }
                        putLeaf(tx + dx, by, tz + dz);           // 内部实心
                    }
                }
            };

            // 树冠三层（每层独立微扰）：两层 5×5 + 一层 3×3，十字顶保持原样
            canopyLayer(trunkTop - 2, 2);
            canopyLayer(trunkTop - 1, 2);
            canopyLayer(trunkTop,     1);

            putLeaf(tx,     trunkTop + 1, tz);
            putLeaf(tx + 1, trunkTop + 1, tz);
            putLeaf(tx - 1, trunkTop + 1, tz);
            putLeaf(tx,     trunkTop + 1, tz + 1);
            putLeaf(tx,     trunkTop + 1, tz - 1);
        }
    }
}


void Terrain::fillChunkWithTerrain(Chunk* chunk, int MinX, int MinZ) {
    int startX = MinX;
    int startZ = MinZ;

    // ========== 主力块填充 =========
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
            // ---- 洞穴：预计算该列所有 Y 层的噪声值 ----
            int caveMaxY = std::min(128, topY);   // ★ 该列实际的最大 STONE 层
            float caveCache[129];                  // 栈上数组，尺寸小开销低

            // ★ 只对奇数 Y 采样 Perlin（减半），偶数 Y 后面插值
            caveCache[1] = caveNoise(worldX, 1, worldZ);
            for(int y = 3; y <= caveMaxY; y += 2) {
                caveCache[y] = caveNoise(worldX, y, worldZ);
                // 偶数层 y-1：取奇数层 y-2 和 y 的均值
                caveCache[y-1] = (caveCache[y-2] + caveCache[y]) * 0.5f;
            }
            // 如果 caveMaxY 是偶数，补算最后一层
            if(caveMaxY % 2 == 0 && caveMaxY >= 2) {
                caveCache[caveMaxY] = caveNoise(worldX, caveMaxY, worldZ);
                caveCache[caveMaxY-1] = (caveCache[caveMaxY-2] + caveCache[caveMaxY]) * 0.5f;
            }

            int waterLevel = 138;//沙滩
            bool nearWater = (topY >= waterLevel - 3 && topY <= waterLevel + 1);
            float desertBlend = getDesertBlend(worldX, worldZ);//沙漠

            // ---- 逐 Y 填充方块 + 应用洞穴缓存 ----
            for(int y = 0; y <= 255; ++y) {
                BlockType block;

                if(y == 0) {
                    block = BEDROCK;
                }
                else if(y <= 128) {
                    block = STONE;
                }
                else if(y <= topY) {
                    if(desertBlend > 0.5f) {
                        block = (y > topY - 4) ? SAND : STONE;  // 顶部4层沙子
                    }else if(topY < waterLevel) {
                        // 水下：沙底
                        block = (y > topY - 4) ? SAND : DIRT;
                    } else if(nearWater) {
                        // 水边沙滩
                        block = (y > topY - 3) ? SAND : DIRT;
                    } else if(blend < 0.5f) {
                        block = (y == topY) ? GRASS : DIRT;
                    } else {
                        block = (y == topY && y >= 200) ? SNOW : STONE;
                    }
                }
                else {
                    block = EMPTY;
                }

                // ★ 用预计算的缓存判断洞穴
                if(block == STONE && y >= 1 && y <= caveMaxY) {
                    if(caveCache[y] < -0.15f) {
                        block = (y < 25) ? LAVA : EMPTY;
                    }
                }

                if(y > 128 && y <= 138 && block == EMPTY) {
                    block = WATER;
                }

                chunk->setLocalBlockAt(x, y, z, block);
            }
        }
    }
    // ---- 第四步：植被装饰（草/花/仙人掌） ----
    decorateChunk(chunk, startX, startZ);
    spawnTrees(chunk, startX, startZ);   // 树

    chunk->setBlockDataFilled(true);
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
                if(b != EMPTY && b != WATER && b != LAVA && b != TALLGRASS && b != FLOWER) return true;
            }
        }
    }
    return false;   // 无碰撞
}

RaycastResult Terrain::raycast(glm::vec3 origin, glm::vec3 dir, float maxDist) const {
    RaycastResult result;
    result.hit = false;
    result.blockPos = glm::ivec3(0);
    result.faceNormal = XPOS;

    // ---- 起始方块 ----
    glm::ivec3 current(
        static_cast<int>(glm::floor(origin.x)),
        static_cast<int>(glm::floor(origin.y)),
        static_cast<int>(glm::floor(origin.z))
        );

    // Y 轴越界直接返回
    if(current.y < 0 || current.y >= 256) return result;

    // ---- 步进方向 ----
    glm::ivec3 step(
        (dir.x > 0.f) ? 1 : -1,
        (dir.y > 0.f) ? 1 : -1,
        (dir.z > 0.f) ? 1 : -1
        );

    // ---- tMax：到达各轴第一个边界的参数 t ----
    glm::vec3 tMax;
    tMax.x = (dir.x > 0.f)  ? (glm::ceil(origin.x) - origin.x) / dir.x
             : (dir.x < 0.f)  ? (origin.x - glm::floor(origin.x)) / (-dir.x)
                             : INFINITY;
    tMax.y = (dir.y > 0.f)  ? (glm::ceil(origin.y) - origin.y) / dir.y
             : (dir.y < 0.f)  ? (origin.y - glm::floor(origin.y)) / (-dir.y)
                             : INFINITY;
    tMax.z = (dir.z > 0.f)  ? (glm::ceil(origin.z) - origin.z) / dir.z
             : (dir.z < 0.f)  ? (origin.z - glm::floor(origin.z)) / (-dir.z)
                             : INFINITY;

    // ---- tDelta：相邻方块边界之间的参数 t 增量 ----
    glm::vec3 tDelta(
        (dir.x != 0.f) ? (1.f / glm::abs(dir.x)) : INFINITY,
        (dir.y != 0.f) ? (1.f / glm::abs(dir.y)) : INFINITY,
        (dir.z != 0.f) ? (1.f / glm::abs(dir.z)) : INFINITY
        );

    // ---- 记录命中面法线 ----
    Direction hitNormal = XPOS;

    // ---- 主循环 ----
    int maxIter = static_cast<int>(maxDist * 3) + 10;
    for(int i = 0; i < maxIter; ++i) {
        // 检查 Y 边界
        if(current.y < 0 || current.y >= 256) break;

        // 检查当前方块是否实心
        if(hasChunkAt(current.x, current.z)) {
            BlockType block = getGlobalBlockAt(current.x, current.y, current.z);
            if(block != EMPTY) {
                result.hit = true;
                result.blockPos = current;
                result.faceNormal = hitNormal;
                return result;
            }
        }
        // Chunk 未生成 → 视为空气，射线继续

        // ---- 选择 tMax 最小的轴步进 ----
        if(tMax.x < tMax.y && tMax.x < tMax.z) {
            if(tMax.x > maxDist) break;
            current.x += step.x;
            tMax.x += tDelta.x;
            hitNormal = (step.x > 0) ? XNEG : XPOS;
        }
        else if(tMax.y < tMax.z) {
            if(tMax.y > maxDist) break;
            current.y += step.y;
            tMax.y += tDelta.y;
            hitNormal = (step.y > 0) ? YNEG : YPOS;
        }
        else {
            if(tMax.z > maxDist) break;
            current.z += step.z;
            tMax.z += tDelta.z;
            hitNormal = (step.z > 0) ? ZNEG : ZPOS;
        }
    }

    return result;
}

float Terrain::getHeightAt(float x, float z) {
    float grassH     = getGrasslandHeight(x, z);
    float mountainH  = getMountainHeight(x, z);
    float desertH   = getDesertHeight(x, z);
    float blend     = getBiomeBlend(x, z);
    float desert    = getDesertBlend(x, z);

    // 先算草原-山地混合
    float grassMountainH = glm::mix(grassH, mountainH, blend);
    // 再用 desert 值平滑过渡到沙漠高度
    return glm::mix(grassMountainH, desertH, desert);
}

void Terrain::CreateTestScene(glm::vec3 playerPos)
{
    // TODO: DELETE THIS LINE WHEN YOU DELETE m_geomCube!
    //m_geomCube.createVBOdata();
    // ---- 玩家所在的 Zone 左下角 ----
    int playerZoneX = static_cast<int>(glm::floor(playerPos.x / 64.f)) * 64;
    int playerZoneZ = static_cast<int>(glm::floor(playerPos.z / 64.f)) * 64;

    // ★ 恢复同步生成：创建 Chunk → 填地形 → 建 VBO
    for(int dz = -1; dz <= 1; ++dz) {
        for(int dx = -1; dx <= 1; ++dx) {
            int zoneX = playerZoneX + dx * 64;
            int zoneZ = playerZoneZ + dz * 64;

            int64_t zoneKey = toKey(zoneX, zoneZ);
            if(m_generatedTerrain.find(zoneKey) != m_generatedTerrain.end())
                continue;

            m_generatedTerrain.insert(zoneKey);
            for(int cz = 0; cz < 64; cz += 16) {
                for(int cx = 0; cx < 64; cx += 16) {
                    int chunkX = zoneX + cx;
                    int chunkZ = zoneZ + cz;
                    Chunk* c = instantiateChunkAt(chunkX, chunkZ);
                    fillChunkWithTerrain(c, chunkX, chunkZ);
                    c->createVBOdata();          // ★ 在主线程算 VBO 并上传 GPU
                }
            }
        }
    }
}

void Terrain::tick(glm::vec3 playerPos) {
    // ============================================================
    // 阶段 1：上传已完成的 VBO 数据到 GPU（主线程才有 OpenGL 上下文）
    // ============================================================
    {
        const int MAX_VBO_UPLOADS_PER_FRAME = 8;

        QMutexLocker lock(&m_completedVBOsMutex);
        int uploaded = 0;
        auto it = m_completedVBOs.begin();
        while(it != m_completedVBOs.end() && uploaded < MAX_VBO_UPLOADS_PER_FRAME) {
            Chunk* c = getChunkAt(it->chunkX, it->chunkZ).get();
            if(c) {
                c->uploadVBOData(it->opaqueData, it->opaqueIdx,
                                 it->transparentData, it->transparentIdx);
                c->setVBOReady(true);
                m_vboInProgress.erase(toKey(it->chunkX, it->chunkZ));
                uploaded++;
            }
            ++it;
        }
        m_completedVBOs.erase(m_completedVBOs.begin(), it);  // 只删除已处理的
    }

    // ============================================================
    // 阶段 2：BlockTypeWorker 完成的 Chunk → 链接邻居 → 派发 VBOWorker
    // ============================================================
    {
        QMutexLocker lock(&m_chunksPendingVBOMutex);
        // 本帧已标脏的集合
        std::unordered_set<int64_t> dirtyThisFrame;

        for(Chunk* c : m_chunksPendingVBO) {
            int cx = c->getMinX();
            int cz = c->getMinZ();

            // 链接四个方向的邻居（如果邻居 Chunk 已存在）
            if(hasChunkAt(cx + 16, cz)) {
                auto& neighbor = getChunkAt(cx + 16, cz);
                c->linkNeighbor(neighbor, XPOS);
                // ★ 方案 B：邻居已有 VBO → 标记重建（新链接改变了面剔除结果）
                int64_t nk = toKey(cx + 16, cz);
                if(neighbor->hasVBO() && dirtyThisFrame.find(nk) == dirtyThisFrame.end()) {
                    dirtyThisFrame.insert(nk);   // 本帧不重复标脏
                    auto* nw = new VBOWorker(neighbor.get(), m_completedVBOs, m_completedVBOsMutex);
                    QThreadPool::globalInstance()->start(nw);
                    m_vboInProgress.insert(nk);  // 防止 Stage 3 重复派发
                }
            }
            if(hasChunkAt(cx - 16, cz)) {
                auto& neighbor = getChunkAt(cx - 16, cz);
                c->linkNeighbor(neighbor, XNEG);
                int64_t nk = toKey(cx - 16, cz);
                if(neighbor->hasVBO() && dirtyThisFrame.find(nk) == dirtyThisFrame.end()) {
                    dirtyThisFrame.insert(nk);   // 本帧不重复标脏
                    auto* nw = new VBOWorker(neighbor.get(), m_completedVBOs, m_completedVBOsMutex);
                    QThreadPool::globalInstance()->start(nw);
                    m_vboInProgress.insert(nk);  // 防止 Stage 3 重复派发
                }
            }
            if(hasChunkAt(cx, cz + 16)) {
                auto& neighbor = getChunkAt(cx, cz + 16);
                c->linkNeighbor(neighbor, ZPOS);
                int64_t nk = toKey(cx, cz + 16);
                if(neighbor->hasVBO() && dirtyThisFrame.find(nk) == dirtyThisFrame.end()) {
                    dirtyThisFrame.insert(nk);   // 本帧不重复标脏
                    auto* nw = new VBOWorker(neighbor.get(), m_completedVBOs, m_completedVBOsMutex);
                    QThreadPool::globalInstance()->start(nw);
                    m_vboInProgress.insert(nk);  // 防止 Stage 3 重复派发
                }
            }
            if(hasChunkAt(cx, cz - 16)) {
                auto& neighbor = getChunkAt(cx, cz - 16);
                c->linkNeighbor(neighbor, ZNEG);
                int64_t nk = toKey(cx, cz - 16);
                if(neighbor->hasVBO() && dirtyThisFrame.find(nk) == dirtyThisFrame.end()) {
                    dirtyThisFrame.insert(nk);   // 本帧不重复标脏
                    auto* nw = new VBOWorker(neighbor.get(), m_completedVBOs, m_completedVBOsMutex);
                    QThreadPool::globalInstance()->start(nw);
                    m_vboInProgress.insert(nk);  // 防止 Stage 3 重复派发
                }
            }

            auto* worker = new VBOWorker(c, m_completedVBOs, m_completedVBOsMutex);
            QThreadPool::globalInstance()->start(worker);
            // ★ 修复：Stage 2 派发的 VBOWorker 也要登记，防止 Stage 3 重复派发
            m_vboInProgress.insert(toKey(cx, cz));
        }
        m_chunksPendingVBO.clear();
    }

    // ============================================================
    // 阶段 3：扫描 5×5 Zone → 分类任务
    // ============================================================
    int playerZoneX = static_cast<int>(glm::floor(playerPos.x / 64.f)) * 64;
    int playerZoneZ = static_cast<int>(glm::floor(playerPos.z / 64.f)) * 64;

    // ---- 待派发队列（本 tick 暂存，后续换成直接创建线程） ----
    std::vector<glm::ivec2> blockTypeZoneQueue;  // Zone 坐标，派给 BlockTypeWorker
    std::vector<Chunk*>     vboWorkerQueue;       // Chunk 指针，派给 VBOWorker

    const int MAX_NEW_ZONES_PER_FRAME = 4;   // 每帧最多生成 4 个新 Zone
    int newZoneCount = 0;

    const int zoneRadius = 4;
    for(int dz = -zoneRadius; dz <= zoneRadius; ++dz) {
        for(int dx = -zoneRadius; dx <= zoneRadius; ++dx) {
            int zoneX = playerZoneX + dx * 64;
            int zoneZ = playerZoneZ + dz * 64;
            int64_t zoneKey = toKey(zoneX, zoneZ);

            if(m_generatedTerrain.find(zoneKey) == m_generatedTerrain.end()) {
                // ---- Zone 未生成 → 主线程立即创建 16 个空 Chunk 外壳 ----
                // ---- 新 Zone：只在未达上限时才创建 ----
                if(newZoneCount >= MAX_NEW_ZONES_PER_FRAME) continue;
                m_generatedTerrain.insert(zoneKey);
                newZoneCount++;                          // ★ 计数

                for(int cz = 0; cz < 64; cz += 16) {
                    for(int cx = 0; cx < 64; cx += 16) {
                        instantiateChunkAt(zoneX + cx, zoneZ + cz);
                    }
                }

                // 整个 Zone 派给 BlockTypeWorker 去填方块数据
                blockTypeZoneQueue.push_back(glm::ivec2(zoneX, zoneZ));
            }
            else {
                // ---- Zone 已生成 → 检查每个 Chunk 是否需要 VBO 重建 ----
                for(int cz = 0; cz < 64; cz += 16) {
                    for(int cx = 0; cx < 64; cx += 16) {
                        int chunkX = zoneX + cx;
                        int chunkZ = zoneZ + cz;
                        int64_t chunkKey = toKey(chunkX, chunkZ);

                        if(!hasChunkAt(chunkX, chunkZ)) continue;

                        Chunk* c = getChunkAt(chunkX, chunkZ).get();
                        if(!c->hasVBO()
                            && c->hasBlockData()                          // ← 新增：没方块数据不建 VBO
                            && m_vboInProgress.find(chunkKey) == m_vboInProgress.end()) {
                            vboWorkerQueue.push_back(c);
                            m_vboInProgress.insert(chunkKey);
                        }
                    }
                }
            }
        }
    }

    // ---- 派发 BlockTypeWorker ----
    for(auto& zone : blockTypeZoneQueue) {
        std::vector<Chunk*> zoneChunks;
        for(int cz = 0; cz < 64; cz += 16) {
            for(int cx = 0; cx < 64; cx += 16) {
                zoneChunks.push_back(
                    getChunkAt(zone.x + cx, zone.y + cz).get());
            }
        }
        auto* worker = new BlockTypeWorker(zone.x, zone.y, zoneChunks,
                                           m_chunksPendingVBO, m_chunksPendingVBOMutex);
        QThreadPool::globalInstance()->start(worker);
    }

    // ★ 修复：派发 vboWorkerQueue（已存在的 Chunk 需要 VBO 重建）
    for(Chunk* c : vboWorkerQueue) {
        auto* worker = new VBOWorker(c, m_completedVBOs, m_completedVBOsMutex);
        QThreadPool::globalInstance()->start(worker);
    }
    // 临时：打印本帧扫描结果
    /*if(!blockTypeZoneQueue.empty() || !vboWorkerQueue.empty()) {
        std::cout << "[tick] new zones: " << blockTypeZoneQueue.size()
        << " | vbo-needed: " << vboWorkerQueue.size() << std::endl;
    }*/
}