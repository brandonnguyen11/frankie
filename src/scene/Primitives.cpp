#include "scene/Primitives.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace og {

std::vector<SceneObject> buildCityPlaza() {
  std::vector<SceneObject> objs;
  // Plaza ground
  {
    SceneObject o;
    o.mesh = makePlane(40.f, 40.f, {0.35f, 0.32f, 0.28f});
    o.mesh.material = {{0.35f, 0.32f, 0.28f}, 0.9f, 0.f, 0, 0, 0, 0};
    o.model = glm::mat4(1.f);
    objs.push_back(std::move(o));
  }
  // Mid-century storefronts around plaza
  auto addBuilding = [&](glm::vec3 pos, glm::vec3 scale, glm::vec3 color, bool neon) {
    SceneObject o;
    o.mesh = makeBox(scale * 0.5f, color);
    o.mesh.material.albedo = color;
    o.mesh.material.materialType = neon ? 4 : 0;
    o.mesh.material.roughness = 0.7f;
    o.model = glm::translate(glm::mat4(1.f), pos);
    objs.push_back(std::move(o));
    if (neon) {
      SceneObject sign;
      sign.mesh = makeBox({scale.x * 0.4f, 0.15f, 0.08f}, {1.f, 0.3f, 0.5f});
      sign.mesh.material.albedo = {1.f, 0.35f, 0.55f};
      sign.mesh.material.materialType = 4;
      sign.model = glm::translate(glm::mat4(1.f), pos + glm::vec3(0, scale.y * 0.35f, scale.z * 0.51f));
      objs.push_back(std::move(sign));
    }
  };
  addBuilding({-14, 3, -16}, {8, 6, 4}, {0.55f, 0.45f, 0.35f}, true);
  addBuilding({0, 2.5f, -17}, {6, 5, 3}, {0.4f, 0.48f, 0.55f}, true);
  addBuilding({12, 3.5f, -15}, {7, 7, 4}, {0.5f, 0.4f, 0.4f}, false);
  addBuilding({-15, 2.8f, 10}, {6, 5.5f, 3}, {0.45f, 0.5f, 0.4f}, true);
  addBuilding({14, 3.2f, 12}, {8, 6.5f, 4}, {0.35f, 0.4f, 0.5f}, true);

  // Streetlamps
  for (int i = -3; i <= 3; ++i) {
    if (i == 0) continue;
    SceneObject pole;
    pole.mesh = makeCylinder(0.08f, 4.f, 8, {0.2f, 0.2f, 0.22f});
    pole.model = glm::translate(glm::mat4(1.f), {(float)i * 4.f, 2.f, 8.f});
    objs.push_back(std::move(pole));
    SceneObject lamp;
    lamp.mesh = makeSphere(0.25f, 10, 8, {1.f, 0.85f, 0.55f});
    lamp.mesh.material.materialType = 4;
    lamp.mesh.material.albedo = {1.f, 0.85f, 0.55f};
    lamp.model = glm::translate(glm::mat4(1.f), {(float)i * 4.f, 4.2f, 8.f});
    objs.push_back(std::move(lamp));
  }

  // Period cars (boxy)
  auto addCar = [&](glm::vec3 pos, glm::vec3 color) {
    SceneObject body;
    body.mesh = makeBox({1.6f, 0.55f, 0.7f}, color);
    body.model = glm::translate(glm::mat4(1.f), pos + glm::vec3(0, 0.7f, 0));
    objs.push_back(std::move(body));
    SceneObject cabin;
    cabin.mesh = makeBox({0.9f, 0.4f, 0.65f}, color * 0.85f);
    cabin.model = glm::translate(glm::mat4(1.f), pos + glm::vec3(-0.2f, 1.35f, 0));
    objs.push_back(std::move(cabin));
  };
  addCar({-8, 0, 14}, {0.7f, 0.15f, 0.12f});
  addCar({6, 0, 15}, {0.15f, 0.25f, 0.55f});
  addCar({10, 0, -8}, {0.85f, 0.75f, 0.2f});

  return objs;
}

