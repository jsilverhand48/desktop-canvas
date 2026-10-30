// Implementation of PlaceholderRenderer declared in PlaceholderRenderer.h.
#include "scene-host/PlaceholderRenderer.h"

#include <epoxy/gl.h>

namespace canvas::scenehost {

void PlaceholderRenderer::render(int width, int height) {
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(r_, g_, b_, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

}  // namespace canvas::scenehost
