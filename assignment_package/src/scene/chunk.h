#pragma once
#include "smartpointerhelp.h"
#include "glm_includes.h"
#include <array>
#include <unordered_map>
#include <cstddef>
#include "drawable.h"

struct ChunkVBOData;
//using namespace std;

// C++ 11 allows us to define the size of an enum. This lets us use only one byte
// of memory to store our different block types. By default, the size of a C++ enum
// is that of an int (so, usually four bytes). This *does* limit us to only 256 different
// block types, but in the scope of this project we'll never get anywhere near that many.
//方块类型：空气，草，土，石头，水
enum BlockType : unsigned char
{
    EMPTY, GRASS, DIRT, STONE, WATER, SNOW, LAVA, BEDROCK, SAND,
    LOG, LEAVES, CACTUS, TALLGRASS, FLOWER //9-13
};

// The six cardinal directions in 3D space
//记录邻居位置
enum Direction : unsigned char
{
    XPOS, XNEG, YPOS, YNEG, ZPOS, ZNEG
};

// Lets us use any enum class as the key of a
//遇到枚举类型，把它强制转换成数字
// std::unordered_map
struct EnumHash {
    template <typename T>
    size_t operator()(T t) const {
        return static_cast<size_t>(t);
    }
};

// One Chunk is a 16 x 256 x 16 section of the world,
// containing all the Minecraft blocks in that area.
// We divide the world into Chunks in order to make
// recomputing its VBO data faster by not having to
// render all the world at once, while also not having
// to render the world block by block.

// TODO have Chunk inherit from Drawable
class Chunk : public InstancedDrawable{
private:
    // All of the blocks contained within this Chunk
    std::array<BlockType, 65536> m_blocks;//该chunk中所有块的类型
    // The coordinates of the chunk's lower-left corner in world space
    int minX, minZ;
    // This Chunk's four neighbors to the north, south, east, and west
    // The third input to this map just lets us use a Direction as
    // a key for this map.
    // These allow us to properly determine
    std::unordered_map<Direction, Chunk*, EnumHash> m_neighbors;

    bool m_vboReady = false;//当前chunk的VBO是否就绪
    bool m_blocksFilled = false;// ★ 方块数据是否已被 BlockTypeWorker 填充

    // ★ 面剔除循环（公共逻辑，被 createVBOdata 和 computeVBOData 共享）
    void buildVBOData(std::vector<GLfloat>& opaqueData,
                      std::vector<GLuint>&  opaqueIdx,
                      std::vector<GLfloat>& transparentData,
                      std::vector<GLuint>&  transparentIdx) const;

public:
    Chunk(OpenGLContext* context, int x, int z);
    BlockType getLocalBlockAt(unsigned int x, unsigned int y, unsigned int z) const;
    BlockType getLocalBlockAt(int x, int y, int z) const;
    void setLocalBlockAt(unsigned int x, unsigned int y, unsigned int z, BlockType t);
    void linkNeighbor(uPtr<Chunk>& neighbor, Direction dir);
    //覆盖 Drawable 的纯虚函数，交错
    void createVBOdata() override;
    //InstancedDrawable 纯虚函数，交错
    void createInstancedVBOdata(std::vector<glm::vec3> &offsets,
                                std::vector<glm::vec3> &colors) override;

    bool hasVBO() const { return m_vboReady; }
    void setVBOReady(bool ready) { m_vboReady = ready; }
    bool hasBlockData() const { return m_blocksFilled; }
    void setBlockDataFilled(bool v = true) { m_blocksFilled = v; }
    // 供 VBOWorker 读取方块数据
    const std::array<BlockType, 65536>& blocks() const { return m_blocks; }
    int getMinX() const { return minX; }       // ← 新增
    int getMinZ() const { return minZ; }       // ← 新增
    void uploadVBOData(const std::vector<GLfloat>&, const std::vector<GLuint>&,
                       const std::vector<GLfloat>&, const std::vector<GLuint>&);  // ← 新增
    // ★ 纯 CPU 计算 VBO 数据（子线程安全，不调 OpenGL）
    void computeVBOData(std::vector<GLfloat>& opaqueData,
                        std::vector<GLuint>&  opaqueIdx,
                        std::vector<GLfloat>& transparentData,
                        std::vector<GLuint>&  transparentIdx) const;
};
