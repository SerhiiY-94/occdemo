#include "DummyApp.h"

#undef main
int main(int argc, char *argv[]) {
    return DummyApp().Run(std::vector<std::string>(argv + 1, argv + argc));
}

// TODO:
// make probe storage's texture immutable
// fix crash when minimizing window
// scene editing
// use direct access extension
// add logstream
// add assetstream
// fix Gui tests
// get rid of SDL in test applications
// make full screen quad passes differently
// refactor repetitive things in shaders
// use frame graph approach in renderer
// check GL_QCOM_alpha_test extension (for depth prepass and shadow rendering)
// check GL_QCOM_tiled_rendering extension
// try to use texture views to share framebuffers texture memory (GL_OES_texture_view)
// use one big array for instance indices
// get rid of SOIL in Ren (??? png loading left)