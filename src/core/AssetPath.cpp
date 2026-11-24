#include "core/AssetPath.h"
#include <filesystem>
#include <vector>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace og {
namespace {
std::string gBase = ".";

std::filesystem::path executableDir(const char* argv0) {
  namespace fs = std::filesystem;
#if defined(__APPLE__)
  char buf[4096];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) == 0) return fs::weakly_canonical(fs::path(buf)).parent_path();
#elif defined(__linux__)
  std::error_code ec;
  auto p = fs::read_symlink("/proc/self/exe", ec);
  if (!ec) return p.parent_path();
#endif
  return fs::absolute(fs::path(argv0 ? argv0 : ".")).parent_path();
}
}  // namespace

void initPaths(const char* argv0) {
  namespace fs = std::filesystem;
  std::vector<fs::path> candidates = {executableDir(argv0), executableDir(argv0).parent_path(), fs::current_path()};
#ifdef OG_SOURCE_DIR
  candidates.push_back(fs::path(OG_SOURCE_DIR));
#endif
  for (auto& c : candidates) {
    if (fs::exists(c / "shaders" / "common.glsl") && fs::exists(c / "assets")) {
      gBase = c.string();
      return;
    }
  }
}

std::string assetPath(const std::string& r) { return gBase + "/assets/" + r; }
std::string shaderPath(const std::string& r) { return gBase + "/shaders/" + r; }

}  // namespace og
