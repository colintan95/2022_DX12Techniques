#include <iostream>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <cassert>
#include <sstream>
#include <unordered_map>

struct float3 {
  float x;
  float y;
  float z;
};

struct Face {
  std::vector<int> indices;
};

struct Material {
  std::string name;
  float Ns;
  float Ni;
  int illum;
  float3 Ka;
  float3 Kd;
  float3 Ks;
  float3 Ke;
};

struct Result {
  std::string material_lib;
  std::vector<float3> positions;
  std::vector<Face> faces;
};

Result ParseObj() {
  Result result{};

  std::ifstream fstrm("cornell_box.obj");
  if (!fstrm.is_open())
    return result;

  std::string line;
  std::string str;

  while (std::getline(fstrm, line)) {
    std::stringstream sstrm(line);

    while (sstrm >> str) {
      // Ignores comments.
      if (str.find("#") != std::string::npos)
        break;

      if (str == "v") {
        float3 val{};
        sstrm >> val.x;
        sstrm >>  val.y;
        sstrm >> val.z;

        result.positions.push_back(val);

      } else if (str == "f") {
        Face face{};

        while (sstrm >> str) {
          int index = std::stoi(str);

          if (index < 0)
            index = result.positions.size() + index;

          face.indices.push_back(index);
        }

        result.faces.push_back(face);
      }
    }
  }

  return result;
}

std::unordered_map<std::string, Material> ParseMaterials() {
  std::unordered_map<std::string, Material> materials;

  std::ifstream fstrm("cornell_box.mtl");
  if (!fstrm.is_open())
    return materials;

  std::string line;
  std::string str;

  Material material{};

  while (std::getline(fstrm, line)) {
    std::stringstream sstrm(line);

    while (sstrm >> str) {
      // Ignores comments.
      if (str.find("#") != std::string::npos)
        break;

      if (str == "newmtl") {
        if (!material.name.empty())
          materials[material.name] = material;

        material = Material{};
        sstrm >> material.name;

      } else if (str == "Ns") {
        sstrm >> material.Ns;

      } else if (str == "Ni") {
        sstrm >> material.Ni;

      } else if (str == "illum") {
        sstrm >> material.illum;

      } else if (str == "Ka") {
        sstrm >> material.Ka.x;
        sstrm >> material.Ka.y;
        sstrm >> material.Ka.z;

      } else if (str == "Kd") {
        sstrm >> material.Kd.x;
        sstrm >> material.Kd.y;
        sstrm >> material.Kd.z;

      } else if (str == "Ks") {
        sstrm >> material.Ks.x;
        sstrm >> material.Ks.y;
        sstrm >> material.Ks.z;

      } else if (str == "Ke") {
        sstrm >> material.Ke.x;
        sstrm >> material.Ke.y;
        sstrm >> material.Ke.z;
      }
    }
  }

  if (!material.name.empty())
    materials[material.name] = material;

  return materials;
}

int main() {
  Result result = ParseObj();

  auto materials = ParseMaterials();

  return 0;
}