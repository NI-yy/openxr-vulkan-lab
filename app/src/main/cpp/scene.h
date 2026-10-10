#pragma once

#include <array>
#include <cstdint>

enum class SceneId { Clear, SingleCube, Grid100 };

struct CubePosition {
    float x = 0;
    float y = 0;
    float z = 0;
};

struct SceneData {
    SceneId id = SceneId::Grid100;
    std::array<CubePosition, 100> cubes{};
    uint32_t cubeCount = 0;
    std::array<float, 4> clearColor{0.02f, 0.12f, 0.55f, 1.0f};
};

const char* SceneName(SceneId id);
bool ParseScene(const char* name, SceneId& id);
SceneData MakeScene(SceneId id);
