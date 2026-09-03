#include "chunk.h"

// ============== 纹理图集配置 ==============
static const int ATLAS_ROWS = 16;     // 图集行数（纵向）
static const int ATLAS_COLS = 16;     // 图集列数（横向）

// ★ 透明方块判定
static bool isOpaque(BlockType t) {
    return t != EMPTY && t != WATER && t != TALLGRASS && t != FLOWER && t != CACTUS;
}
// ★ cross 模型植物（两个对角线 quad）
static bool isCrossPlant(BlockType t) {
    return t == TALLGRASS || t == FLOWER;
}
// ★ 仙人掌：官方模型不是简单的 14×14 方盒，而是"互锁"结构
//   - 四个侧面只沿法线方向内缩 1/16，切向保持满 16（相邻面在角部互相咬合）
//   - 顶/底面保持满 16×16（底面盖住基座缝隙，顶面形成带刺的"帽檐"）
inline glm::vec3 cactusInset(Direction dir, const glm::vec3& c) {
    const float i = 1.f / 16.f;
    glm::vec3 r = c;
    switch(dir) {
    case XPOS: r.x = 1.f - i; break;   // 东侧面推到 x=15/16，z 不动（满宽）
    case XNEG: r.x = i;       break;   // 西侧面推到 x=1/16
    case ZPOS: r.z = 1.f - i; break;   // 南侧面推到 z=15/16
    case ZNEG: r.z = i;       break;   // 北侧面推到 z=1/16
    default:   break;                  // YPOS / YNEG 不动（满 16×16）
    }
    return r;
}


// ============== 6 个面的数据定义（顶点顺序 UR/LR/LL/UL，与 Cube 一致） ==============
struct FaceData {
    Direction dir;
    glm::vec4 normal;
    glm::vec3 corners[4];   // 相对于方块最小角的偏移
};

// ---------- 工具函数：(行, 列) 转 UV ----------
// row, col 从 0 开始计数
// 返回 u0,v0（左下角）和 u1,v1（右上角）
inline void atlasToUV(int row, int col,
                      float& u0, float& v0,
                      float& u1, float& v1) {
    u0 = col / (float)ATLAS_COLS;
    u1 = (col + 1) / (float)ATLAS_COLS;
    // V 翻转：row 0 在纹理图集顶部（y=1），越往下 v 越小
    v0 = 1.0f - (row + 1) / (float)ATLAS_ROWS;
    v1 = 1.0f - row / (float)ATLAS_ROWS;
}

// ============== 每方块 × 6 面  →  图集格子(行, 列) ==============
// Direction 枚举顺序：XPOS=0, XNEG=1, YPOS=2, YNEG=3, ZPOS=4, ZNEG=5
//
//  格式：{ {X+, X-, Y+, Y-, Z+, Z- } }
//  行/列 从 0 开始，你直接改成自己的数字即可。

