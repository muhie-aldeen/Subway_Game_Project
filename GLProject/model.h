#pragma once
#include <glm/glm.hpp>
#include <vector>

#include "libs/glad_loader_docs.h"
using namespace glm;
using namespace std;

class Model {
public:
  Model(vector<vec3> postions, vector<vec3> colors);
  void draw(GLint drawingMode);

private:
  GLuint VAO;
  unsigned long verticesCount;
};

class TexturedModel {
public:
  TexturedModel(vector<vec3> positions, vector<vec3> colors,
                vector<vec2> texCoords, GLuint textureId);
  void draw(GLint drawingMode);

private:
  GLuint VAO;
  GLuint textureId;
  unsigned long verticesCount;
};
