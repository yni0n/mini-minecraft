#pragma once
#include "smartpointerhelp.h"
#include "glm_includes.h"
#include "chunk.h"
#include <array>
#include <QMutex>
#include <unordered_map>
#include <unordered_set>
#include "shaderprogram.h"
#include "cube.h"


//using namespace std;

// Helper functions to convert (x, z) to and from hash map key
int64_t toKey(int x, int z);
glm::ivec2 toCoords(int64_t k);

// VBOWorker 的输出：准备好上传的 VBO 数据
struct ChunkVBOData {
    int chunkX, chunkZ;
    std::vector<GLfloat> opaqueData;
    std::vector<GLuint>  opaqueIdx;
    std::vector<GLfloat> transparentData;
    std::vector<GLuint>  transparentIdx;
};

struct RaycastResult {
    bool hit;             // 是否命中方块
    glm::ivec3 blockPos;  // 命中方块的坐标
    Direction faceNormal; // 命中面的法线方向（用于确定放置位置）
};

// The container class for all of the Chunks in the game.
// Ultimately, while Terrain will always store all Chunks,
// not all Chunks will be drawn at any given time as the world
// expands.
class Terrain {
private:
    // Stores every Chunk according to the location of its lower-left corner
    // in world space.
    // We combine the X and Z coordinates of the Chunk's corner into one 64-bit int
    // so that we can use them as a key for the map, as objects like std::pairs or
    // glm::ivec2s are not hashable by default, so they cannot be used as keys.
    std::unordered_map<int64_t, uPtr<Chunk>> m_chunks; //所有区块的集合，使用64位，分别存储x和z

    // We will designate every 64 x 64 area of the world's x-z plane
    // as one "terrain generation zone". Every time the player moves
    // near a portion of the world that has not yet been generated
    // (i.e. its lower-left coordinates are not in this set), a new
    // 4 x 4 collection of Chunks is created to represent that area
    // of the world.
    // The world that exists when the base code is run consists of exactly
    // one 64 x 64 area with its lower-left corner at (0, 0).
    // When milestone 1 has been implemented, the Player can move around the
    // world to add more "terrain generation zone" IDs to this set.
    // While only the 3 x 3 collection of terrain generation zones
    // surrounding the Player should be rendered, the Chunks
    // in the Terrain will never be deleted until the program is terminated.
    std::unordered_set<int64_t> m_generatedTerrain; //记录已生成的Zone

    // TODO: DELETE ALL REFERENCES TO m_geomCube AS YOU WILL NOT USE
    // IT IN YOUR FINAL PROGRAM!
    // The instance of a unit cube we can use to render any cube.
    // Presently, Terrain::draw renders one instance of this cube
    // for every non-EMPTY block within its Chunks. This is horribly
    // inefficient, and will cause your game to run very slowly until
    // milestone 1's Chunk VBO setup is completed.
    Cube m_geomCube;

    // Set this to "true" whenever you modify the blocks
    // in your terrain. NOT NEEDED ONCE MILESTONE 1's CHUNKING
    // IS IMPLEMENTED.
    bool m_chunkVBOsNeedUpdating;

    // ========== 多线程地形生成 ==========
    // BlockTypeWorker 的输出：填好方块数据的 Chunk 指针
    std::vector<Chunk*> m_chunksPendingVBO;
    QMutex m_chunksPendingVBOMutex;

    std::vector<ChunkVBOData> m_completedVBOs;
    QMutex m_completedVBOsMutex;

    // VBO 正在生成的 Chunk，防止重复派发
    std::unordered_set<int64_t> m_vboInProgress;

    OpenGLContext* mp_context;

public:
    Terrain(OpenGLContext *context);
    ~Terrain();

    // Instantiates a new Chunk and stores it in
    // our chunk map at the given coordinates.
    // Returns a pointer to the created Chunk.
    Chunk* instantiateChunkAt(int x, int z);
    // Do these world-space coordinates lie within
    // a Chunk that exists?
    bool hasChunkAt(int x, int z) const;
    // Assuming a Chunk exists at these coords,
    // return a mutable reference to it
    uPtr<Chunk>& getChunkAt(int x, int z);
    // Assuming a Chunk exists at these coords,
    // return a const reference to it
    const uPtr<Chunk>& getChunkAt(int x, int z) const;
    // Given a world-space coordinate (which may have negative
    // values) return the block stored at that point in space.
    BlockType getGlobalBlockAt(int x, int y, int z) const;
    BlockType getGlobalBlockAt(glm::vec3 p) const;
    // Given a world-space coordinate (which may have negative
    // values) set the block at that point in space to the
    // given type.
    void setGlobalBlockAt(int x, int y, int z, BlockType t);

    // Draws every Chunk that falls within the bounding box
    // described by the min and max coords, using the provided
    // ShaderProgram
    void draw(int minX, int maxX, int minZ, int maxZ, ShaderProgram *shaderProgram);
    void drawTransparent(int minX, int maxX, int minZ, int maxZ, ShaderProgram *shaderProgram);
    // ★ 新增：根据玩家位置检查并扩展地形
    void expandTerrain(glm::vec3 playerPos);
    // ★ 新增：地形生成
    static float fractalNoise(glm::vec2 p, int octaves);//分型柏林
    static float getGrasslandHeight(float x, float z);
    static float getMountainHeight(float x, float z);
    static float getBiomeBlend(float x, float z);
    static void fillChunkWithTerrain(Chunk* chunk, int MinX, int MinZ);
    // ★ 新增：用噪声函数直接计算 (x,z) 处的地表高度（不依赖实际方块数据）
    static float getHeightAt(float x, float z);

    // Initializes the Chunks that store the 64 x 256 x 64 block scene you
    // see when the base code is run.
    void CreateTestScene(glm::vec3 playerPos);   // ★ 接受玩家位置

    // ★ 新增：检查玩家 AABB 在给定位置是否与方块碰撞
    bool checkPlayerCollision(glm::vec3 pos) const;

    //★ 新增：返回命中结果
    RaycastResult raycast(glm::vec3 origin, glm::vec3 dir, float maxDist) const;

    // ---- 方法声明 ----
    void tick(glm::vec3 playerPos);  // 每帧调用
};