std::vector<SceneObject> buildGarnierExterior() {
  std::vector<SceneObject> objs;
  // Main facade block
  {
    SceneObject facade;
    facade.mesh = makeBox({10.f, 8.f, 4.f}, {0.82f, 0.78f, 0.65f});
    facade.mesh.material.albedo = {0.82f, 0.78f, 0.65f};
    facade.mesh.material.roughness = 0.55f;
    facade.model = glm::translate(glm::mat4(1.f), {0, 8.f, -2.f});
    objs.push_back(std::move(facade));
  }
  // Pediment
  {
    SceneObject ped;
    ped.mesh = makeBox({11.f, 1.5f, 4.2f}, {0.9f, 0.85f, 0.7f});
    ped.model = glm::translate(glm::mat4(1.f), {0, 17.f, -2.f});
    objs.push_back(std::move(ped));
  }
  // Columns
  for (int i = -4; i <= 4; ++i) {
    SceneObject col;
    col.mesh = makeCylinder(0.35f, 10.f, 12, {0.88f, 0.84f, 0.72f});
    col.model = glm::translate(glm::mat4(1.f), {(float)i * 2.2f, 5.f, 1.5f});
    objs.push_back(std::move(col));
  }
  // Front steps
  for (int i = 0; i < 5; ++i) {
    SceneObject step;
    step.mesh = makeBox({9.f - i * 0.3f, 0.2f, 0.6f}, {0.7f, 0.68f, 0.62f});
    step.model = glm::translate(glm::mat4(1.f), {0, 0.2f + i * 0.35f, 4.5f - i * 0.5f});
    objs.push_back(std::move(step));
  }
  // Double doors at ground/step level (walk into these to enter)
  {
    SceneObject doorL;
    doorL.mesh = makeBox({1.15f, 2.2f, 0.12f}, {0.25f, 0.12f, 0.08f});
    doorL.mesh.name = "door";
    doorL.model = glm::translate(glm::mat4(1.f), {-1.25f, 2.4f, 2.35f});
    objs.push_back(std::move(doorL));
    SceneObject doorR;
    doorR.mesh = makeBox({1.15f, 2.2f, 0.12f}, {0.25f, 0.12f, 0.08f});
    doorR.mesh.name = "door";
    doorR.model = glm::translate(glm::mat4(1.f), {1.25f, 2.4f, 2.35f});
    objs.push_back(std::move(doorR));
    // Doorway frame opening hint
    SceneObject lintel;
    lintel.mesh = makeBox({2.6f, 0.25f, 0.2f}, {0.55f, 0.4f, 0.25f});
    lintel.model = glm::translate(glm::mat4(1.f), {0.f, 4.7f, 2.35f});
    objs.push_back(std::move(lintel));
  }
  // Facade lamps
  for (float x : {-8.f, -4.f, 4.f, 8.f}) {
    SceneObject lamp;
    lamp.mesh = makeSphere(0.3f, 10, 8, {1.f, 0.8f, 0.5f});
    lamp.mesh.material.materialType = 4;
    lamp.mesh.material.albedo = {1.f, 0.8f, 0.5f};
    lamp.model = glm::translate(glm::mat4(1.f), {x, 6.f, 2.3f});
    objs.push_back(std::move(lamp));
  }
  return objs;
}

