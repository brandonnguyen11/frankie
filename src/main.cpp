#include "core/Application.h"
#include "core/AssetPath.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

static void usage() {
  std::cout << "opera_grenouille [--join host:port|opera://host:port] [--name NAME]\n"
               "                 [--shot out.png --scene city|house|film|pit|balcony|lighting|orbit\n"
               "                  --frames N --size WxH [--hud]]\n"
               "                 [--record-test SECONDS]\n";
}

int main(int argc, char** argv) {
  og::initPaths(argc > 0 ? argv[0] : nullptr);
  og::LaunchOptions opts;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--shot") opts.shotPath = next();
    else if (a == "--scene") opts.shotScene = next();
    else if (a == "--frames") opts.shotFrames = std::max(1, std::atoi(next().c_str()));
    else if (a == "--size") {
      std::string s = next();
      auto x = s.find('x');
      if (x != std::string::npos) {
        opts.shotW = std::atoi(s.substr(0, x).c_str());
        opts.shotH = std::atoi(s.substr(x + 1).c_str());
      }
    } else if (a == "--hud") opts.shotHud = true;
    else if (a == "--join") opts.join = next();
    else if (a == "--name") opts.name = next();
    else if (a == "--record-test") opts.recordTest = (float)std::atof(next().c_str());
    else if (a.rfind("opera://", 0) == 0) opts.join = a;  // launched from a room link
    else if (a == "-h" || a == "--help") { usage(); return 0; }
  }
  og::Application app;
  if (!app.init(opts)) {
    std::cerr << "Failed to initialise Opéra Grenouille\n";
    return 1;
  }
  app.run();
  app.shutdown();
  return 0;
}
