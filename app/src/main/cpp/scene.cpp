#include "scene.h"

#include <cstring>

const char* SceneName(SceneId id) {
    switch (id) {
        case SceneId::Clear: return "clear";
        case SceneId::SingleCube: return "single-cube";
        case SceneId::Grid100: return "grid-100";
    }
    return "grid-100";
}

bool ParseScene(const char* name, SceneId& id) {
    if (name == nullptr || name[0] == '\0' || std::strcmp(name, "grid-100") == 0) {
        id = SceneId::Grid100;
    } else if (std::strcmp(name, "clear") == 0) {
        id = SceneId::Clear;
    } else if (std::strcmp(name, "single-cube") == 0) {
        id = SceneId::SingleCube;
    } else {
        return false;
    }
    return true;
}

SceneData MakeScene(SceneId id) {
    SceneData scene{};
    scene.id = id;
    if (id == SceneId::SingleCube) {
        scene.cubes[0] = {0.0f, 0.0f, -2.0f};
        scene.cubeCount = 1;
    } else if (id == SceneId::Grid100) {
        for (uint32_t cube = 0; cube < scene.cubes.size(); ++cube) {
            const uint32_t column = cube % 5;
            const uint32_t row = (cube / 5) % 5;
            const uint32_t depth = cube / 25;
            scene.cubes[cube] = {
                (static_cast<float>(column) - 2.0f) * 0.55f,
                (static_cast<float>(row) - 2.0f) * 0.55f,
                -2.0f - static_cast<float>(depth) * 0.65f};
        }
        scene.cubeCount = static_cast<uint32_t>(scene.cubes.size());
    }
    return scene;
}
