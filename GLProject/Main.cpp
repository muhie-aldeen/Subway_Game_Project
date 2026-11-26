#define LECTURE_TITLE "Road of No Return"
#define WINDOW_HEIGHT 800
#define WINDOW_WIDTH 800
#define MINIAUDIO_IMPLEMENTATION


#include "miniaudio.h"
#include "libs/glad_loader_docs.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "model.h"
#include "shader_program.h"

using namespace std;
using namespace glm;

struct PlayerCamera {
  vec3 pos{0.0f, 0.0f, 0.0f};      // موقع قدمي اللاعب
  float eyeHeight = 1.7f;          // ارتفاع العين عن الأرض
  float yaw = -90.0f;              // زاوية الالتفات أفقياً (-90 تعني النظر باتجاه -Z)
  float pitch = -5.0f;             // ميلان خفيف للأسفل لرؤية الطريق بوضوح
  vec3 front{0.0f, 0.0f, -1.0f};
  vec3 up{0.0f, 1.0f, 0.0f};

  void updateVectors() { // حساب اتجاه النظر
    vec3 direction;
    direction.x = cos(radians(yaw)) * cos(radians(pitch));
    direction.y = sin(radians(pitch));
    direction.z = sin(radians(yaw)) * cos(radians(pitch));
    front = normalize(direction);
  }

  mat4 getViewMatrix() { // إرجاع مصفوفة الكاميرا
    updateVectors();
    vec3 eyePos = pos + vec3(0.0f, eyeHeight, 0.0f);
    return lookAt(eyePos, eyePos + front, up);
  }
};

// صندوق إحاطة محوري المحاور يُستخدم للتصادم بين اللاعب والعوائق.
struct AABB {
  vec3 min;
  vec3 max;
};
/////////////////////////
// بارامترات التحكم بسرعة اللاعب وتذبذبها (مبنية على صيغة الدكتور)
float velocityBaseScale = 2.0f; // يتحكم بشدة السرعة والقيمة العظمى العامة
float velocityFrequency = 0.5f; // يتحكم بتواتر التسارع والتباطؤ (كلما زاد زاد عدد مرات التغير)
float velocityPhase     = 0.1f; // إزاحة الطور الأولية للموجة
float maxAllowedSpeed   = 28.0f; // حد أقصى اختياري لمنع انفلات السرعة بعد وقت طويل
/////////////////////////////////////////////////
GLuint loadTexture(const char *path, bool repeat) {
  stbi_set_flip_vertically_on_load(true);
  int width, height, channels;
  unsigned char *data =
      stbi_load(path, &width, &height, &channels, 4);
  if (!data)
    return 0;

  GLuint texture;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                  repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                  repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, data);
  glGenerateMipmap(GL_TEXTURE_2D);
  stbi_image_free(data);
  return texture;
}

struct ObjMesh {
  vector<vec3> positions;
  vector<vec3> colors;
  vector<vec2> texCoords;
};

ObjMesh loadObj(const char *path, vec3 color) {
  ObjMesh mesh;
  vector<vec3> vertices;
  ifstream file(path);
  if (!file)
    return mesh;

  string line;
  while (getline(file, line)) {
    istringstream stream(line);
    string type;
    stream >> type;
    if (type == "v") {
      vec3 vertex;
      stream >> vertex.x >> vertex.y >> vertex.z;
      vertices.push_back(vertex);
    } else if (type == "f") {
      vector<int> indices;
      string value;
      while (stream >> value) {
        size_t slash = value.find('/');
        indices.push_back(stoi(value.substr(0, slash)) - 1);
      }
      if (indices.size() >= 3) {
        for (size_t i = 1; i + 1 < indices.size(); ++i) {
          vec3 faceVertices[3] = {
              vertices[indices[0]],
              vertices[indices[i]],
              vertices[indices[i + 1]]};
          for (const vec3 &faceVertex : faceVertices) {
            mesh.positions.push_back(faceVertex);
            mesh.colors.push_back(color);
            mesh.texCoords.push_back({
                (faceVertex.x + 1.0f) * 0.5f,
                (faceVertex.z + 2.5f) / 5.0f});
          }
        }
      }
    }
  }
  return mesh;
}

bool checkCollision(const AABB &a, const AABB &b) {
  return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
         (a.min.y <= b.max.y && a.max.y >= b.min.y) &&
         (a.min.z <= b.max.z && a.max.z >= b.min.z);
}

AABB getPlayerAABB(const PlayerCamera &player, float playerHeightScale) {
  const float playerWidth = 0.9f;
  const float standingHeight = 1.7f;
  const float playerDepth = 0.9f;
  const float playerHeight = standingHeight * playerHeightScale;

  return {
      {player.pos.x - playerWidth * 0.5f, player.pos.y, player.pos.z - playerDepth * 0.5f},
      {player.pos.x + playerWidth * 0.5f, player.pos.y + playerHeight, player.pos.z + playerDepth * 0.5f}};
}

// Gets called whenever the application window is resized
void windowResizeCallback(GLFWwindow *window, int width, int height) {
  glViewport(0, 0, width, height);
}