static const std::array<glm::ivec2, 6> BLOCK_FACE_ATLAS[] = {
    // [0] EMPTY — 不会被渲染，占位即可
    { glm::ivec2(0,0), glm::ivec2(0,0), glm::ivec2(0,0),
     glm::ivec2(0,0), glm::ivec2(0,0), glm::ivec2(0,0) },

    // [1] GRASS
    { glm::ivec2(0,3), glm::ivec2(0,3),   // 侧面 X±
     glm::ivec2(2,8),                     // 顶面 Y+
     glm::ivec2(0,2),                     // 底面 Y-
     glm::ivec2(0,3), glm::ivec2(0,3) }, // 侧面 Z±

    // [2] DIRT
    { glm::ivec2(0,2), glm::ivec2(0,2), glm::ivec2(0,2),
     glm::ivec2(0,2), glm::ivec2(0,2), glm::ivec2(0,2) },

    // [3] STONE
    { glm::ivec2(0,1), glm::ivec2(0,1), glm::ivec2(0,1),
     glm::ivec2(0,1), glm::ivec2(0,1), glm::ivec2(0,1) },

    // [4] WATER
    { glm::ivec2(13,14), glm::ivec2(13,14), glm::ivec2(13,14),
     glm::ivec2(13,14), glm::ivec2(13,14), glm::ivec2(13,14) },

    // [5] SNOW
    { glm::ivec2(4,2), glm::ivec2(4,2), glm::ivec2(4,2),
     glm::ivec2(4,2), glm::ivec2(4,2), glm::ivec2(4,2) },

    // ★ [6] LAVA — 你自己改数字
    { glm::ivec2(15,15), glm::ivec2(15,15), glm::ivec2(15,15),
     glm::ivec2(15,15), glm::ivec2(15,15), glm::ivec2(15,15) },

    // ★ [7] BEDROCK — 你自己改数字
    { glm::ivec2(1,1), glm::ivec2(1,1), glm::ivec2(1,1),
     glm::ivec2(1,1), glm::ivec2(1,1), glm::ivec2(1,1) },

    //  [8] SAND
    { glm::ivec2(1,2), glm::ivec2(1,2), glm::ivec2(1,2),
     glm::ivec2(1,2), glm::ivec2(1,2), glm::ivec2(1,2) },

    // [9] LOG — 顶/底用年轮纹理，四侧用树皮
    { glm::ivec2(1, 4), glm::ivec2(1, 4), glm::ivec2(1, 5),
     glm::ivec2(1, 5), glm::ivec2(1, 4), glm::ivec2(1, 4) },

    // [10] LEAVES — 六面同图
    { glm::ivec2(3, 4), glm::ivec2(3, 4), glm::ivec2(3, 4),
     glm::ivec2(3, 4), glm::ivec2(3, 4), glm::ivec2(3, 4) },

    // [11] CACTUS — 顶/底同图（仙人掌顶），四侧用带刺侧面
    { glm::ivec2(4, 6), glm::ivec2(4, 6),glm::ivec2(4, 5),
     glm::ivec2(4, 5), glm::ivec2(4, 6), glm::ivec2(4, 6) },

    // [12] TALLGRASS
    { glm::ivec2(2, 7), glm::ivec2(2, 7),glm::ivec2(2, 7),
     glm::ivec2(2, 7), glm::ivec2(2, 7), glm::ivec2(2, 7) },

    // [13] FLOWER — 先随便填个格子占位，Step 2 才真正用
    { glm::ivec2(0, 12), glm::ivec2(0, 12),glm::ivec2(0, 12),
     glm::ivec2(0, 12), glm::ivec2(0, 12), glm::ivec2(0, 12) }

    };

static const FaceData faceDefs[6] = {//面的数据
    // XPOS — 右面 (+X)
    {XPOS, glm::vec4( 1, 0, 0, 0),
     {glm::vec3(1,1,1), glm::vec3(1,1,0), glm::vec3(1,0,0), glm::vec3(1,0,1)}},
    // XNEG — 左面 (-X)
    {XNEG, glm::vec4(-1, 0, 0, 0),
     {glm::vec3(0,1,0), glm::vec3(0,1,1), glm::vec3(0,0,1), glm::vec3(0,0,0)}},
    // YPOS — 顶面 (+Y)
    {YPOS, glm::vec4( 0, 1, 0, 0),
     {glm::vec3(0,1,0), glm::vec3(1,1,0), glm::vec3(1,1,1), glm::vec3(0,1,1)}},
    // YNEG — 底面 (-Y)
    {YNEG, glm::vec4( 0,-1, 0, 0),
     {glm::vec3(0,0,1), glm::vec3(1,0,1), glm::vec3(1,0,0), glm::vec3(0,0,0)}},
    // ZPOS — 前面 (+Z)
    {ZPOS, glm::vec4( 0, 0, 1, 0),
     {glm::vec3(0,1,1), glm::vec3(1,1,1), glm::vec3(1,0,1), glm::vec3(0,0,1)}},
    // ZNEG — 后面 (-Z)
    {ZNEG, glm::vec4( 0, 0,-1, 0),
     {glm::vec3(1,1,0), glm::vec3(0,1,0), glm::vec3(0,0,0), glm::vec3(1,0,0)}}
};

