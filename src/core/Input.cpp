#include "core/Input.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include <cstring>

namespace og {

Input& Input::instance() {
  static Input i;
  return i;
}

void Input::bind(GLFWwindow* window) {
  glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int sc, int action, int mods) {
    ImGui_ImplGlfw_KeyCallback(w, key, sc, action, mods);
    auto& s = Input::instance().s_;
    if (key < 0 || key >= 512) return;
    if (action == GLFW_PRESS) {
      // typing into a text field must not play notes or move the frog
      if (s.uiWantsKeyboard) return;
      s.keysPressed[key] = true;
      s.keys[key] = true;
    } else if (action == GLFW_RELEASE) {
      if (s.keys[key]) s.keysReleased[key] = true;
      s.keys[key] = false;
    }
  });
  glfwSetCharCallback(window, [](GLFWwindow* w, unsigned int c) { ImGui_ImplGlfw_CharCallback(w, c); });
  glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
    ImGui_ImplGlfw_CursorPosCallback(w, x, y);
    auto& s = Input::instance().s_;
    s.mouseDX += x - s.mouseX;
    s.mouseDY += y - s.mouseY;
    s.mouseX = x;
    s.mouseY = y;
  });
  glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int mods) {
    ImGui_ImplGlfw_MouseButtonCallback(w, button, action, mods);
    auto& s = Input::instance().s_;
    bool down = action != GLFW_RELEASE;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
      if (down && !s.uiWantsMouse) s.leftClicked = true;
      s.leftDown = down;
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT) s.rightDown = down && !s.uiWantsMouse;
  });
  glfwSetScrollCallback(window, [](GLFWwindow* w, double x, double y) {
    ImGui_ImplGlfw_ScrollCallback(w, x, y);
    auto& s = Input::instance().s_;
    if (!s.uiWantsMouse) s.scrollY += y;
  });
  glfwSetCursorEnterCallback(window, [](GLFWwindow* w, int e) { ImGui_ImplGlfw_CursorEnterCallback(w, e); });
  glfwSetWindowFocusCallback(window, [](GLFWwindow* w, int f) {
    ImGui_ImplGlfw_WindowFocusCallback(w, f);
    if (!f) {  // release held keys so notes don't hang when the window loses focus
      auto& s = Input::instance().s_;
      for (int k = 0; k < 512; ++k)
        if (s.keys[k]) { s.keys[k] = false; s.keysReleased[k] = true; }
    }
  });
  double x, y;
  glfwGetCursorPos(window, &x, &y);
  s_.mouseX = x;
  s_.mouseY = y;
}

void Input::beginFrame() {
  std::memset(s_.keysPressed, 0, sizeof(s_.keysPressed));
  std::memset(s_.keysReleased, 0, sizeof(s_.keysReleased));
  s_.mouseDX = s_.mouseDY = 0;
  s_.scrollY = 0;
  s_.leftClicked = false;
}

bool Input::down(int k) const { return k >= 0 && k < 512 && s_.keys[k]; }
bool Input::pressed(int k) const { return k >= 0 && k < 512 && s_.keysPressed[k]; }
bool Input::released(int k) const { return k >= 0 && k < 512 && s_.keysReleased[k]; }

}  // namespace og
