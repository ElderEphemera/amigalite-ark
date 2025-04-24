#include "raylib.h"
#include "raymath.h"
#include "rcamera.h"
#define FNL_IMPL
#include "FastNoiseLite.h"
#include <math.h>

#define WAVE_PEAKS 256
#define WAVE_SPAN 1024
#define WAVE_TEX_TILES 8

bool randomEvent(float avgSeconds) {
  return GetRandomValue(1, (int)128*avgSeconds/GetFrameTime()) <= 128;
}

float waveHeight(fnl_state* noise, float x, float z, float t) {
  return 4*fnlGetNoise3D(noise, 2*x, 2*z, 30*t);
}

void genWaveVertices(fnl_state* noise, float* vertices)
{
  FNLfloat t = (FNLfloat)GetTime();

  int i = 0;
  for (int z = 0; z < WAVE_PEAKS; z++) {
    float zPos = ((float)z/(WAVE_PEAKS-1) - 0.5f)*WAVE_SPAN;
    for (int x = 0; x < WAVE_PEAKS; x++) {
      float xPos = ((float)x/(WAVE_PEAKS-1) - 0.5f)*WAVE_SPAN;
      vertices[i++] = xPos;
      vertices[i++] = waveHeight(noise, xPos, zPos, t);
      vertices[i++] = zPos;
    }
  }
}

RayCollision CheckCollisionModel(Ray ray, Model model) {
  RayCollision collision = { 0 };

  for (int i = 0; i < model.meshCount; i++) {
    RayCollision newCollision = GetRayCollisionMesh(ray, model.meshes[i], model.transform);
    if (newCollision.hit && (!collision.hit || collision.distance > newCollision.distance)) {
      collision = newCollision;
    }
  }

  return collision;
}

Ray RayTransform(Ray r, Matrix mat) {
  Vector3 position = Vector3Transform(r.position, mat);
  r.direction = Vector3Normalize(Vector3Subtract(Vector3Transform(Vector3Add(r.position, r.direction), mat), position));
  r.position = position;
  return r;
}

void playerPhysics(Camera3D* camera, Vector3 oldPos, Model ship, Model ladder) {
  Vector3 player = Vector3Subtract(oldPos, (Vector3){ 0, 2, 0 });
  Vector3 movement = Vector3Subtract(camera->position, oldPos);

  RayCollision collision = GetRayCollisionBox(RayTransform((Ray){ Vector3Subtract(player, (Vector3){ 0, 4, 0 }), movement }, MatrixInvert(ladder.transform)), GetMeshBoundingBox(ladder.meshes[0]));
  bool climbing = collision.hit && collision.distance < 10;

  if (climbing) {
    camera->position = oldPos;
    camera->position.y += 0.2f;
  } else {
    collision = CheckCollisionModel((Ray){ player, movement }, ship);
    if (collision.hit && collision.distance <= 1) {
      Vector3 newPos = Vector3Add(oldPos, climbing ? Vector3Scale(camera->up, 2) : Vector3Reject(movement, collision.normal));
      camera->target = Vector3Add(camera->target, Vector3Subtract(newPos, camera->position));
      camera->position = newPos;

      movement = Vector3Subtract(camera->position, oldPos);
      collision = CheckCollisionModel((Ray){ player, Vector3ClampValue(movement, 1, 100) }, ship);

      if (collision.hit && collision.distance <= 1) {
        camera->target = Vector3Add(camera->target, Vector3Subtract(oldPos, camera->position));
        camera->position = oldPos;
      }
    }

    collision = CheckCollisionModel((Ray){ player, (Vector3){ 0, -1, 0 } }, ship);
    if (collision.hit && collision.distance >= 5 && !CheckCollisionBoxSphere(GetMeshBoundingBox(ladder.meshes[0]), Vector3Transform(Vector3Subtract(player, (Vector3){ 0, 4, 0 }), MatrixInvert(ladder.transform)), 20)) {
      camera->position.y -= 0.5f;
    }
  }
}

