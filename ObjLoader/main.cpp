#include <iostream>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <cassert>
#include <sstream>

struct float3 {
  float x;
  float y;
  float z;
};

struct Face {
  std::vector<int> indices;
};

struct MaterialFaceGroup {
  std::string material;
  std::vector<Face> faces;
};

struct Group {
  std::string group_name;
  std::vector<MaterialFaceGroup> material_face_groups;
};

struct Result {
  std::string material_lib;
  std::vector<float3> positions;
  std::vector<Group> groups;
};

Result ParseObj() {
  Result result{};

  std::ifstream fstrm("cornell_box.obj");
  if (!fstrm.is_open())
    return {};

  std::string line;
  std::string str;

  Group group{};
  MaterialFaceGroup material_face_group{};

  while (std::getline(fstrm, line)) {
    std::stringstream sstrm(line);

    while (sstrm >> str) {
      // Ignores comments.
      if (str.find("#") != std::string::npos)
        break;

      if (str == "mtllib") {
        sstrm >> result.material_lib;
        break;
      } else if (str == "v") {
        float3 val{};

        sstrm >> str;
        val.x = std::stof(str);

        sstrm >> str;
        val.y = std::stof(str);

        sstrm >> str;
        val.z = std::stof(str);

        result.positions.push_back(val);

      } else if (str == "g") {
        if (!material_face_group.faces.empty())
          group.material_face_groups.push_back(material_face_group);

        material_face_group.faces.clear();

        if (!group.material_face_groups.empty())
          result.groups.push_back(group);

        group = Group{};
        sstrm >> group.group_name;

      } else if (str == "usemtl") {
        if (!material_face_group.faces.empty())
          group.material_face_groups.push_back(material_face_group);

        material_face_group = MaterialFaceGroup{};
        sstrm >> material_face_group.material;

      } else if (str == "f") {
        Face face{};

        while (sstrm >> str) {
          int index = std::stoi(str);

          if (index < 0)
            index = result.positions.size() + index;

          face.indices.push_back(index);
        }

        material_face_group.faces.push_back(face);
      }
    }
  }

  if (!material_face_group.faces.empty())
    group.material_face_groups.push_back(material_face_group);

  if (!group.material_face_groups.empty())
    result.groups.push_back(group);

  return result;
}

int main() {
  Result result = ParseObj();

  return 0;
}