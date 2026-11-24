#pragma once
#include <string>
#include <vector>

namespace og {

// Native file dialogs (macOS: NSOpenPanel/NSSavePanel). On other platforms
// these return false and the HUD falls back to its typed path field.
bool openFileDialog(const std::string& title, const std::vector<std::string>& extensions, std::string& out);
bool saveFileDialog(const std::string& title, const std::string& defaultName, std::string& out);

}  // namespace og
