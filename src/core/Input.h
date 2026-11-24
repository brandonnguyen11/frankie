#pragma once
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

namespace og {

struct InputState {
  bool keys[512]{};
  bool keysPressed[512]{};
  bool keysReleased[512]{};
  double mouseX = 0, mouseY = 0;
  double mouseDX = 0, mouseDY = 0;
  double scrollY = 0;
  bool leftDown = false, rightDown = false;
  bool leftClicked = false;
  bool uiWantsMouse = false;     // ImGui is under the cursor
  bool uiWantsKeyboard = false;  // ImGui text field has focus
};

// Collects GLFW events (and forwards them to ImGui). Gameplay reads a
// per-frame snapshot; presses/releases are edge-triggered for one frame.
class Input {
 public:
  void bind(GLFWwindow* window);
  void beginFrame();

  bool down(int key) const;
  bool pressed(int key) const;
  bool released(int key) const;

  InputState& state() { return s_; }
  const InputState& state() const { return s_; }
  static Input& instance();

 private:
  InputState s_;
};

}  // namespace og