// ============== 方块类型 → 颜色 ==============
static glm::vec4 blockColor(BlockType t) {
    float anim = (t == WATER || t == LAVA) ? 1.0f : 0.0f;  // ★ alpha=1 表示需要动画
    switch(t) {
    case GRASS: return glm::vec4( 95.f/255, 159.f/255,  53.f/255, anim);
    case DIRT:  return glm::vec4(121.f/255,  85.f/255,  58.f/255, anim);
    case STONE: return glm::vec4(0.5f, 0.5f, 0.5f, anim);
    case WATER: return glm::vec4(0.f, 0.f, 0.75f, anim);
    case SNOW:  return glm::vec4(1.f, 1.f, 1.f, anim);
    case LAVA:  return glm::vec4(1.f, 0.4f, 0.f, anim);
    case BEDROCK: return glm::vec4(0.2f, 0.2f, 0.2f, anim);
    case SAND: return glm::vec4(0.7f, 0.9f, 0.9f, anim);
    case LOG:      return glm::vec4(1.f, 1.f, 1.f, anim);
    case LEAVES:   return glm::vec4(1.f, 1.f, 1.f, anim);
    case CACTUS:   return glm::vec4(1.f, 1.f, 1.f, anim);
    case TALLGRASS: return glm::vec4(1.f, 1.f, 1.f, anim);
    case FLOWER:   return glm::vec4(1.f, 1.f, 1.f, anim);

    default:    return glm::vec4(1.f, 0.f, 1.f, anim);
    }
}

Chunk::Chunk(OpenGLContext* context, int x, int z) : InstancedDrawable(context), m_blocks(), minX(x), minZ(z), m_neighbors{{XPOS, nullptr}, {XNEG, nullptr}, {ZPOS, nullptr}, {ZNEG, nullptr}}
{
    std::fill_n(m_blocks.begin(), 65536, EMPTY);
}

// Does bounds checking with at()
BlockType Chunk::getLocalBlockAt(unsigned int x, unsigned int y, unsigned int z) const {
    return m_blocks.at(x + 16 * y + 16 * 256 * z);
}

// Exists to get rid of compiler warnings about int -> unsigned int implicit conversion
BlockType Chunk::getLocalBlockAt(int x, int y, int z) const {
    return getLocalBlockAt(static_cast<unsigned int>(x), static_cast<unsigned int>(y), static_cast<unsigned int>(z));
}

// Does bounds checking with at()
void Chunk::setLocalBlockAt(unsigned int x, unsigned int y, unsigned int z, BlockType t) {
    m_blocks.at(x + 16 * y + 16 * 256 * z) = t;
}


const static std::unordered_map<Direction, Direction, EnumHash> oppositeDirection {
    {XPOS, XNEG},
    {XNEG, XPOS},
    {YPOS, YNEG},
    {YNEG, YPOS},
    {ZPOS, ZNEG},
    {ZNEG, ZPOS}
};

void Chunk::linkNeighbor(uPtr<Chunk> &neighbor, Direction dir) {
    if(neighbor != nullptr) {
        this->m_neighbors[dir] = neighbor.get();
        neighbor->m_neighbors[oppositeDirection.at(dir)] = this;
    }
}