int main(int argc, char *argv[]) {
  if (argc > 0) {
    std::filesystem::path exePath =
        std::filesystem::canonical(argv[0]).parent_path();
    std::filesystem::current_path(exePath);
  }

  // Initialize GLFW library and create window to draw on it
  glfwInit();

  // Set OpenGL Version
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  // Specify depth buffer bits as well so we can make use of depth features
  glfwWindowHint(GLFW_DEPTH_BITS, 24);

  GLFWwindow *window =
      glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, LECTURE_TITLE, NULL, NULL);
  glfwMakeContextCurrent(window);

  // Set the window resize callback
  glfwSetFramebufferSizeCallback(window, windowResizeCallback);

  gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

  // Enable depth
  glEnable(GL_DEPTH_TEST);

  GLuint trackTexture =
      loadTexture("textures/track_ground.png", true);
  GLuint trainFrontTexture =
      loadTexture("textures/train_front.png", false);
  GLuint trainSideTexture =
      loadTexture("textures/train_side.png", false);
  GLuint barrierTexture =
      loadTexture("textures/barrier_hazard.png", false);
  GLuint buildingTexture =
      loadTexture("textures/building_facade.png", true);
  GLuint coinTexture =
      loadTexture("textures/gold_coin.png", false);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  // تهيئة لون السماء (أزرق سماوي هادئ)
  glClearColor(0.53f, 0.81f, 0.92f, 1.0f);

  // بناء قطعة طريق سوداء (أوتوستراد) مقسمة إلى 3 مسارات بخطوط بيضاء متقطعة
  float laneWidth = 2.0f;
  float segmentLength = 80.0f;
  float halfW = laneWidth * 1.5f; // 3.0f (إجمالي عرض الشارع 6.0)

  vector<vec3> roadPositions;
  vector<vec3> roadColors;

  auto addQuad = [&](float x1, float x2, float z1, float z2, float y, vec3 color) {
    roadPositions.push_back({x1, y, z1});
    roadPositions.push_back({x2, y, z1});
    roadPositions.push_back({x2, y, z2});

    roadPositions.push_back({x1, y, z1});
    roadPositions.push_back({x2, y, z2});
    roadPositions.push_back({x1, y, z2});

    for (int i = 0; i < 6; ++i) roadColors.push_back(color);
  };

  auto addTriangle = [&](vec3 p1, vec3 p2, vec3 p3, vec3 color) {
    roadPositions.push_back(p1);
    roadPositions.push_back(p2);
    roadPositions.push_back(p3);

    for (int i = 0; i < 3; ++i) roadColors.push_back(color);
  };

  // سطح الأوتوستراد الأسود للمسارات الثلاثة
  addQuad(-halfW, -1.0f, 0.0f, -segmentLength, 0.0f, {0.06f, 0.06f, 0.07f}); // المسار الأيسر
  addQuad(-1.0f,   1.0f, 0.0f, -segmentLength, 0.0f, {0.08f, 0.08f, 0.09f}); // المسار الأوسط
  addQuad( 1.0f,  halfW, 0.0f, -segmentLength, 0.0f, {0.06f, 0.06f, 0.07f}); // المسار الأيمن

  // رصيف جانبي أوضح وأعرض يمين ويسار الطريق
  float curbWidth = 1.4f;
  float curbHeight = 0.16f;
  vec3 curbColor{0.48f, 0.48f, 0.52f};

  addQuad(-halfW - curbWidth, -halfW, 0.0f, -segmentLength, curbHeight, curbColor); // يسار
  addQuad( halfW, halfW + curbWidth, 0.0f, -segmentLength, curbHeight, curbColor); // يمين

  // خطوط بيضاء صغيرة متقطعة تفصل بين المسارات
  float dashHalfWidth = 0.04f; // عرض الخط الأبيض الكلي 0.08
  float dashLength = 2.0f;
  float step = 4.0f;
  float laneDividers[2] = {-1.0f, 1.0f};

  for (float dividerX : laneDividers) {
    for (float zStart = -1.0f; zStart > -segmentLength; zStart -= step) {
      float zEnd = glm::max(zStart - dashLength, -segmentLength);
      addQuad(dividerX - dashHalfWidth,
              dividerX + dashHalfWidth,
              zStart,
              zEnd,
              0.005f,
              {0.95f, 0.95f, 0.95f});
    }
  }

 // بناء شجرة بطبقات متدرجة: جذع بني + 3 مثلثات خضراء متداخلة لواقعية أكبر
