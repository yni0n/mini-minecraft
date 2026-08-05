#include "chunk.h"

// ============== 纹理图集配置 ==============
static const int ATLAS_ROWS = 16;     // 图集行数（纵向）
static const int ATLAS_COLS = 16;     // 图集列数（横向）

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
    { glm::ivec2(0,14), glm::ivec2(0,14), glm::ivec2(0,14),
     glm::ivec2(0,14), glm::ivec2(0,14), glm::ivec2(0,14) },

    // [5] SNOW
    { glm::ivec2(4,2), glm::ivec2(4,2), glm::ivec2(4,2),
     glm::ivec2(4,2), glm::ivec2(4,2), glm::ivec2(4,2) },

    // ★ [6] LAVA — 你自己改数字
    { glm::ivec2(15,15), glm::ivec2(15,15), glm::ivec2(15,15),
     glm::ivec2(15,15), glm::ivec2(15,15), glm::ivec2(15,15) },

    // ★ [7] BEDROCK — 你自己改数字
    { glm::ivec2(0,7), glm::ivec2(0,7), glm::ivec2(0,7),
     glm::ivec2(0,7), glm::ivec2(0,7), glm::ivec2(0,7) },
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
    switch(t) {
    case GRASS: return glm::vec4( 95.f/255, 159.f/255,  53.f/255, 1.f);
    case DIRT:  return glm::vec4(121.f/255,  85.f/255,  58.f/255, 1.f);
    case STONE: return glm::vec4(0.5f, 0.5f, 0.5f, 1.f);
    case WATER: return glm::vec4(0.f, 0.f, 0.75f, 1.f);
    case SNOW:  return glm::vec4(1.f, 1.f, 1.f, 1.f);
    case LAVA:    return glm::vec4(1.f, 0.4f, 0.f, 1.f);   // ★ 新增
    case BEDROCK: return glm::vec4(0.2f, 0.2f, 0.2f, 1.f); // ★ 新增
    default:    return glm::vec4(1.f, 0.f, 1.f, 1.f);   // debug 紫色
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

// ============== 面剔除交错 VBO 生成 ==============
void Chunk::createVBOdata() {
    std::vector<GLfloat> interleaved;   // 存放 pos+ nor+ col 的交错数据
    std::vector<GLuint>  indices;        // 三角形索引

    for(int z = 0; z < 16; ++z) {
        for(int y = 0; y < 256; ++y) {
            for(int x = 0; x < 16; ++x) {

                BlockType curr = getLocalBlockAt(x, y, z);//遍历当前的cube
                if(curr == EMPTY) continue;

                glm::vec3 worldBase(x + minX, y, z + minZ);//世界坐标

                // 检查 6 个面
                for(int f = 0; f < 6; ++f) {
                    const FaceData& fd = faceDefs[f]; //提取每个面
                    BlockType adj = EMPTY; //获取当前面的邻居类型

                    switch(fd.dir) { //如果在边界则视为空气
                    case XPOS:
                        if(x == 15) {//边界
                            auto it = m_neighbors.find(XPOS);//获取该方向的邻居chunk
                            adj = (it != m_neighbors.end() && it->second)
                                      ? it->second->getLocalBlockAt(0, y, z) : EMPTY;
                        } else adj = getLocalBlockAt(x+1, y, z);
                        break;
                    case XNEG:
                        if(x == 0) {
                            auto it = m_neighbors.find(XNEG);
                            adj = (it != m_neighbors.end() && it->second)
                                      ? it->second->getLocalBlockAt(15, y, z) : EMPTY;
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
                            adj = (it != m_neighbors.end() && it->second)
                                      ? it->second->getLocalBlockAt(x, y, 0) : EMPTY;
                        } else adj = getLocalBlockAt(x, y, z+1);
                        break;
                    case ZNEG:
                        if(z == 0) {
                            auto it = m_neighbors.find(ZNEG);
                            adj = (it != m_neighbors.end() && it->second)
                                      ? it->second->getLocalBlockAt(x, y, 15) : EMPTY;
                        } else adj = getLocalBlockAt(x, y, z-1);
                        break;
                    }

                    // 邻块不是空气 → 该面被遮挡，跳过
                    if(adj != EMPTY) continue;

                    // 生成该面的 4 个顶点 + 6 个索引，每顶点 14 个 float
                    GLuint baseIdx = static_cast<GLuint>(interleaved.size() / 14);
                    glm::vec4 col = blockColor(curr);

                    // ★ 从图集查表获取该方块该面的纹理格子
                    glm::ivec2 atlasCell = BLOCK_FACE_ATLAS[curr][f];
                    float u0, v0, u1, v1;
                    atlasToUV(atlasCell.x, atlasCell.y, u0, v0, u1, v1);

                    // 四个顶点的 UV（顺序：TL/TR/BR/BL，与 corners 一致）
                    glm::vec2 uvs[4] = {
                        glm::vec2(u0, v1),   // top-left
                        glm::vec2(u1, v1),   // top-right
                        glm::vec2(u1, v0),   // bottom-right
                        glm::vec2(u0, v0)    // bottom-left
                    };

                    for(int v = 0; v < 4; ++v) {
                        glm::vec4 pos = glm::vec4(worldBase + fd.corners[v], 1.0f);
                        // 每个顶点 12 个 float：pos(4) + nor(4) + col(4)
                        interleaved.push_back(pos.x);
                        interleaved.push_back(pos.y);
                        interleaved.push_back(pos.z);
                        interleaved.push_back(pos.w);
                        interleaved.push_back(fd.normal.x);
                        interleaved.push_back(fd.normal.y);
                        interleaved.push_back(fd.normal.z);
                        interleaved.push_back(fd.normal.w);
                        interleaved.push_back(col.r);
                        interleaved.push_back(col.g);
                        interleaved.push_back(col.b);
                        interleaved.push_back(col.a);
                        // ★ 新增：UV
                        interleaved.push_back(uvs[v].x);
                        interleaved.push_back(uvs[v].y);
                    }

                    indices.push_back(baseIdx);
                    indices.push_back(baseIdx + 1);
                    indices.push_back(baseIdx + 2);
                    indices.push_back(baseIdx);
                    indices.push_back(baseIdx + 2);
                    indices.push_back(baseIdx + 3);
                }
            }
        }
    }

    // ---- 上传 GPU ----
    indexCounts[INDEX] = static_cast<int>(indices.size());

    generateBuffer(INDEX);
    bindBuffer(INDEX);
    mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             indices.size() * sizeof(GLuint),
                             indices.data(), GL_STATIC_DRAW);

    generateBuffer(INTERLEAVED);
    bindBuffer(INTERLEAVED);
    mp_context->glBufferData(GL_ARRAY_BUFFER,
                             interleaved.size() * sizeof(GLfloat),
                             interleaved.data(), GL_STATIC_DRAW);
}

// ============== InstancedDrawable 纯虚函数占位 ==============
void Chunk::createInstancedVBOdata(std::vector<glm::vec3> &offsets,
                                   std::vector<glm::vec3> &colors) {
    // 当前 Chunk 使用 createVBOdata() + drawInterleaved()
    // 此函数仅在后续需要实例化渲染时实现
}