// ★ 纯 CPU 的面剔除循环（createVBOdata 和 computeVBOData 共用）
void Chunk::buildVBOData(std::vector<GLfloat>& opaqueData,
                         std::vector<GLuint>&  opaqueIdx,
                         std::vector<GLfloat>& transparentData,
                         std::vector<GLuint>&  transparentIdx) const {
    for(int z = 0; z < 16; ++z) {
        for(int y = 0; y < 256; ++y) {
            for(int x = 0; x < 16; ++x) {

                BlockType curr = getLocalBlockAt(x, y, z);
                if(curr == EMPTY) continue;

                glm::vec3 worldBase(x + minX, y, z + minZ);

                // ★ cross 植物：两条对角线竖直 quad，正反绕序各一份 → 写入【不透明】VBO
                //   放不透明 VBO 的原因：1) 有深度写入，被山体正确遮挡 2) 阴影 pass 只画不透明 VBO
                if(isCrossPlant(curr)) {
                    glm::ivec2 cell = BLOCK_FACE_ATLAS[curr][0];   // 随便取一个槽位的图集坐标
                    float u0, v0, u1, v1;
                    atlasToUV(cell.x, cell.y, u0, v0, u1, v1);
                    glm::vec4 col = blockColor(curr);
                    glm::vec4 nor(0.f, 1.f, 0.f, 0.f);   // 法线朝上：植物像地面一样受光（MC 同款做法）

                    // 顶点顺序：顶左、顶右、底右、底左 —— 与 uv 数组一一对应，保证贴图不倒
                    glm::vec3 quads[2][4] = {
                        { worldBase + glm::vec3(0,1,0), worldBase + glm::vec3(1,1,1),   // 对角线 A：(0,0)→(1,1)
                         worldBase + glm::vec3(1,0,1), worldBase + glm::vec3(0,0,0) },
                        { worldBase + glm::vec3(1,1,0), worldBase + glm::vec3(0,1,1),   // 对角线 B：(1,0)→(0,1)
                         worldBase + glm::vec3(0,0,1), worldBase + glm::vec3(1,0,0) }
                    };
                    glm::vec2 uvs[4] = { glm::vec2(u0,v1), glm::vec2(u1,v1),
                                        glm::vec2(u1,v0), glm::vec2(u0,v0) };

                    for(int q = 0; q < 2; ++q) {
                        for(int side = 0; side < 2; ++side) {   // 0=正面绕序 1=反面绕序（应对背面剔除）
                            GLuint base = static_cast<GLuint>(opaqueData.size() / 14);
                            for(int v = 0; v < 4; ++v) {
                                glm::vec4 pos(quads[q][v], 1.f);
                                opaqueData.push_back(pos.x); opaqueData.push_back(pos.y);
                                opaqueData.push_back(pos.z); opaqueData.push_back(pos.w);
                                opaqueData.push_back(nor.x);  opaqueData.push_back(nor.y);
                                opaqueData.push_back(nor.z);  opaqueData.push_back(nor.w);
                                opaqueData.push_back(col.r);   opaqueData.push_back(col.g);
                                opaqueData.push_back(col.b);   opaqueData.push_back(col.a);
                                opaqueData.push_back(uvs[v].x);opaqueData.push_back(uvs[v].y);
                            }
                            if(side == 0) {
                                opaqueIdx.insert(opaqueIdx.end(),
                                                 {base, base+1, base+2, base, base+2, base+3});
                            } else {
                                opaqueIdx.insert(opaqueIdx.end(),
                                                 {base, base+2, base+1, base, base+3, base+2});
                            }
                        }
                    }
                    continue;   // 跳过普通 6 面生成
                }


                for(int f = 0; f < 6; ++f) {
                    const FaceData& fd = faceDefs[f];
                    BlockType adj = EMPTY;

                    switch(fd.dir) {
                    case XPOS:
                        if(x == 15) {
                            auto it = m_neighbors.find(XPOS);
                            if(it != m_neighbors.end() && it->second)
                                adj = it->second->getLocalBlockAt(0, y, z);
                            // else: adj 保持 EMPTY（邻居未生成 = 当作空气）
                        } else adj = getLocalBlockAt(x+1, y, z);
                        break;
                    case XNEG:
                        if(x == 0) {
                            auto it = m_neighbors.find(XNEG);
                            if(it != m_neighbors.end() && it->second)
                                adj = it->second->getLocalBlockAt(15, y, z);
                        } else adj = getLocalBlockAt(x-1, y, z);
                        break;
                    case YPOS:
                        adj = (y == 255) ? EMPTY : getLocalBlockAt(x, y+1, z);
                        break;
                    case YNEG:
                        adj = (y == 0)   ? EMPTY : getLocalBlockAt(x, y-1, z);
                        break;
                    case ZPOS:
                        if(z == 15) {
                            auto it = m_neighbors.find(ZPOS);
                            if(it != m_neighbors.end() && it->second)
                                adj = it->second->getLocalBlockAt(x, y, 0);
                        } else adj = getLocalBlockAt(x, y, z+1);
                        break;
                    case ZNEG:
                        if(z == 0) {
                            auto it = m_neighbors.find(ZNEG);
                            if(it != m_neighbors.end() && it->second)
                                adj = it->second->getLocalBlockAt(x, y, 15);
                        } else adj = getLocalBlockAt(x, y, z-1);
                        break;
                    }

                    // ========== 面剔除判断 ==========
                    bool currOp = isOpaque(curr);
                    bool renderFront = true;

                    if(adj != EMPTY) {
                        if(curr == adj && (curr == WATER || curr == LAVA))
                            renderFront = false;
                        else if(currOp && isOpaque(adj))
                            renderFront = false;
                        else if(!currOp && isOpaque(adj))
                            renderFront = false;
                    }
                    //仙人掌不参与共面剔除
                    // ★ 仙人掌之间（上下堆叠或左右相邻）互相剔除，避免共面 z-fighting
                    if(curr == CACTUS && adj == CACTUS) renderFront = false;

                    glm::vec4 col = blockColor(curr);
                    glm::ivec2 atlasCell = BLOCK_FACE_ATLAS[curr][f];
                    float u0, v0, u1, v1;
                    atlasToUV(atlasCell.x, atlasCell.y, u0, v0, u1, v1);
                    glm::vec2 uvs[4] = {
                        glm::vec2(u0, v1),
                        glm::vec2(u1, v1),
                        glm::vec2(u1, v0),
                        glm::vec2(u0, v0)
                    };

                    // ========== 正面 ==========
                    if(renderFront) {
                        // 仙人掌面不满块但材质不透明 → 仍进不透明 VBO（写深度、参与阴影）
                        bool toOpaqueVBO = currOp || (curr == CACTUS);
                        std::vector<GLfloat>& targetData = toOpaqueVBO ? opaqueData : transparentData;
                        std::vector<GLuint>&  targetIdx  = toOpaqueVBO ? opaqueIdx   : transparentIdx;


                        GLuint baseIdx = static_cast<GLuint>(targetData.size() / 14);

                        for(int v = 0; v < 4; ++v) {
                            glm::vec3 corner = (curr == CACTUS) ? cactusInset(fd.dir, fd.corners[v]) : fd.corners[v];
                            glm::vec4 pos = glm::vec4(worldBase + corner, 1.0f);
                            targetData.push_back(pos.x);
                            targetData.push_back(pos.y);
                            targetData.push_back(pos.z);
                            targetData.push_back(pos.w);
                            targetData.push_back(fd.normal.x);
                            targetData.push_back(fd.normal.y);
                            targetData.push_back(fd.normal.z);
                            targetData.push_back(fd.normal.w);
                            targetData.push_back(col.r);
                            targetData.push_back(col.g);
                            targetData.push_back(col.b);
                            targetData.push_back(col.a);
                            targetData.push_back(uvs[v].x);
                            targetData.push_back(uvs[v].y);
                        }

                        targetIdx.push_back(baseIdx);
                        targetIdx.push_back(baseIdx + 1);
                        targetIdx.push_back(baseIdx + 2);
                        targetIdx.push_back(baseIdx);
                        targetIdx.push_back(baseIdx + 2);
                        targetIdx.push_back(baseIdx + 3);

                        // ★ 仙人掌侧面双面渲染：斜 45° 看角部缺口时，对面侧壁要透过缺口可见
                        //   （MC 不开背面剔除所以天然双面；我们引擎全局开了剔除，需补反向绕序）
                        bool cactusSide = (curr == CACTUS)
                                          && (fd.dir == XPOS || fd.dir == XNEG || fd.dir == ZPOS || fd.dir == ZNEG);
                        if(cactusSide) {
                            GLuint revBase = static_cast<GLuint>(targetData.size() / 14);
                            for(int v = 0; v < 4; ++v) {
                                glm::vec3 corner = cactusInset(fd.dir, fd.corners[v]);
                                glm::vec4 pos = glm::vec4(worldBase + corner, 1.0f);
                                targetData.push_back(pos.x);  targetData.push_back(pos.y);
                                targetData.push_back(pos.z);  targetData.push_back(pos.w);
                                targetData.push_back(col.r);  targetData.push_back(col.g);
                                targetData.push_back(col.b);  targetData.push_back(col.a);
                                targetData.push_back(fd.normal.x);  targetData.push_back(fd.normal.y);
                                targetData.push_back(fd.normal.z);  targetData.push_back(fd.normal.w);
                                targetData.push_back(uvs[v].x); targetData.push_back(uvs[v].y);
                            }
                            // 反向绕序的索引（与 cross 植物的反面绕序写法一致）
                            targetIdx.push_back(revBase + 0);
                            targetIdx.push_back(revBase + 2);
                            targetIdx.push_back(revBase + 1);
                            targetIdx.push_back(revBase + 0);
                            targetIdx.push_back(revBase + 3);
                            targetIdx.push_back(revBase + 2);
                        }

                    }


                    // ========== 液体反面 ==========
                    bool needBackFace = false;
                    bool backIsOpaque = false;

                    if(curr == LAVA && adj != LAVA) {
                        needBackFace = true;
                        backIsOpaque = true;
                    } else if(curr == WATER && adj == EMPTY) {
                        needBackFace = true;
                        backIsOpaque = false;
                    }

                    if(needBackFace) {
                        std::vector<GLfloat>& backData = backIsOpaque ? opaqueData : transparentData;
                        std::vector<GLuint>&  backIdx  = backIsOpaque ? opaqueIdx   : transparentIdx;

                        glm::vec4 revNormal = glm::vec4(
                            -fd.normal.x, -fd.normal.y, -fd.normal.z, fd.normal.w);

                        GLuint revBaseIdx = static_cast<GLuint>(backData.size() / 14);

                        for(int v = 0; v < 4; ++v) {
                            glm::vec4 pos = glm::vec4(worldBase + fd.corners[v], 1.0f);
                            backData.push_back(pos.x);
                            backData.push_back(pos.y);
                            backData.push_back(pos.z);
                            backData.push_back(pos.w);
                            backData.push_back(revNormal.x);
                            backData.push_back(revNormal.y);
                            backData.push_back(revNormal.z);
                            backData.push_back(revNormal.w);
                            backData.push_back(col.r);
                            backData.push_back(col.g);
                            backData.push_back(col.b);
                            backData.push_back(col.a);
                            backData.push_back(uvs[v].x);
                            backData.push_back(uvs[v].y);
                        }

                        backIdx.push_back(revBaseIdx);
                        backIdx.push_back(revBaseIdx + 2);
                        backIdx.push_back(revBaseIdx + 1);
                        backIdx.push_back(revBaseIdx);
                        backIdx.push_back(revBaseIdx + 3);
                        backIdx.push_back(revBaseIdx + 2);
                    }
                }
            }
        }
    }
}

