#include "scene.h"

#include <cassert>
#include <cmath>
#include <cstring>

int main() {
    SceneId id = SceneId::Clear;
    assert(ParseScene("", id) && id == SceneId::Grid100);
    assert(ParseScene("clear", id) && id == SceneId::Clear);
    assert(ParseScene("single-cube", id) && id == SceneId::SingleCube);
    assert(ParseScene("grid-100", id) && id == SceneId::Grid100);
    assert(!ParseScene("unknown", id) && id == SceneId::Grid100);

    const SceneData clear = MakeScene(SceneId::Clear);
    const SceneData single = MakeScene(SceneId::SingleCube);
    const SceneData grid = MakeScene(SceneId::Grid100);
    assert(clear.cubeCount == 0);
    assert(single.cubeCount == 1 && single.cubes[0].z == -2.0f);
    assert(grid.cubeCount == 100);
    assert(grid.cubes[0].x == -1.1f && grid.cubes[0].y == -1.1f && grid.cubes[0].z == -2.0f);
    assert(grid.cubes[99].x == 1.1f && grid.cubes[99].y == 1.1f);
    assert(std::fabs(grid.cubes[99].z + 3.95f) < 0.00001f);
    assert(std::strcmp(SceneName(grid.id), "grid-100") == 0);
}
