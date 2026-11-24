#pragma once
#include <string>

namespace og {

// Resolves assets/ and shaders/ next to the executable (the build copies them
// there), falling back to the source tree. Independent of the working directory.
void initPaths(const char* argv0);
std::string assetPath(const std::string& relative);
std::string shaderPath(const std::string& relative);

}  // namespace og