// ============== 面剔除交错 VBO 生成 ==============
void Chunk::createVBOdata() {
    std::vector<GLfloat> opaqueData, transparentData;   // 存放 pos+ nor+ col 的交错数据
    std::vector<GLuint>  opaqueIdx,   transparentIdx;    // 三角形索引

    // ★ 调共享逻辑
    buildVBOData(opaqueData, opaqueIdx, transparentData, transparentIdx);

    // ---- 上传不透明 VBO ----
    indexCounts[INDEX] = static_cast<int>(opaqueIdx.size());
    generateBuffer(INDEX);
    bindBuffer(INDEX);
    mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             opaqueIdx.size() * sizeof(GLuint),
                             opaqueIdx.data(), GL_STATIC_DRAW);
    generateBuffer(INTERLEAVED);
    bindBuffer(INTERLEAVED);
    mp_context->glBufferData(GL_ARRAY_BUFFER,
                             opaqueData.size() * sizeof(GLfloat),
                             opaqueData.data(), GL_STATIC_DRAW);

    // ---- 上传透明 VBO ----
    indexCounts[INDEX_TRANSPARENT] = static_cast<int>(transparentIdx.size());
    generateBuffer(INDEX_TRANSPARENT);
    bindBuffer(INDEX_TRANSPARENT);
    mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             transparentIdx.size() * sizeof(GLuint),
                             transparentIdx.data(), GL_STATIC_DRAW);
    generateBuffer(INTERLEAVED_TRANSPARENT);
    bindBuffer(INTERLEAVED_TRANSPARENT);
    mp_context->glBufferData(GL_ARRAY_BUFFER,
                             transparentData.size() * sizeof(GLfloat),
                             transparentData.data(), GL_STATIC_DRAW);
    m_vboReady = true;
}

