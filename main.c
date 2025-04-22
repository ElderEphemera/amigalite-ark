#include "raylib.h"
#include "raymath.h"
#include "rcamera.h"
#define FNL_IMPL
#include "FastNoiseLite.h"
#include <math.h>

#define WAVE_PEAKS 256
#define WAVE_SPAN 1024
#define WAVE_TEX_TILES 8

void genWaveVertices(fnl_state* noise, float* vertices)
{
  FNLfloat t = (FNLfloat)GetTime();

  int i = 0;
  for (int z = 0; z < WAVE_PEAKS; z++) {
    float zPos = ((float)z/(WAVE_PEAKS-1) - 0.5f)*WAVE_SPAN;
    for (int x = 0; x < WAVE_PEAKS; x++) {
      float xPos = ((float)x/(WAVE_PEAKS-1) - 0.5f)*WAVE_SPAN;
      vertices[i++] = xPos;
      vertices[i++] = 4*fnlGetNoise3D(noise, 2*xPos, 2*zPos, 30*t);
      vertices[i++] = zPos;
    }
  }
}

int main(void)
{
  // initialization
  //======================================================================================
  const int screenWidth = 1600;
  const int screenHeight = 900;

  InitWindow(screenWidth, screenHeight, "arc TODO");

  // noise
  fnl_state noise = fnlCreateState();
  noise.noise_type = FNL_NOISE_OPENSIMPLEX2;

  // assets
  Texture2D woodTexture = LoadTexture("assets/wood.jpg");
  Texture2D sailTexture = LoadTexture("assets/sail.jpg");
  Model ship = LoadModel("assets/ship.glb");
  for (int i = 0; i < ship.meshCount; i++) {
    float* texcoords = (float*)MemAlloc(ship.meshes[i].vertexCount*2*sizeof(float));
    for (int j = 0; j < ship.meshes[i].vertexCount*2; j++) {
      texcoords[j] = (float)GetRandomValue(0,255)/255;
    }
    UpdateMeshBuffer(ship.meshes[i], 1, texcoords, ship.meshes[i].vertexCount*2*sizeof(float), 0);
    MemFree(texcoords);
  }
  ship.materials[2].maps[MATERIAL_MAP_DIFFUSE].texture = woodTexture; // sides
  ship.materials[3].maps[MATERIAL_MAP_DIFFUSE].texture = woodTexture; // masts
  ship.materials[4].maps[MATERIAL_MAP_DIFFUSE].texture = woodTexture; // deck
  ship.materials[5].maps[MATERIAL_MAP_DIFFUSE].texture = sailTexture; // sails
  ship.materials[5].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;

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

  Model waves = LoadModelFromMesh(GenMeshPlane(WAVE_SPAN, WAVE_SPAN, WAVE_PEAKS-1, WAVE_PEAKS-1));
  waves.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = LoadTexture("assets/waves/1.jpg");
  float* waveVertices = (float*)MemAlloc(waves.meshes[0].vertexCount*3*sizeof(float));
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

    Vector3 oldPos = camera.position;
    UpdateCamera(&camera, CAMERA_FIRST_PERSON);
    Matrix shipMatrix = MatrixMultiply(MatrixScale(12, 12, 12), MatrixTranslate(35, 22, 5));
    bool collide = false;
    RayCollision collision = { 0 };
    Vector3 player = Vector3Subtract(oldPos, (Vector3){ 0, 2, 0 });
    Vector3 movement = Vector3Subtract(camera.position, oldPos);
    for (int i = 0; i < ship.meshCount; i++) {
      RayCollision newCollision = GetRayCollisionMesh((Ray){ player, Vector3Scale(movement, 100) }, ship.meshes[i], shipMatrix);
      if (newCollision.hit && (!collision.hit || collision.distance > newCollision.distance)) collision = newCollision;
    }
    if (collision.hit && collision.distance <= 0.1f) {
      Vector3 newPos = Vector3Add(oldPos, Vector3Reject(movement, Vector3Negate(collision.normal)));
      camera.target = Vector3Add(camera.target, Vector3Subtract(newPos, camera.position));
      camera.position = newPos;
    }

    // drawing
    //====================================================================================
    BeginDrawing();

      ClearBackground(SKYBLUE);

      BeginMode3D(camera);

        genWaveVertices(&noise, waveVertices);
        UpdateMeshBuffer(waves.meshes[0], 0, waveVertices, waves.meshes[0].vertexCount*3*sizeof(float), 0);
        DrawModel(waves, (Vector3){ 0, -13, 0 }, 1.0f, WHITE);

        DrawModel(ship, (Vector3){ 35, 22, 5 }, 12, WHITE);

        Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
        Matrix matScale = MatrixScale(0.6f, 0.4f, 0.4f);
        Matrix matRotation = MatrixMultiply(
          MatrixRotate(camera.up, atan2f(forward.x, forward.z)+PI),
          MatrixRotate(right, atan2f(forward.y, sqrtf(forward.x*forward.x+forward.z*forward.z)))
        );
        Matrix matTranslation = MatrixTranslate(
          camera.position.x + forward.x * 2.0f + right.x * 1.0f,
          camera.position.y - 1.0f,
          camera.position.z + forward.z * 2.0f + right.z * 1.0f
        );
        pole.transform = MatrixMultiply(MatrixMultiply(matScale, matRotation), matTranslation);
        DrawModel(pole, Vector3Zero(), 1, WHITE);

      EndMode3D();

      //DrawFPS(10, 10);

    EndDrawing();
  }

  // de-initialization
  //======================================================================================
  CloseWindow();

  return 0;
}