// بناء شجرة بطبقات متدرجة مرتبة بدون تداخل لوني: جذع + 3 مثلثات متدرجة
  auto addTree = [&](float treeX, float zCenter) {
    float trunkHalfW = 0.11f;
    float trunkBottomY = curbHeight;
    float trunkTopY = 0.85f;
    vec3 trunkColor{0.35f, 0.19f, 0.07f};

    // طبقات متصلة دون تداخل عشوائي، وعرض أضيق وأكثر تناسقاً
    struct CrownLayer {
      float halfW;
      float bottomY;
      float topY;
      vec3 color;
    };

    CrownLayer layers[3] = {
        {0.52f, 0.85f, 1.45f, {0.09f, 0.56f, 0.16f}}, // أول مثلث بعد الجذع (أضيق وأرتب)
        {0.38f, 1.45f, 2.00f, {0.12f, 0.68f, 0.20f}}, // مثلث أضيق فوقه
        {0.24f, 2.00f, 2.50f, {0.16f, 0.78f, 0.25f}}  // مثلث صغير في القمة
    };

    float anglesDeg[2] = {0.0f, 55.0f};
    for (int idx = 0; idx < 2; ++idx) {
      float rad = radians(anglesDeg[idx]);
      float c = cos(rad);
      float s = sin(rad);
      float zBias = (idx == 1) ? 0.01f : 0.0f; // إزاحة بسيطة جداً لمنع تذبذب الألوان عند التقاطع

      auto rotXZ = [&](float localX, float yVal, float localZ) -> vec3 {
        return {
            treeX + localX * c - localZ * s,
            yVal,
            zCenter + zBias + localX * s + localZ * c
        };
      };

      // 1. الجذع (مستطيل = مثلثان)
      vec3 tbL = rotXZ(-trunkHalfW, trunkBottomY, 0.0f);
      vec3 tbR = rotXZ( trunkHalfW, trunkBottomY, 0.0f);
      vec3 ttR = rotXZ( trunkHalfW, trunkTopY,    0.0f);
      vec3 ttL = rotXZ(-trunkHalfW, trunkTopY,    0.0f);

      addTriangle(tbL, tbR, ttR, trunkColor);
      addTriangle(tbL, ttR, ttL, trunkColor);

      // 2. طبقات التاج المثلثة المتدرجة
      for (const auto& layer : layers) {
        vec3 cL   = rotXZ(-layer.halfW, layer.bottomY, 0.0f);
        vec3 cR   = rotXZ( layer.halfW, layer.bottomY, 0.0f);
        vec3 cTop = rotXZ(0.0f,         layer.topY,    0.0f);

        addTriangle(cL, cR, cTop, layer.color);
      }
    }
  };

  // تكرار الأشجار على جانبي الطريق على طول كل قطعة
  float leftTreeX = -halfW - curbWidth * 0.65f;
  float rightTreeX = halfW + curbWidth * 0.65f;
  float treeSpacing = 5.0f;

  for (float zPos = -2.0f; zPos > -segmentLength; zPos -= treeSpacing) {
    addTree(leftTreeX, zPos);
    addTree(rightTreeX, zPos - 2.5f);
  }
  ///////////////////////////////////////////////////////////
  // دالة لبناء صندوق 3D كامل (6 أوجه = 12 مثلث = 36 رأس) ضمن مصفوفات ممررة
  auto addBoxToBuffers = [](vector<vec3>& posBuf, vector<vec3>& colBuf,
                            vec3 center, vec3 size, vec3 color) {
    float hx = size.x * 0.5f;
    float hy = size.y * 0.5f;
    float hz = size.z * 0.5f;

    vec3 p000 = center + vec3(-hx, -hy, -hz);
    vec3 p100 = center + vec3( hx, -hy, -hz);
    vec3 p110 = center + vec3( hx,  hy, -hz);
    vec3 p010 = center + vec3(-hx,  hy, -hz);

    vec3 p001 = center + vec3(-hx, -hy,  hz);
    vec3 p101 = center + vec3( hx, -hy,  hz);
    vec3 p111 = center + vec3( hx,  hy,  hz);
    vec3 p011 = center + vec3(-hx,  hy,  hz);

    auto pushTri = [&](vec3 a, vec3 b, vec3 c) {
      posBuf.push_back(a);
      posBuf.push_back(b);
      posBuf.push_back(c);
      for (int i = 0; i < 3; ++i) colBuf.push_back(color);
    };

    // الوجه الأمامي (+Z)
    pushTri(p001, p101, p111);
    pushTri(p001, p111, p011);
    // الوجه الخلفي (-Z)
    pushTri(p100, p000, p010);
    pushTri(p100, p010, p110);
    // الوجه الأيسر (-X)
    pushTri(p000, p001, p011);
    pushTri(p000, p011, p010);
    // الوجه الأيمن (+X)
    pushTri(p101, p100, p110);
    pushTri(p101, p110, p111);
    // الوجه العلوي (+Y)
    pushTri(p011, p111, p110);
    pushTri(p011, p110, p010);
    // الوجه السفلي (-Y)
    pushTri(p000, p100, p101);
    pushTri(p000, p101, p001);
  };

  ///////////////////////////////////////////////////////////////
  // نشر وتوزيع المجسمات على طول قطعة بطول 80.0 وحدة
  float laneLeftX = -2.0f;
  float laneCenterX = 0.0f;
  float laneRightX = 2.0f;

/*   auto addSlideGate = [&](float laneX, float zPos) {
    float clearanceY = 1.05f;
    float beamThickness = 0.35f;
    float legHeight = curbHeight + clearanceY;
    vec3 slideColor{0.15f, 0.55f, 0.90f}; */