void Chunk::uploadVBOData(const std::vector<GLfloat>& opaqueData,
                          const std::vector<GLuint>&  opaqueIdx,
                          const std::vector<GLfloat>& transparentData,
                          const std::vector<GLuint>&  transparentIdx) {
    // ★ 如果已有旧 buffer，先释放
    if(bufGenerated[INDEX]) {
        mp_context->glDeleteBuffers(1, &bufHandles[INDEX]);
        mp_context->glDeleteBuffers(1, &bufHandles[INTERLEAVED]);
    }
    if(bufGenerated[INDEX_TRANSPARENT]) {
        mp_context->glDeleteBuffers(1, &bufHandles[INDEX_TRANSPARENT]);
        mp_context->glDeleteBuffers(1, &bufHandles[INTERLEAVED_TRANSPARENT]);
    }

    // ---- 上传不透明 VBO ----
    indexCounts[INDEX] = static_cast<int>(opaqueIdx.size());
    generateBuffer(INDEX);
    bindBuffer(INDEX);
    mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             opaqueIdx.size() * sizeof(GLuint),
                             opaqueIdx.data(), GL_STATIC_DRAW);
    generateBuffer(INTERLEAVED);
    bindBuffer(INTERLEAVED);
    mp_context->glBufferData(GL_ARRAY_BUFFER,
                             opaqueData.size() * sizeof(GLfloat),
                             opaqueData.data(), GL_STATIC_DRAW);

    // ---- 上传透明 VBO ----
    indexCounts[INDEX_TRANSPARENT] = static_cast<int>(transparentIdx.size());
    generateBuffer(INDEX_TRANSPARENT);
    bindBuffer(INDEX_TRANSPARENT);
    mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             transparentIdx.size() * sizeof(GLuint),
                             transparentIdx.data(), GL_STATIC_DRAW);
    generateBuffer(INTERLEAVED_TRANSPARENT);
    bindBuffer(INTERLEAVED_TRANSPARENT);
    mp_context->glBufferData(GL_ARRAY_BUFFER,
                             transparentData.size() * sizeof(GLfloat),
                             transparentData.data(), GL_STATIC_DRAW);
}

// ============== InstancedDrawable 纯虚函数占位 ==============
void Chunk::createInstancedVBOdata(std::vector<glm::vec3> &offsets,
                                   std::vector<glm::vec3> &colors) {
    // 当前 Chunk 使用 createVBOdata() + drawInterleaved()
    // 此函数仅在后续需要实例化渲染时实现
}

void Chunk::computeVBOData(std::vector<GLfloat>& opaqueData,
                           std::vector<GLuint>&  opaqueIdx,
                           std::vector<GLfloat>& transparentData,
                           std::vector<GLuint>&  transparentIdx) const {
    buildVBOData(opaqueData, opaqueIdx, transparentData, transparentIdx);
}