void updateCamera(Camera* camera) {
  float moveForward = 0, moveRight = 0;
  float rotateYaw = 0, rotatePitch = 0;

  // mouse
  Vector2 mousePositionDelta = GetMouseDelta();
  rotateYaw -= mousePositionDelta.x;
  rotatePitch -= mousePositionDelta.y;

  // keyboard
  if (IsKeyDown(KEY_W)) moveForward += 1;
  if (IsKeyDown(KEY_A)) moveRight -= 1;
  if (IsKeyDown(KEY_S)) moveForward -= 1;
  if (IsKeyDown(KEY_D)) moveRight += 1;

  // gamepad
  if (IsGamepadAvailable(0)) {
    rotateYaw -= 20*GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X);
    rotatePitch -= 20*GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y);

    float deadZone = 0.25f;
    if (GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y) <= -deadZone) moveForward += 1;
    else if (GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y) >= deadZone) moveForward -= 1;
    if (GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X) <= -deadZone) moveRight -= 1;
    else if (GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X) >= deadZone) moveRight += 1;
  }

  // update
  float moveAmount = 15*GetFrameTime();
  CameraMoveForward(camera, moveForward*moveAmount, true);
  CameraMoveRight(camera, moveRight*moveAmount, true);

  float rotateSensitivity = 0.003f;
  CameraYaw(camera, rotateYaw*rotateSensitivity, false);
  CameraPitch(camera, rotatePitch*rotateSensitivity, true, false, false);
}

void randomizeUV(Model* model) {
  for (int i = 0; i < model->meshCount; i++) {
    float* texcoords = (float*)MemAlloc(model->meshes[i].vertexCount*2*sizeof(float));
    for (int j = 0; j < model->meshes[i].vertexCount*2; j++) {
      texcoords[j] = (float)GetRandomValue(0,255)/255;
    }
    UpdateMeshBuffer(model->meshes[i], 1, texcoords, model->meshes[i].vertexCount*2*sizeof(float), 0);
    MemFree(texcoords);
  }
}

typedef enum { STATE_POLE, STATE_CASTING, STATE_CAST, STATE_HOOKED, STATE_REELING } state;