/*     addBoxToBuffers(roadPositions, roadColors,
                    {laneX - 0.75f, legHeight * 0.5f, zPos},
                    {0.25f, legHeight, 0.7f},
                    slideColor);
    addBoxToBuffers(roadPositions, roadColors,
                    {laneX + 0.75f, legHeight * 0.5f, zPos},
                    {0.25f, legHeight, 0.7f},
                    slideColor);
    addBoxToBuffers(roadPositions, roadColors,
                    {laneX, legHeight + beamThickness * 0.5f, zPos},
                    {1.75f, beamThickness, 0.7f},
                    slideColor);
  }; */

/*   auto addJumpCube = [&](float laneX, float zPos) {
    float jumpSize = 0.75f;
    addBoxToBuffers(roadPositions, roadColors,
                    {laneX, curbHeight + jumpSize * 0.5f, zPos},
                    {1.1f, jumpSize, 0.4f},
                    {0.95f, 0.55f, 0.10f});
  };

  auto addLongTrain = [&](float laneX, float zCenter) {
    float trainLength = 4.5f;
    float trainHeight = 2.1f;
    addBoxToBuffers(roadPositions, roadColors,
                    {laneX, curbHeight + trainHeight * 0.5f, zCenter},
                    {1.3f, trainHeight, trainLength},
                    {0.80f, 0.18f, 0.18f});
  }; */

 /*  // توزيع متوازن ومتنوع على مسافة 80.0 وحدة مع مسافات مريحة بين العوائق
  addJumpCube(laneCenterX,  -6.0f);   // بداية هادئة: قفز بالمنتصف
  addSlideGate(laneLeftX,   -15.5f);  // انزلاق يسار (+9.5)
  addLongTrain(laneRightX,  -25.5f);  // قطار يمين (+10.0)
  addJumpCube(laneRightX,   -35.0f);  // قفز يمين (+9.5)
  addSlideGate(laneCenterX, -45.5f);  // انزلاق وسط (+10.5)
  addJumpCube(laneLeftX,    -55.0f);  // قفز يسار (+9.5)
  addLongTrain(laneCenterX, -65.5f);  // قطار وسط (+10.5)
  addSlideGate(laneRightX,  -75.0f);  // انزلاق يمين قبل نهاية القطعة (+9.5) */
  ///////////////////////////////////////////////////////////////////

  Model roadSegment(roadPositions, roadColors);
  vector<vec2> roadTexCoords;
  for (const vec3 &vertex : roadPositions)
    roadTexCoords.push_back({vertex.x * 0.5f, -vertex.z * 0.25f});
  TexturedModel texturedRoadSegment(
      roadPositions, roadColors, roadTexCoords, trackTexture);

  if (GLenum err = glGetError() != 0) {
    return err;
  }

  ////////////////////////////////////////////////////////////////
  // 1. مجسم القفز: مكعب منخفض (36 رأس) يُقفز من فوقه
  vector<vec3> jumpPos, jumpCol;
  float jumpSize = 0.75f;
  addBoxToBuffers(jumpPos, jumpCol,
                  {0.0f, curbHeight + jumpSize * 0.5f, 0.0f},
                  {1.1f, jumpSize, 0.4f},
                  {0.95f, 0.55f, 0.10f}); // برتقالي للقفز
  Model jumpObstacle(jumpPos, jumpCol);

  // 2. مجسم الانزلاق: بوابة/جسر مفرغ من الأسفل (3 أعمدة/أجزاء = 108 رؤوس) للانزلاق تحته
  vector<vec3> slidePos, slideCol;
  float clearanceY = 1.05f; // ارتفاع الفراغ السفلي للانزلاق
  float beamThickness = 0.35f;
  float legHeight = curbHeight + clearanceY;
  vec3 slideColor{0.15f, 0.55f, 0.60f}; // أزرق للانزلاق

  // قائم أيسر + قائم أيمن + عارضة علوية مفرغة من الأسفل
  addBoxToBuffers(slidePos, slideCol,
                  {-0.75f, legHeight * 0.5f, 0.0f},
                  {0.25f, legHeight, 0.6f},
                  slideColor);
  addBoxToBuffers(slidePos, slideCol,
                  { 0.75f, legHeight * 0.5f, 0.0f},
                  {0.25f, legHeight, 0.6f},
                  slideColor);
  addBoxToBuffers(slidePos, slideCol,
                  {0.0f, legHeight + beamThickness * 0.5f, 0.0f},
                  {1.75f, beamThickness, 0.6f},
                  slideColor);
  Model slideObstacle(slidePos, slideCol);

  // 3. مجسم التجنب: مستطيل طويل يشبه القطار/الجدار (36 رأس) لتغيير المسار
  vector<vec3> trainPos, trainCol;
  float trainLength = 6.0f;
  float trainHeight = 2.2f;
  addBoxToBuffers(trainPos, trainCol,
                  {0.0f, curbHeight + trainHeight * 0.5f, -trainLength * 0.5f},
                  {1.4f, trainHeight, trainLength},
                  {0.80f, 0.18f, 0.18f}); // أحمر كعائق/قطار طويل
  Model trainObstacle(trainPos, trainCol);
  ///////////////////////////////////////////////////////////////////////

  // عملات موزعة على المسارات الثلاثة بطول قطعة 80.0 وحدة
  vector<vec3> coinPos, coinCol;
  vec3 coinColor{1.0f, 0.82f, 0.05f};
  const int coinSides = 16;
  const float coinRadius = 0.30f;
  const float coinHalfDepth = 0.055f;
  for (int i = 0; i < coinSides; ++i) {
    float angleA = radians(static_cast<float>(i) * 360.0f / coinSides);
    float angleB = radians(static_cast<float>(i + 1) * 360.0f / coinSides);
    vec3 a(cos(angleA) * coinRadius, sin(angleA) * coinRadius, -coinHalfDepth);
    vec3 b(cos(angleB) * coinRadius, sin(angleB) * coinRadius, -coinHalfDepth);
    vec3 c(cos(angleB) * coinRadius, sin(angleB) * coinRadius, coinHalfDepth);
    vec3 d(cos(angleA) * coinRadius, sin(angleA) * coinRadius, coinHalfDepth);
    coinPos.insert(coinPos.end(), {a, b, c, a, c, d});
    coinPos.insert(coinPos.end(), {a, d, c, b, c, d});
    for (int v = 0; v < 12; ++v)
      coinCol.push_back(coinColor);
  }
  vector<vec2> coinTexCoords;
  for (const vec3 &vertex : coinPos)
    coinTexCoords.push_back({vertex.x * 0.5f + 0.5f,
                             vertex.y * 0.5f + 0.5f});

  struct Coin {
    float laneX;
    float z;
    bool isCollected;
  };
  Coin coins[] = {
      {-2.0f,  -7.0f, false},
      {-2.0f,  -8.5f, false},
      { 0.0f, -18.0f, false},
      { 0.0f, -19.5f, false},
      { 2.0f, -31.0f, false},
      {-2.0f, -44.0f, false},
      { 0.0f, -57.0f, false},
      { 2.0f, -70.0f, false}};

  ObjMesh coinObj = loadObj("models/coin.obj", coinColor);
  TexturedModel coinModel(
      coinObj.positions.empty() ? coinPos : coinObj.positions,
      coinObj.positions.empty() ? coinCol : coinObj.colors,
      coinObj.positions.empty()
          ? coinTexCoords
          : coinObj.texCoords,
      coinTexture);

  ObjMesh trainObj = loadObj("models/train.obj",
                             vec3(0.80f, 0.18f, 0.18f));
  vector<vec2> trainTexCoords;
  for (const vec3 &vertex : trainObj.positions)
    trainTexCoords.push_back({(vertex.x + 0.85f) / 1.7f,
                              vertex.y / 2.45f});
  TexturedModel trainModel(
      trainObj.positions.empty() ? trainPos : trainObj.positions,
      trainObj.positions.empty() ? trainCol : trainObj.colors,
      trainObj.positions.empty()
          ? vector<vec2>(trainPos.size(), vec2(0.5f, 0.5f))
          : trainTexCoords,
      trainObj.positions.empty() ? barrierTexture : trainFrontTexture);
  TexturedModel trainSideModel(
      trainObj.positions.empty() ? trainPos : trainObj.positions,
      trainObj.positions.empty() ? trainCol : trainObj.colors,
      trainObj.positions.empty()
          ? vector<vec2>(trainPos.size(), vec2(0.5f, 0.5f))
          : trainTexCoords,
      trainObj.positions.empty() ? barrierTexture : trainSideTexture);

  vector<vec3> buildingPos;
  vector<vec3> buildingCol;
  addBoxToBuffers(buildingPos, buildingCol,
                  {-4.8f, 1.6f, -segmentLength * 0.5f},
                  {1.2f, 3.2f, segmentLength},
                  vec3(0.48f, 0.52f, 0.58f));
  addBoxToBuffers(buildingPos, buildingCol,
                  {4.8f, 1.6f, -segmentLength * 0.5f},
                  {1.2f, 3.2f, segmentLength},
                  vec3(0.48f, 0.52f, 0.58f));
  vector<vec2> buildingTexCoords;
  for (const vec3 &vertex : buildingPos)
    buildingTexCoords.push_back({vertex.x * 0.5f, -vertex.z * 0.05f});
  TexturedModel buildingModel(buildingPos, buildingCol,
                              buildingTexCoords, buildingTexture);

  vector<vec2> jumpTexCoords(jumpPos.size());
  for (size_t i = 0; i < jumpPos.size(); ++i)
    jumpTexCoords[i] = {(jumpPos[i].x + 1.1f) / 2.2f,
                        jumpPos[i].y / 1.0f};
  TexturedModel texturedJump(jumpPos, jumpCol, jumpTexCoords, barrierTexture);

  vector<vec2> slideTexCoords(slidePos.size());
  for (size_t i = 0; i < slidePos.size(); ++i)
    slideTexCoords[i] = {(slidePos[i].x + 1.75f) / 3.5f,
                         slidePos[i].y / 1.6f};
  TexturedModel texturedSlide(slidePos, slideCol, slideTexCoords, barrierTexture);

  // Load and link shader files
  ShaderProgram shaderProgram("shaders/shader.vert", "shaders/shader.frag");

  if (shaderProgram.getError()) {
    return -1;
  }

  if (GLenum err = glGetError() != 0) {
    return err;
  }

  GLuint modelMatrixLocation =
      glGetUniformLocation(shaderProgram.getProgram(), "model");
  GLuint viewMatrixLocation =
      glGetUniformLocation(shaderProgram.getProgram(), "view");
  GLuint perspectiveMatrixLocation =
      glGetUniformLocation(shaderProgram.getProgram(), "perspective");
  GLuint timeLocation =
      glGetUniformLocation(shaderProgram.getProgram(), "time");
  GLuint cameraPositionLocation =
      glGetUniformLocation(shaderProgram.getProgram(), "cameraPosition");
  GLuint diffuseTextureLocation =
      glGetUniformLocation(shaderProgram.getProgram(), "diffuseTexture");
  GLuint useTextureLocation =
      glGetUniformLocation(shaderProgram.getProgram(), "useTexture");

  if (GLenum err = glGetError() != 0) {
    return err;
  }

  glUseProgram(shaderProgram.getProgram());
  glUniform1i(diffuseTextureLocation, 0);

  float fieldOfView = radians(45.0f);
  float aspectRatio = (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT;
  float nearPlane = 0.01f;
  float farPlane = 100.0f;

  mat4 perspectiveMatrix =
      perspective(fieldOfView, aspectRatio, nearPlane, farPlane);

  glUniformMatrix4fv(perspectiveMatrixLocation, 1, GL_FALSE,
                     &perspectiveMatrix[0][0]);

  if (GLenum err = glGetError() != 0) {
    return err;
  }

  double lastFrameTime = glfwGetTime();
  PlayerCamera player;
  mat4 viewMatrix(1.0f);
  const float lanePositions[3] = {-2.0f, 0.0f, 2.0f};
  int currentLane = 1;
  bool leftKeyDown = false;
  bool rightKeyDown = false;
  const float laneMoveSpeed = 10.0f;
  bool jumpKeyDown = false;
  float jumpVelocity = 0.0f;
  const float jumpInitialVelocity = 7.0f;
  const float gravity = -20.0f;
  const float groundHeight = 0.0f;
  const float standingHeightScale = 1.0f; 
  const float slidingHeightScale = 0.5f;
  const float slideBlendSpeed = 14.0f;
  float playerHeightScale = standingHeightScale;
  int collisionCount = 0;
  int score = 0;
  bool isGameOver = false;
  bool retryKeyDown = false;
  double gameTime = 0.0;

  enum class ObstacleType { Jump, Slide, Train };
  struct Obstacle {
    float laneX;
    float z;
    ObstacleType type;
  };

  const Obstacle obstaclePositions[] = {
     {2.0f, -1.0f, ObstacleType::Train},
      { 0.0f,  -7.0f,  ObstacleType::Slide},
      { 0.0f, -30.0f, ObstacleType::Train},
      {-2.0f, -19.5f, ObstacleType::Jump},
      { 2.0f, -19.5f, ObstacleType::Jump},
    
      { -2.0f, -38.5f, ObstacleType::Slide},
      {-2.0f, -50.5f, ObstacleType::Train},
      { 0.0f, -52.0f, ObstacleType::Slide},
      {0.0f, -65.5f, ObstacleType::Train},
      { 2.0f, -70.5f, ObstacleType::Slide}};

  auto retryGame = [&]() {
    player.pos = vec3(0.0f, groundHeight, 0.0f);
    player.eyeHeight = 1.7f;
    currentLane = 1;
    jumpVelocity = 0.0f;
    jumpKeyDown = false;
    leftKeyDown = false;
    rightKeyDown = false;
    playerHeightScale = standingHeightScale;
    collisionCount = 0;
    score = 0;
    gameTime = 0.0;
    isGameOver = false;
    lastFrameTime = glfwGetTime();
    for (Coin &coin : coins)
      coin.isCollected = false;
  };


  /////////////////////////////////////song
  // تهيئة محرك الصوت وتشغيل الأغنية بتكرار مستمر
ma_engine audioEngine{};
  ma_sound bgMusic{};
  bool engineInitialized = false;
  bool musicInitialized = false;

  if (ma_engine_init(nullptr, &audioEngine) == MA_SUCCESS) {
    engineInitialized = true;

    // تأكد من تطابق الامتداد الحقيقي للملف (مثلاً assets/music.mp3 أو assets/music.wav)
    if (ma_sound_init_from_file(&audioEngine, "assets/music.mp3",
                                 MA_SOUND_FLAG_STREAM, nullptr, nullptr,
                                 &bgMusic) == MA_SUCCESS) {
      ma_sound_set_looping(&bgMusic, MA_TRUE);
      ma_sound_set_volume(&bgMusic, 0.5f);
      ma_sound_start(&bgMusic);
      musicInitialized = true;
    }
  }
  ///////////////////////////////////////////

 while (!glfwWindowShouldClose(window)) {
      if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

      double thisFrameTime = glfwGetTime();
      double deltaTime = thisFrameTime - lastFrameTime;
      lastFrameTime = thisFrameTime;
      float frameDeltaTime =
          std::min(static_cast<float>(deltaTime), 0.05f);

      bool retryPressed = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
      if (isGameOver && retryPressed && !retryKeyDown)
        retryGame();
      retryKeyDown = retryPressed;

      int currentSeg = (int)floor(-player.pos.z / segmentLength);

      if (!isGameOver) {
        gameTime += frameDeltaTime;

        bool leftKeyPressed =
            glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS;
        bool rightKeyPressed =
            glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS;

        if (leftKeyPressed && !leftKeyDown)
          currentLane = std::max(0, currentLane - 1);
        if (rightKeyPressed && !rightKeyDown)
          currentLane = std::min(2, currentLane + 1);

        leftKeyDown = leftKeyPressed;
        rightKeyDown = rightKeyPressed;

        float laneBlend =
            1.0f - exp(-laneMoveSpeed * static_cast<float>(deltaTime));
        player.pos.x +=
            (lanePositions[currentLane] - player.pos.x) * laneBlend;

        bool jumpKeyPressed =
            glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS;

        if (jumpKeyPressed && !jumpKeyDown &&
            player.pos.y <= groundHeight + 0.001f)
          jumpVelocity = jumpInitialVelocity;

        jumpKeyDown = jumpKeyPressed;

        jumpVelocity += gravity * frameDeltaTime;
        player.pos.y += jumpVelocity * frameDeltaTime;

        if (player.pos.y < groundHeight) {
          player.pos.y = groundHeight;
          jumpVelocity = 0.0f;
        }

        bool slidePressed =
            glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS;

        float targetHeightScale =
            slidePressed ? slidingHeightScale : standingHeightScale;
        float slideBlend =
            1.0f - exp(-slideBlendSpeed * frameDeltaTime);
        playerHeightScale +=
            (targetHeightScale - playerHeightScale) * slideBlend;
        player.eyeHeight =
            1.7f * playerHeightScale;

        // حركة وسرعة اللاعب وفق معادلة الدكتور مع بارامترات التحكم
        float t = static_cast<float>(gameTime);
        float rawVelocity =
            velocityBaseScale * t * (1.0f + sin(velocityFrequency * t + velocityPhase));
        float forwardVelocity = std::min(rawVelocity, maxAllowedSpeed);
        player.pos.z -= forwardVelocity * frameDeltaTime;

        // فحص التصادم الديناميكي مع عوائق قطع الطريق في المحاور X/Y/Z.
        currentSeg = (int)floor(-player.pos.z / segmentLength);
        AABB playerAABB = getPlayerAABB(player, playerHeightScale);
        for (int i = currentSeg - 1; i < currentSeg + 5 && !isGameOver; ++i) {
          for (const Obstacle &obstacle : obstaclePositions) {
            float worldZ = obstacle.z - i * segmentLength;
            AABB obstacleAABB;

            if (obstacle.type == ObstacleType::Jump) {
              obstacleAABB = {
                  {obstacle.laneX - 0.55f, curbHeight, worldZ - 0.2f},
                  {obstacle.laneX + 0.55f, curbHeight + 0.75f, worldZ + 0.2f}};
            } else if (obstacle.type == ObstacleType::Slide) {
              const float clearanceY = 1.05f;
              const float beamThickness = 0.35f;
              const float beamCenterY =
                  curbHeight + clearanceY + beamThickness * 0.5f;
              obstacleAABB = {
                  {obstacle.laneX - 0.875f, beamCenterY - beamThickness * 0.5f, worldZ - 0.35f},
                  {obstacle.laneX + 0.875f, beamCenterY + beamThickness * 0.5f, worldZ + 0.35f}};
            } else {
              obstacleAABB = {
                  {obstacle.laneX - 0.65f, curbHeight, worldZ - 2.25f},
                  {obstacle.laneX + 0.65f, curbHeight + 2.1f, worldZ + 2.25f}};
            }

            if (checkCollision(playerAABB, obstacleAABB)) {
              ++collisionCount;
              isGameOver = true;
              break;
            }
          }
        }

        // فحص العملات الحالية في قطع الطريق المعروضة.
        if (!isGameOver) {
          AABB playerAABB = getPlayerAABB(player, playerHeightScale);
          for (int i = currentSeg - 1; i < currentSeg + 5; ++i) {
            for (Coin &coin : coins) {
              if (coin.isCollected)
                continue;

              float worldZ = coin.z - i * segmentLength;
              AABB coinAABB = {
                  {coin.laneX - 0.25f, 0.75f, worldZ - 0.25f},
                  {coin.laneX + 0.25f, 1.25f, worldZ + 0.25f}};
              if (checkCollision(playerAABB, coinAABB)) {
                coin.isCollected = true;
                ++score;
              }
            }
          }
        }
      }

      // تحديث الكاميرا وإرسالها للـ Shader
      viewMatrix = player.getViewMatrix();
      glUniformMatrix4fv(viewMatrixLocation, 1, GL_FALSE, &viewMatrix[0][0]);

      // إرسال الزمن المجمد عند الخسارة ومسح الشاشة
      glUniform1f(timeLocation, (float)gameTime);
      vec3 cameraPosition =
          player.pos + vec3(0.0f, player.eyeHeight, 0.0f);
      glUniform3fv(cameraPositionLocation, 1, value_ptr(cameraPosition));
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      // رسم قطع الطريق بشكل لانهائي أمام اللاعب
      for (int i = currentSeg - 1; i < currentSeg + 5; ++i) {
        mat4 roadModel = translate(mat4(1.0f), {0.0f, 0.0f, -i * segmentLength});
        glUniformMatrix4fv(modelMatrixLocation, 1, GL_FALSE, &roadModel[0][0]);
        glUniform1i(useTextureLocation, trackTexture != 0 ? 1 : 0);
        texturedRoadSegment.draw(GL_TRIANGLES);

        mat4 buildingModelMatrix =
            translate(mat4(1.0f), {0.0f, 0.0f, -i * segmentLength});
        glUniformMatrix4fv(modelMatrixLocation, 1, GL_FALSE,
                           &buildingModelMatrix[0][0]);
        glUniform1i(useTextureLocation, buildingTexture != 0 ? 1 : 0);
        buildingModel.draw(GL_TRIANGLES);

        for (const Obstacle &obstacle : obstaclePositions) {
          float worldZ = obstacle.z - i * segmentLength;
          mat4 obstacleModel =
              translate(mat4(1.0f), {obstacle.laneX, 0.0f, worldZ});

          if (obstacle.type == ObstacleType::Jump) {
            glUniformMatrix4fv(modelMatrixLocation, 1, GL_FALSE,
                               &obstacleModel[0][0]);
            glUniform1i(useTextureLocation, barrierTexture != 0 ? 1 : 0);
            texturedJump.draw(GL_TRIANGLES);
          } else if (obstacle.type == ObstacleType::Slide) {
            glUniformMatrix4fv(modelMatrixLocation, 1, GL_FALSE,
                               &obstacleModel[0][0]);
            glUniform1i(useTextureLocation, barrierTexture != 0 ? 1 : 0);
            texturedSlide.draw(GL_TRIANGLES);
          } else {
            glUniformMatrix4fv(modelMatrixLocation, 1, GL_FALSE,
                               &obstacleModel[0][0]);
            glUniform1i(useTextureLocation,
                        trainFrontTexture != 0 ? 1 : 0);
            if (i % 2 == 0)
              trainModel.draw(GL_TRIANGLES);
            else
              trainSideModel.draw(GL_TRIANGLES);
          }
        }

        for (const Coin &coin : coins) {
          if (coin.isCollected)
            continue;
          float worldZ = coin.z - i * segmentLength;
          mat4 coinModelMatrix =
              translate(mat4(1.0f), {coin.laneX, 1.0f, worldZ});
          coinModelMatrix =
              rotate(coinModelMatrix, static_cast<float>(gameTime) * 2.6f,
                     vec3(0.0f, 1.0f, 0.0f));
          glUniformMatrix4fv(modelMatrixLocation, 1, GL_FALSE,
                             &coinModelMatrix[0][0]);
          glUniform1i(useTextureLocation, coinTexture != 0 ? 1 : 0);
          coinModel.draw(GL_TRIANGLES);
        }
      }
    ////////////////////////////////////
   
    ////////////////////////////////////////
   //////////////////////////////// انهاء الاغنية و التنظيف
    

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (!isGameOver) {
      ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f));
      ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f));
      ImGui::Begin("HUD", nullptr,
                   ImGuiWindowFlags_NoBackground |
                       ImGuiWindowFlags_NoDecoration |
                       ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_AlwaysAutoResize |
                       ImGuiWindowFlags_NoSavedSettings);
      ImGui::SetWindowFontScale(1.65f);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.20f, 1.0f));
      ImGui::Text("Score: %d", score);
      ImGui::PopStyleColor();
      ImGui::End();
    }

    if (isGameOver) {
      ImGuiStyle &style = ImGui::GetStyle();
      style.WindowRounding = 14.0f;
      style.FrameRounding = 8.0f;
      style.WindowPadding = ImVec2(24.0f, 18.0f);
      style.ItemSpacing = ImVec2(12.0f, 12.0f);

      ImVec2 displaySize = io.DisplaySize;
      ImGui::SetNextWindowPos(
          ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f),
          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(ImVec2(320.0f, 210.0f));
      ImGui::Begin("Game Over", nullptr,
                   ImGuiWindowFlags_NoTitleBar |
                       ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoCollapse);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.22f, 1.0f));
      ImGui::SetWindowFontScale(1.35f);
      ImGui::TextUnformatted("GAME OVER");
      ImGui::PopStyleColor();
      ImGui::SetWindowFontScale(1.15f);
      ImGui::Text("Final Score: %d", score);
      ImGui::Dummy(ImVec2(0.0f, 8.0f));
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95f, 0.55f, 0.08f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.68f, 0.16f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.78f, 0.38f, 0.04f, 1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18.0f, 12.0f));
      if (ImGui::Button("Retry (R)", ImVec2(272.0f, 52.0f)))
        retryGame();
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(3);
      ImGui::TextDisabled("Press R to retry");
      ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
    glfwPollEvents();
  }
    //////////////////////////////// انهاء الاغنية و التنظيف
if (musicInitialized) {
    ma_sound_uninit(&bgMusic);
  }
  if (engineInitialized) {
    ma_engine_uninit(&audioEngine);
  }
  /////////////////////////////////////////

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwTerminate();
  return 0;
}