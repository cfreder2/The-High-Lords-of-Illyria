// Fetching the 3D models made by tools/models for the renderer: the site's
// models/<name>.bin (the mesh) and models/<name>.webp (its texture), decoded
// and handed to the renderer (renderer_models.cpp). Until a model arrives, or
// if its fetch fails, the renderer draws its stand-in built in code.

#include "../render/renderer.h"
#include "platform.h"

namespace {

illyria::game::Renderer* target = nullptr;


}  // namespace

extern "C" {
ILLYRIA_API int illyria_model_loaded(int kind, const uint8_t* mesh, int meshBytes, const uint8_t* rgba, int width, int height) {
    return target && target->loadModel(static_cast<illyria::game::Renderer::ModelKind>(kind), mesh, static_cast<size_t>(meshBytes), rgba,
                                       width, height)
               ? 1
               : 0;
}
// Test hook: whether a model has come (0: the ruin).
ILLYRIA_API int illyria_test_model(int kind) {
    return target && target->modelLoaded(static_cast<illyria::game::Renderer::ModelKind>(kind)) ? 1 : 0;
}
// Test hook: the models off (0: the sites as before them) or on (1), for before/after shots.
ILLYRIA_API void illyria_test_models(int on) {
    if (target) target->enableModels(on != 0);
}
}

namespace illyria::game {

void fetchModels(Renderer& renderer) {
    target = &renderer;
    platform::requestModel(static_cast<int>(Renderer::ModelKind::Ruin));
}

}  // namespace illyria::game