int main(void)
{
  // initialization
  //======================================================================================
  const int screenWidth = 1600;
  const int screenHeight = 900;

  InitWindow(screenWidth, screenHeight, "ark TODO");

  state st = STATE_POLE;

  // noise
  fnl_state noise = fnlCreateState();
  noise.noise_type = FNL_NOISE_OPENSIMPLEX2;

  // assets
  Texture2D woodTexture = LoadTexture("assets/wood.jpg");
  Texture2D sailTexture = LoadTexture("assets/sail.jpg");
  Model ship = LoadModel("assets/ship.glb");
  randomizeUV(&ship);
  ship.materials[2].maps[MATERIAL_MAP_DIFFUSE].texture = woodTexture; // sides
  ship.materials[3].maps[MATERIAL_MAP_DIFFUSE].texture = woodTexture; // masts
  ship.materials[4].maps[MATERIAL_MAP_DIFFUSE].texture = woodTexture; // deck
  ship.materials[5].maps[MATERIAL_MAP_DIFFUSE].texture = sailTexture; // sails
  ship.materials[5].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
  ship.transform = MatrixMultiply(MatrixScale(12, 12, 12), MatrixTranslate(35, 22, 5));

  Model ladder = LoadModel("assets/ladder.glb");
  randomizeUV(&ladder);
  ladder.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = woodTexture;
  ladder.meshMaterial[0] = 0;
  ladder.transform = MatrixMultiply(MatrixMultiply(MatrixScale(0.05f, 0.05f, 0.05f), MatrixRotate((Vector3){0,1,0}, 7*PI/32)), MatrixTranslate(12.5f, -10, -9.5f));

  Texture2D reelTexture = LoadTexture("assets/reel.jpg");
  Texture2D lineTexture = LoadTexture("assets/line.jpg");
  Texture2D metalTexture = LoadTexture("assets/metal.jpg");
  Model pole = LoadModel("assets/pole.glb");
  pole.materials[1].maps[MATERIAL_MAP_DIFFUSE].texture = reelTexture; // reel
  pole.materials[2].maps[MATERIAL_MAP_DIFFUSE].texture = woodTexture; // rod
  pole.materials[3].maps[MATERIAL_MAP_DIFFUSE].texture = lineTexture; // line
  pole.materials[4].maps[MATERIAL_MAP_DIFFUSE].texture = metalTexture; // hook
  pole.materials[5].maps[MATERIAL_MAP_DIFFUSE].texture = metalTexture; // trim
  pole.materials[6].maps[MATERIAL_MAP_DIFFUSE].texture = metalTexture; // guide
  float poleCastAngle = 0;

  Model bobber = LoadModel("assets/bobber.glb");
  Vector3 bobberVel = { 0 };
  Vector3 bobberPos = { 0 };

  Model exclamation = LoadModel("assets/exclamation.glb");
  exclamation.transform = MatrixRotate((Vector3){1,0,0}, PI);
  Vector3 exclamationPos = { 0 };

  Model waves = LoadModelFromMesh(GenMeshPlane(WAVE_SPAN, WAVE_SPAN, WAVE_PEAKS-1, WAVE_PEAKS-1));
  waves.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = LoadTexture("assets/waves/1.jpg");
  int i = 0;
  for (int z = 0; z < WAVE_PEAKS; z++) {
    bool flipZ = z/(WAVE_PEAKS/WAVE_TEX_TILES)%2;
    for (int x = 0; x < WAVE_PEAKS; x++) {
      bool flipX = x/(WAVE_PEAKS/WAVE_TEX_TILES)%2;
      float xPreflip = fmodf(waves.meshes[0].texcoords[i]*WAVE_TEX_TILES, 1);
      waves.meshes[0].texcoords[i++] = flipX ? 1-xPreflip : xPreflip;
      float zPreflip = fmodf(waves.meshes[0].texcoords[i]*WAVE_TEX_TILES, 1);
      waves.meshes[0].texcoords[i++] = flipZ ? 1-zPreflip : zPreflip;
    }
  }
  UpdateMeshBuffer(waves.meshes[0], 1, waves.meshes[0].texcoords, waves.meshes[0].vertexCount*2*sizeof(float), 0);
  waves.transform = MatrixTranslate(0, -13, 0);

  Camera camera = { 0 };
  camera.position = (Vector3){ 0.0f, 2.0f, 0.0f };
  camera.target = (Vector3){ 0.0f, 2.0f, 2.0f };
  camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
  camera.fovy = 60.0f;
  camera.projection = CAMERA_PERSPECTIVE;

  DisableCursor();

  SetTargetFPS(60);

  // main loop
  //======================================================================================
  while (!WindowShouldClose()) {
    // update
    //====================================================================================

    genWaveVertices(&noise, waves.meshes[0].vertices);
    UpdateMeshBuffer(waves.meshes[0], 0, waves.meshes[0].vertices, waves.meshes[0].vertexCount*3*sizeof(float), 0);

    Vector3 oldPos = camera.position;
    updateCamera(&camera);
    playerPhysics(&camera, oldPos, ship, ladder);

    if (st == STATE_POLE && (IsKeyDown(KEY_SPACE) || IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))) {
      poleCastAngle += PI/80;
    } else {
      poleCastAngle -= PI/10;
    }
    poleCastAngle = Clamp(poleCastAngle, 0, 2*PI/3);

    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Matrix matScale = MatrixScale(0.6f, 0.4f, 0.4f);
    Matrix matRotation = MatrixMultiply(
      MatrixRotate(camera.up, atan2f(forward.x, forward.z)+PI),
      MatrixRotate(right, poleCastAngle+atan2f(forward.y, sqrtf(forward.x*forward.x+forward.z*forward.z)))
    );
    Matrix matTranslation = MatrixTranslate(
      camera.position.x + forward.x * 2.0f + right.x * 1.0f,
      camera.position.y - 1.0f,
      camera.position.z + forward.z * 2.0f + right.z * 1.0f
    );
    pole.transform = MatrixMultiply(MatrixMultiply(matScale, matRotation), matTranslation);
    Vector3 bobberOnPolePos = Vector3Transform(Vector3Zero(), MatrixMultiply(MatrixMultiply(MatrixTranslate(0, -0.2f, -4.5f), matRotation), matTranslation));

    if (st == STATE_POLE) {
      bobberPos = bobberOnPolePos;
      pole.materials[3].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
      pole.materials[4].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    } else {
      pole.materials[3].maps[MATERIAL_MAP_DIFFUSE].color = BLANK;
      pole.materials[4].maps[MATERIAL_MAP_DIFFUSE].color = BLANK;
    }

    if (st == STATE_POLE && (IsKeyReleased(KEY_SPACE) || IsMouseButtonReleased(MOUSE_BUTTON_LEFT) || IsGamepadButtonReleased(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))) {
      bobberVel = Vector3Scale(forward, 30*poleCastAngle);
      st = STATE_CASTING;
    }

    if (st == STATE_CASTING) {
      Vector3 oldBobberPos = bobberPos;
      bobberVel = Vector3ClampValue(Vector3Add(bobberVel, (Vector3){ 0, -30*GetFrameTime(), 0 }), 0, 100000);
      bobberPos = Vector3Add(bobberPos, Vector3Scale(bobberVel, GetFrameTime()));

      /*
      RayCollision collision = CheckCollisionModel((Ray){ oldBobberPos, Vector3Subtract(bobberPos, oldBobberPos) }, waves);
      if (collision.hit && collision.distance < 1) {
        bobberPos = collision.point;
        st = STATE_CAST;
      } else if (bobberPos.y < -13) {
        st = STATE_CAST;
      }
      */
      if (bobberPos.y < waveHeight(&noise, bobberPos.x, bobberPos.y, GetTime()) - 13) st = STATE_CAST;
    }

    if (st == STATE_CAST || st == STATE_HOOKED) {
      /*
      Vector3 vertical = { 0, 5, 0 };
      RayCollision collision = CheckCollisionModel((Ray){ Vector3Add(bobberPos, vertical), Vector3Subtract(bobberPos, vertical) }, waves);
      if (collision.hit && collision.distance < 10) {
        bobberPos.y = collision.point.y;
      }
      */
      bobberPos.y = waveHeight(&noise, bobberPos.x, bobberPos.y, GetTime()) - 13;

      if (randomEvent(15)) st = STATE_HOOKED;

      if (IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) st = STATE_REELING;
    }

    if (st == STATE_HOOKED) {
      exclamationPos = bobberPos;
      exclamationPos.y += 5;

      if (randomEvent(3)) st = STATE_CAST;
    }

    if (st == STATE_REELING) {
      bobberPos = Vector3MoveTowards(bobberPos, bobberOnPolePos, 2);
      if (Vector3Equals(bobberPos, bobberOnPolePos)) st = STATE_POLE;
    }

    // drawing
    //====================================================================================
    BeginDrawing();

      ClearBackground(SKYBLUE);

      BeginMode3D(camera);

        DrawModel(waves, Vector3Zero(), 1, WHITE);
        DrawModel(ship, Vector3Zero(), 1, WHITE);
        DrawModel(ladder, Vector3Zero(), 1, WHITE);
        DrawModel(pole, Vector3Zero(), 1, WHITE);
        DrawModel(bobber, bobberPos, 0.01f, RED);
        if (st != STATE_POLE) DrawLine3D(bobberPos, bobberOnPolePos, WHITE);
        if (st == STATE_HOOKED) DrawModel(exclamation, exclamationPos, 1, YELLOW);

      EndMode3D();

      //DrawFPS(10, 10);

    EndDrawing();
  }

  // de-initialization
  //======================================================================================
  CloseWindow();

  return 0;
}
