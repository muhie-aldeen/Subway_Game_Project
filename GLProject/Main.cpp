#define LECTURE_TITLE "Road of No Return"
#define WINDOW_HEIGHT 800
#define WINDOW_WIDTH 800

#include "libs/glad_loader_docs.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

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

  // تهيئة لون السماء (أزرق سماوي هادئ)
  glClearColor(0.53f, 0.81f, 0.92f, 1.0f);

  // بناء قطعة طريق سوداء (أوتوستراد) مقسمة إلى 3 مسارات بخطوط بيضاء متقطعة
  float laneWidth = 2.0f;
  float segmentLength = 40.0f;
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
  ///////////////////////////////////////////////////////////////احتمال عدله بالمستقبل لتخفيف عدد العوائق انتبه اويس
// نشر وتوزيع المجسمات على طول قطعة بطول 40.0 وحدة
  float laneLeftX = -2.0f;
  float laneCenterX = 0.0f;
  float laneRightX = 2.0f;

  auto addSlideGate = [&](float laneX, float zPos) {
    float clearanceY = 1.05f;
    float beamThickness = 0.35f;
    float legHeight = curbHeight + clearanceY;
    vec3 slideColor{0.15f, 0.55f, 0.90f};

    addBoxToBuffers(roadPositions, roadColors,
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
  };

  auto addJumpCube = [&](float laneX, float zPos) {
    float jumpSize = 0.75f;
    addBoxToBuffers(roadPositions, roadColors,
                    {laneX, curbHeight + jumpSize * 0.5f, zPos},
                    {1.1f, jumpSize, 0.8f},
                    {0.95f, 0.55f, 0.10f});
  };

  auto addLongTrain = [&](float laneX, float zCenter) {
    float trainLength = 4.5f;
    float trainHeight = 2.1f;
    addBoxToBuffers(roadPositions, roadColors,
                    {laneX, curbHeight + trainHeight * 0.5f, zCenter},
                    {1.3f, trainHeight, trainLength},
                    {0.80f, 0.18f, 0.18f});
  };

  // توزيع متوازن على مسافة 40.0 وحدة مع مسافات مريحة بين العوائق
  addJumpCube(laneLeftX,    -4.0f);
  addSlideGate(laneCenterX, -9.0f);
  addLongTrain(laneRightX, -15.0f);

  addSlideGate(laneLeftX,  -21.0f);
  addJumpCube(laneRightX,  -26.5f);
  addLongTrain(laneCenterX,-32.0f);
  addJumpCube(laneLeftX,   -37.5f);
  ///////////////////////////////////////////////////////////////////

  Model roadSegment(roadPositions, roadColors);

  if (GLenum err = glGetError() != 0) {
    return err;
  }

  ////////////////////////////////////////////////////////////////
  // 1. مجسم القفز: مكعب منخفض (36 رأس) يُقفز من فوقه
  vector<vec3> jumpPos, jumpCol;
  float jumpSize = 0.75f;
  addBoxToBuffers(jumpPos, jumpCol,
                  {0.0f, curbHeight + jumpSize * 0.5f, 0.0f},
                  {1.1f, jumpSize, 0.8f},
                  {0.95f, 0.55f, 0.10f}); // برتقالي للقفز
  Model jumpObstacle(jumpPos, jumpCol);

  // 2. مجسم الانزلاق: بوابة/جسر مفرغ من الأسفل (3 أعمدة/أجزاء = 108 رؤوس) للانزلاق تحته
  vector<vec3> slidePos, slideCol;
  float clearanceY = 1.05f; // ارتفاع الفراغ السفلي للانزلاق
  float beamThickness = 0.35f;
  float legHeight = curbHeight + clearanceY;
  vec3 slideColor{0.15f, 0.55f, 0.90f}; // أزرق للانزلاق

  // قائم أيسر + قائم أيمن + عارضة علوية مفرغة من الأسفل
  addBoxToBuffers(slidePos, slideCol,
                  {-0.75f, legHeight * 0.5f, 0.0f},
                  {0.25f, legHeight, 0.7f},
                  slideColor);
  addBoxToBuffers(slidePos, slideCol,
                  { 0.75f, legHeight * 0.5f, 0.0f},
                  {0.25f, legHeight, 0.7f},
                  slideColor);
  addBoxToBuffers(slidePos, slideCol,
                  {0.0f, legHeight + beamThickness * 0.5f, 0.0f},
                  {1.75f, beamThickness, 0.7f},
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

  if (GLenum err = glGetError() != 0) {
    return err;
  }

  glUseProgram(shaderProgram.getProgram());

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

  while (!glfwWindowShouldClose(window)) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(window, true);

    double thisFrameTime = glfwGetTime();
    double deltaTime = thisFrameTime - lastFrameTime;
    lastFrameTime = thisFrameTime;

    // حركة اللاعب للأمام مؤقتاً للتجربة
    player.pos.z -= 6.0f * (float)deltaTime;

    // تحديث الكاميرا وإرسالها للـ Shader
    viewMatrix = player.getViewMatrix();
    glUniformMatrix4fv(viewMatrixLocation, 1, GL_FALSE, &viewMatrix[0][0]);

    // إرسال الزمن ومسح الشاشة
    glUniform1f(timeLocation, (float)thisFrameTime);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // رسم قطع الطريق بشكل لانهائي أمام اللاعب
    int currentSeg = (int)floor(-player.pos.z / segmentLength);
    for (int i = currentSeg - 1; i < currentSeg + 5; ++i) {
      mat4 roadModel = translate(mat4(1.0f), {0.0f, 0.0f, -i * segmentLength});
      glUniformMatrix4fv(modelMatrixLocation, 1, GL_FALSE, &roadModel[0][0]);
      roadSegment.draw(GL_TRIANGLES);
    }
    ////////////////////////////////////
   
    ////////////////////////////////////////

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  glfwTerminate();
  return 0;
}