std::vector<SceneObject> buildAuditorium() {
  std::vector<SceneObject> objs;
  // Floor
  {
    SceneObject floor;
    floor.mesh = makePlane(36.f, 40.f, {0.15f, 0.08f, 0.08f});
    floor.mesh.material = {{0.2f, 0.08f, 0.08f}, 0.8f, 0, 0.6f, 0, 0, 1};
    objs.push_back(std::move(floor));
  }
  // Stage
  {
    SceneObject stage;
    stage.mesh = makeBox({8.f, 0.5f, 4.f}, {0.25f, 0.12f, 0.1f});
    stage.model = glm::translate(glm::mat4(1.f), {0, 0.5f, -6.f});
    objs.push_back(std::move(stage));
  }
  // Proscenium gold arch bars
  {
    SceneObject arch;
    arch.mesh = makeBox({9.f, 0.4f, 0.4f}, {0.85f, 0.7f, 0.3f});
    arch.mesh.material = {{1.f, 0.76f, 0.33f}, 0.3f, 1.f, 0, 0.7f, 0, 2};
    arch.model = glm::translate(glm::mat4(1.f), {0, 7.f, -2.f});
    objs.push_back(std::move(arch));
    for (float x : {-8.5f, 8.5f}) {
      SceneObject side;
      side.mesh = makeBox({0.4f, 3.5f, 0.4f}, {0.85f, 0.7f, 0.3f});
      side.mesh.material = arch.mesh.material;
      side.model = glm::translate(glm::mat4(1.f), {x, 3.5f, -2.f});
      objs.push_back(std::move(side));
    }
  }
  // Red curtains
  for (float x : {-7.f, 7.f}) {
    SceneObject curtain;
    curtain.mesh = makeBox({1.2f, 5.f, 0.15f}, {0.55f, 0.05f, 0.1f});
    curtain.mesh.material = {{0.55f, 0.05f, 0.1f}, 0.85f, 0, 0.9f, 0, 0, 1};
    curtain.model = glm::translate(glm::mat4(1.f), {x, 5.f, -2.2f});
    objs.push_back(std::move(curtain));
  }
  // Horseshoe seating rows (velvet)
  for (int row = 0; row < 8; ++row) {
    float z = 4.f + row * 2.2f;
    float y = 0.15f + row * 0.15f;
    int seats = 10 + row;
    for (int s = 0; s < seats; ++s) {
      float x = (s - seats * 0.5f) * 1.1f;
      SceneObject seat;
      seat.mesh = makeBox({0.4f, 0.35f, 0.4f}, {0.5f, 0.05f, 0.1f});
      seat.mesh.material = {{0.5f, 0.05f, 0.1f}, 0.85f, 0, 0.85f, 0, 0, 1};
      seat.model = glm::translate(glm::mat4(1.f), {x, y + 0.35f, z});
      objs.push_back(std::move(seat));
    }
  }
  // Balcony fronts (gold)
  for (int lvl = 0; lvl < 2; ++lvl) {
    float y = 5.5f + lvl * 3.5f;
    SceneObject bal;
    bal.mesh = makeBox({14.f, 0.25f, 0.3f}, {0.9f, 0.75f, 0.35f});
    bal.mesh.material = {{1.f, 0.76f, 0.33f}, 0.35f, 1.f, 0, 0.8f, 0, 2};
    bal.model = glm::translate(glm::mat4(1.f), {0, y, 12.f});
    objs.push_back(std::move(bal));
    // balcony seats
    for (int s = -10; s <= 10; ++s) {
      SceneObject seat;
      seat.mesh = makeBox({0.35f, 0.3f, 0.35f}, {0.5f, 0.05f, 0.1f});
      seat.mesh.material = {{0.5f, 0.05f, 0.1f}, 0.85f, 0, 0.85f, 0, 0, 1};
      seat.model = glm::translate(glm::mat4(1.f), {(float)s * 1.1f, y + 0.5f, 13.5f});
      objs.push_back(std::move(seat));
    }
  }
  // Walls
  {
    SceneObject back;
    back.mesh = makeBox({18.f, 10.f, 0.4f}, {0.35f, 0.12f, 0.12f});
    back.mesh.material = {{0.35f, 0.12f, 0.12f}, 0.8f, 0, 0.4f, 0, 0, 1};
    back.model = glm::translate(glm::mat4(1.f), {0, 10.f, 24.f});
    objs.push_back(std::move(back));
    for (float x : {-18.f, 18.f}) {
      SceneObject wall;
      wall.mesh = makeBox({0.4f, 10.f, 20.f}, {0.3f, 0.1f, 0.1f});
      wall.mesh.material = back.mesh.material;
      wall.model = glm::translate(glm::mat4(1.f), {x, 10.f, 4.f});
      objs.push_back(std::move(wall));
    }
  }
  // Chandelier
  {
    SceneObject ch;
    ch.mesh = makeCylinder(1.2f, 0.4f, 16, {1.f, 0.85f, 0.4f});
    ch.mesh.material.materialType = 4;
    ch.mesh.material.albedo = {1.f, 0.9f, 0.6f};
    ch.model = glm::translate(glm::mat4(1.f), {0, 14.f, 8.f});
    objs.push_back(std::move(ch));
    for (int i = 0; i < 8; ++i) {
      float a = i / 8.f * 6.28318f;
      SceneObject crystal;
      crystal.mesh = makeSphere(0.15f, 8, 6, {1.f, 0.95f, 0.8f});
      crystal.mesh.material.materialType = 4;
      crystal.mesh.material.albedo = {1.f, 0.95f, 0.8f};
      crystal.model = glm::translate(glm::mat4(1.f), {std::cos(a) * 1.1f, 13.5f, 8.f + std::sin(a) * 1.1f});
      objs.push_back(std::move(crystal));
    }
  }
  // Orchestra pit rail
  {
    SceneObject pit;
    pit.mesh = makeBox({7.f, 0.15f, 2.5f}, {0.12f, 0.1f, 0.1f});
    pit.model = glm::translate(glm::mat4(1.f), {0, 0.2f, 0.5f});
    objs.push_back(std::move(pit));
  }
  return objs;
}

}  // namespace og
