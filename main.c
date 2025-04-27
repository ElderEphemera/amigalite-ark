#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include "rcamera.h"
#define FNL_IMPL
#include "FastNoiseLite.h"
#include <string.h>
#include <math.h>

#define WAVE_PEAKS 128
#define WAVE_SPAN 1024
#define WAVE_TEX_TILES 8

#define UNLOCK_ALL 1

typedef enum { MODE_FISHING, MODE_AWARD, MODE_JUKEBOX, MODE_CANVAS, MODE_PAINT } mode;

typedef enum { STATE_POLE, STATE_WINDING, STATE_CASTING, STATE_CAST, STATE_HOOKED, STATE_REELING } state;

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

RayCollision GetRayCollisionModel(Ray ray, Model model) {
  RayCollision collision = { 0 };

  for (int i = 0; i < model.meshCount; i++) {
    RayCollision newCollision = GetRayCollisionMesh(ray, model.meshes[i], model.transform);
    if (newCollision.hit && (!collision.hit || collision.distance > newCollision.distance)) {
      collision = newCollision;
    }
  }

  return collision;
}

RayCollision playerCollision(Ray ray, Vector3 offsets[], int offsetCount, Model objects[], int objectCount) {
  RayCollision collision = { 0 };
  Vector3 originalPosition = ray.position;

  for (int i = 0; i < offsetCount; i++) {
    ray.position = Vector3Add(originalPosition, offsets[i]);
    for (int j = 0; j < objectCount; j++) {
      RayCollision newCollision = GetRayCollisionModel(ray, objects[j]);
      if (newCollision.hit && (!collision.hit || collision.distance > newCollision.distance)) {
        collision = newCollision;
      }
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

void playerPhysics(Camera3D* camera, Vector3 oldPos, Model objects[], int objectCount, Model ladder) {
  Vector3 offsets[2] = { { 0, 0, 0 }, { 0, -2, 0 } };
  int offsetCount = sizeof(offsets)/sizeof(Vector3);
  Vector3 movement = Vector3Subtract(camera->position, oldPos);

  RayCollision collision = GetRayCollisionBox(RayTransform((Ray){ Vector3Subtract(camera->position, (Vector3){ 0, 7, 0 }), movement }, MatrixInvert(ladder.transform)), GetMeshBoundingBox(ladder.meshes[0]));
  bool climbing = collision.hit && collision.distance < 10;

  if (climbing) {
    camera->position = oldPos;
    camera->position.y += 12*GetFrameTime();
  } else {
    collision = playerCollision((Ray){ oldPos, movement }, offsets, offsetCount, objects, objectCount);
    if (collision.hit && collision.distance <= 2) {
      if (collision.normal.y < 0) collision.normal.y = 0;
      Vector3 newPos = Vector3Add(oldPos, Vector3Reject(movement, collision.normal));
      camera->target = Vector3Add(camera->target, Vector3Subtract(newPos, camera->position));
      camera->position = newPos;

      movement = Vector3Subtract(camera->position, oldPos);
      collision = playerCollision((Ray){ oldPos, movement }, offsets, offsetCount, objects, objectCount);
      if (collision.hit && collision.distance <= 2) {
        camera->target = Vector3Add(camera->target, Vector3Subtract(oldPos, camera->position));
        camera->position = oldPos;
      }
    }

    collision = playerCollision((Ray){ camera->position, (Vector3){ 0, -1, 0 } }, offsets, 1, objects, objectCount);
    if (collision.hit && collision.distance >= 7 && !CheckCollisionBoxSphere(GetMeshBoundingBox(ladder.meshes[0]), Vector3Transform(Vector3Subtract(camera->position, (Vector3){ 0, 7, 0 }), MatrixInvert(ladder.transform)), 20)) {
      float fall = 40*GetFrameTime();
      camera->position.y -= fall;
      camera->target.y -= fall;
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

void randomizeUV(Model model) {
  for (int i = 0; i < model.meshCount; i++) {
    float* texcoords = (float*)MemAlloc(model.meshes[i].vertexCount*2*sizeof(float));
    for (int j = 0; j < model.meshes[i].vertexCount*2; j++) {
      texcoords[j] = (float)GetRandomValue(0,255)/255;
    }
    UpdateMeshBuffer(model.meshes[i], SHADER_LOC_VERTEX_TEXCOORD01, texcoords, model.meshes[i].vertexCount*2*sizeof(float), 0);
    MemFree(texcoords);
  }
}

void addAward(const char* caught[], int* caughtCount, const char* award) {
  int i = 0;
  while (i < *caughtCount) if (strcmp(award, caught[i++]) == 0) break;
  if (i == *caughtCount) {
    caught[i] = award;
    (*caughtCount)++;
  }
}

int main(void)
{
  // initialization
  //======================================================================================
  const int screenWidth = 1600;
  const int screenHeight = 900;

  InitWindow(screenWidth, screenHeight, "An Ark For The Amigalites");
  InitAudioDevice();

  mode mode = MODE_FISHING;
  state state = STATE_POLE;

  // noise
  fnl_state noise = fnlCreateState();
  noise.noise_type = FNL_NOISE_OPENSIMPLEX2;

  // assets
  Texture2D woodTexture = LoadTexture("assets/wood.jpg");
  Texture2D sailTexture = LoadTexture("assets/sail.jpg");
  Model ship = LoadModel("assets/ship.glb");
  randomizeUV(ship);
  ship.materials[2].maps[MATERIAL_MAP_DIFFUSE].texture = woodTexture; // sides
  ship.materials[3].maps[MATERIAL_MAP_DIFFUSE].texture = woodTexture; // masts
  ship.materials[4].maps[MATERIAL_MAP_DIFFUSE].texture = woodTexture; // deck
  ship.materials[5].maps[MATERIAL_MAP_DIFFUSE].texture = sailTexture; // sails
  ship.materials[5].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
  ship.transform = MatrixMultiply(ship.transform, MatrixScale(12, 12, 12));
  ship.transform = MatrixMultiply(ship.transform, MatrixTranslate(35, 22, 5));

  Model ladder = LoadModel("assets/ladder.glb");
  randomizeUV(ladder);
  ladder.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = woodTexture;
  ladder.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = DARKBROWN;
  ladder.meshMaterial[0] = 0;
  ladder.transform = MatrixMultiply(ladder.transform, MatrixScale(0.05f, 0.05f, 0.05f));
  ladder.transform = MatrixMultiply(ladder.transform, MatrixRotate((Vector3){0,1,0}, 7*PI/32));
  ladder.transform = MatrixMultiply(ladder.transform, MatrixTranslate(12.5f, -10, -9.5f));

  Model jukebox = LoadModel("assets/jukebox.glb");
  jukebox.meshes[3].triangleCount = 0; // the glass doesn't render correctly; disable it
  jukebox.transform = MatrixMultiply(jukebox.transform, MatrixScale(4.2f, 4.2f, 4.2f));
  jukebox.transform = MatrixMultiply(jukebox.transform, MatrixRotate((Vector3){0,1,0}, -7*PI/32));
  jukebox.transform = MatrixMultiply(jukebox.transform, MatrixTranslate(12.5, -5, 9.5));

  Model easel = LoadModel("assets/easel.glb");
  randomizeUV(easel);
  easel.materials[1].maps[MATERIAL_MAP_DIFFUSE].texture = woodTexture;
  easel.transform = MatrixMultiply(easel.transform, MatrixScale(0.5f, 0.5f, 0.5f));
  easel.transform = MatrixMultiply(easel.transform, MatrixRotate((Vector3){0,1,0}, PI/2));
  easel.transform = MatrixMultiply(easel.transform, MatrixTranslate(-18.6f, -1, 1.7f));

  Model canvas = LoadModel("assets/canvas.glb");
  canvas.transform = MatrixMultiply(canvas.transform, MatrixScale(0.2f, 0.2f, 0.2f));
  canvas.transform = MatrixMultiply(canvas.transform, MatrixRotate((Vector3){0,0,1}, 59*PI/64));
  canvas.transform = MatrixMultiply(canvas.transform, MatrixTranslate(-19.8f, 2.0f, -0.8f));

  Model paint = LoadModelFromMesh(GenMeshPlane(1, 1, 1, 1));
  paint.transform = MatrixMultiply(paint.transform, MatrixScale(6.80f, 1.0f, 9.44f));
  paint.transform = MatrixMultiply(paint.transform, MatrixRotate((Vector3){0,0,1}, 4.9905f));
  paint.transform = MatrixMultiply(paint.transform, MatrixTranslate(-19.398f, 2.093f, -0.780f));
  Image paintImg = LoadImageFromTexture(canvas.materials[11].maps[MATERIAL_MAP_DIFFUSE].texture);
  ImageResize(&paintImg, 1888, 1360);
  paint.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = LoadTextureFromImage(paintImg);
  Image paintImgModified = paintImg;
  Image paintSticker;
  Rectangle paintStickerRect = { 0 };
  float paintStickerScale = 1;

  const char* allStickers[] = {
    "Batman1.png",
    "Batman2.png",
    "Godzilla_Breath.gif",
    "Godzilla_Stare.gif",
    "Indiana_Jones.jpg",
    "Mothra.gif",
    "Ripley.jpg",
    "TMNT_Donatello.jpg",
    "TMNT.jpg",
    "TMNT_Leonardo.jpg",
    "TMNT_Michaelangelo.jpg",
    "TMNT_Raphael.jpg"
  };
  enum { allStickerCount = sizeof(allStickers)/sizeof(char*) };
  const char* caughtStickers[allStickerCount] = { 0 };
  int caughtStickerCount = 0;
  #if UNLOCK_ALL
  for (int i = 0; i < allStickerCount; i++) caughtStickers[i] = allStickers[i];
  caughtStickerCount = allStickerCount;
  #endif

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
  Vector3 bobberOnPolePos = { 0 };

  Texture2D goldTexture = LoadTexture("assets/exclamation.jpg");
  Model exclamation = LoadModel("assets/exclamation.glb");
  randomizeUV(exclamation);
  exclamation.materials[2].maps[MATERIAL_MAP_DIFFUSE].texture = goldTexture;
  exclamation.materials[2].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
  exclamation.transform = MatrixMultiply(exclamation.transform, MatrixScale(2, 2, 2));
  exclamation.transform = MatrixMultiply(exclamation.transform, MatrixRotate((Vector3){1,0,0}, PI));
  Vector3 exclamationPos = { 0 };

  Model cd = LoadModel("assets/cd.glb");
  bool cdCaught = false;

  Model waves = LoadModelFromMesh(GenMeshPlane(WAVE_SPAN, WAVE_SPAN, WAVE_PEAKS-1, WAVE_PEAKS-1));
  waves.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = LoadTexture("assets/waves/1.jpg");
  for (int i = 0, z = 0; z < WAVE_PEAKS; z++) {
    bool flipZ = z/(WAVE_PEAKS/WAVE_TEX_TILES)%2;
    for (int x = 0; x < WAVE_PEAKS; x++) {
      bool flipX = x/(WAVE_PEAKS/WAVE_TEX_TILES)%2;
      float xPreflip = fmodf(waves.meshes[0].texcoords[i]*WAVE_TEX_TILES, 1);
      waves.meshes[0].texcoords[i++] = flipX ? 1-xPreflip : xPreflip;
      float zPreflip = fmodf(waves.meshes[0].texcoords[i]*WAVE_TEX_TILES, 1);
      waves.meshes[0].texcoords[i++] = flipZ ? 1-zPreflip : zPreflip;
    }
  }
  UpdateMeshBuffer(waves.meshes[0], SHADER_LOC_VERTEX_TEXCOORD01, waves.meshes[0].texcoords, waves.meshes[0].vertexCount*2*sizeof(float), 0);
  waves.transform = MatrixTranslate(0, -13, 0);

  Texture2D skyTexture = LoadTexture("assets/sky.jpg");
  Model sky = LoadModelFromMesh(GenMeshSphere(WAVE_SPAN/2, 64, 64));
  sky.transform = MatrixTranslate(0, -200, 0);
  sky.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = skyTexture;

  Model collisionObjects[4] = { ship, jukebox, easel, canvas };
  int collisionObjectCount = sizeof(collisionObjects)/sizeof(Model);

  Font font = LoadFont("assets/font.ttf");
  const char* award;
  int awardIndex;

  const char** menuItems;
  int menuItemCount = 0;
  int menuSelected = 0;

  Music oceanSounds = LoadMusicStream("assets/ocean.mp3");
  SetMusicVolume(oceanSounds, 2);
  PlayMusicStream(oceanSounds);

  // NOTE LoadSound and LoadWave appear to be broken in wasm so Music it is
  Music hookedSfx = LoadMusicStream("assets/hooked.mp3");
  SetMusicVolume(hookedSfx, 0.3f);
  SetMusicPitch(hookedSfx, 1.5f);
  hookedSfx.looping = false;

  Music music = { 0 };
  const char* allSongs[] = {
    "Call_Me.mp3",
    "He_Stopped_Loving_Her_Today.mp3",
    "Master_of_Puppets.mp3",
    "Super_Freak.mp3",
    "Don't_You_Want_Me.mp3",
    "I_Love_Rock_n_Roll.mp3",
    "Miami_2017.mp3",
    "Tainted_Love.mp3",
    "Electric_Avenue.mp3",
    "Karma_Chameleon.mp3",
    "Push_It.mp3",
    "You_Spin_Me_Round.mp3",
    "Sailor_Moon.mp3",
  };
  enum { allSongCount = sizeof(allSongs)/sizeof(char*) };
  const char* caughtSongs[allSongCount] = { 0 };
  int caughtSongCount = 0;
  #if UNLOCK_ALL
  for (int i = 0; i < allSongCount; i++) caughtSongs[i] = allSongs[i];
  caughtSongCount = allSongCount;
  #endif

  Camera camera = { 0 };
  camera.position = (Vector3){ 0, 2, 0 };
  camera.target = (Vector3){ 2, 2, 0 };
  camera.up = (Vector3){ 0, 1, 0 };
  camera.fovy = 60.0f;
  camera.projection = CAMERA_PERSPECTIVE;

  DisableCursor();

  SetTargetFPS(60);

  // main loop
  //======================================================================================
  while (!WindowShouldClose()) {
    // update
    //====================================================================================

    UpdateMusicStream(oceanSounds);
    UpdateMusicStream(hookedSfx);
    UpdateMusicStream(music);

    genWaveVertices(&noise, waves.meshes[0].vertices);
    UpdateMeshBuffer(waves.meshes[0], SHADER_LOC_VERTEX_POSITION, waves.meshes[0].vertices, waves.meshes[0].vertexCount*3*sizeof(float), 0);

    switch (mode) {

    case MODE_AWARD:
      if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        mode = MODE_FISHING;
      }
      break;

    case MODE_JUKEBOX:
    case MODE_CANVAS:
      if (IsKeyPressed(KEY_Q)) {
        mode = MODE_FISHING;
        break;
      }

      if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) menuSelected++;
      if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) menuSelected--;
      menuSelected %= menuItemCount;

      if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
        if (mode == MODE_JUKEBOX) {
          UnloadMusicStream(music);
          char file[100] = "assets/songs/";
          strncat(file, menuItems[menuSelected], 87);
          music = LoadMusicStream(file);
          music.looping = false;
          PlayMusicStream(music);
          mode = MODE_FISHING;
        } else /* mode == MODE_CANVAS */ {
          char file[100] = "assets/stickers/";
          strncat(file, menuItems[menuSelected], 84);
          paintSticker = LoadImage(file);
          ImageRotateCCW(&paintSticker);
          paintImgModified = paintImg;
          EnableCursor();
          mode = MODE_PAINT;
        }
      }

      break;

    case MODE_PAINT:
      if (IsKeyPressed(KEY_Q)) {
        UpdateTexture(paint.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture, paintImg.data);
        DisableCursor();
        mode = MODE_CANVAS;
        break;
      }

      paintStickerScale *= 1+GetMouseWheelMove()/4;
      if (paintStickerScale <= 0) paintStickerScale = 0.1f;
      paintStickerRect.width = 0.1f*paintStickerScale*paintImg.width;
      paintStickerRect.height = 0.1f*paintStickerScale*((float)paintSticker.height/paintSticker.width)*paintImg.height;

      RayCollision collision = GetRayCollisionModel(GetScreenToWorldRay(GetMousePosition(), camera), paint);
      if (collision.hit) {
        Vector3 p = Vector3Transform(collision.point, MatrixInvert(paint.transform));
        paintStickerRect.x = paintImg.width*(p.x+0.5f) - paintStickerRect.width/2;
        paintStickerRect.y = paintImg.height*(p.z+0.5f) - paintStickerRect.height/2;
      }

      paintImgModified = ImageCopy(paintImg);
      ImageDraw(&paintImgModified, paintSticker, (Rectangle){ 0, 0, paintSticker.width, paintSticker.height }, paintStickerRect, WHITE);
      UpdateTexture(paint.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture, paintImgModified.data);

      if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        paintImg = paintImgModified;
        DisableCursor();
        mode = MODE_FISHING;
      }

      break;

    case MODE_FISHING:
      {
        Vector3 oldPos = camera.position;
        updateCamera(&camera);
        playerPhysics(&camera, oldPos, collisionObjects, collisionObjectCount, ladder);
      }

      if (IsKeyPressed(KEY_E)) {
        Vector3 look = Vector3Subtract(camera.target, camera.position);
        RayCollision collision = GetRayCollisionModel((Ray){ camera.position, look }, jukebox);
        if (collision.hit && collision.distance < 5 && caughtSongCount >= 1) {
          menuItems = caughtSongs;
          menuItemCount = caughtSongCount;
          menuSelected = caughtSongCount-1;
          mode = MODE_JUKEBOX;
          break;
        }

        look = Vector3Subtract(camera.target, camera.position);
        collision = GetRayCollisionModel((Ray){ camera.position, look }, canvas);
        if (collision.hit && collision.distance < 5 && caughtStickerCount >= 1) {
          menuItems = caughtStickers;
          menuItemCount = caughtStickerCount;
          menuSelected = caughtStickerCount-1;
          mode = MODE_CANVAS;
          break;
        }
      }

      if (state == STATE_POLE && (IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))) {
        state = STATE_WINDING;
      }

      if (state == STATE_WINDING && (IsKeyDown(KEY_SPACE) || IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))) {
        poleCastAngle += 0.75f*PI*GetFrameTime();
      } else {
        poleCastAngle -= 6*PI*GetFrameTime();
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
      bobberOnPolePos = Vector3Transform(Vector3Zero(), MatrixMultiply(MatrixMultiply(MatrixTranslate(0, -0.2f, -4.5f), matRotation), matTranslation));

      if (state == STATE_POLE || state == STATE_WINDING) {
        bobberPos = bobberOnPolePos;
        pole.materials[3].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        pole.materials[4].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
      } else {
        pole.materials[3].maps[MATERIAL_MAP_DIFFUSE].color = BLANK;
        pole.materials[4].maps[MATERIAL_MAP_DIFFUSE].color = BLANK;
      }

      if (state == STATE_WINDING && (IsKeyReleased(KEY_SPACE) || IsMouseButtonReleased(MOUSE_BUTTON_LEFT) || IsGamepadButtonReleased(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))) {
        if (poleCastAngle > PI/8) {
          bobberVel = Vector3Scale(forward, 30*poleCastAngle);
          state = STATE_CASTING;
        } else {
          state = STATE_POLE;
        }
      }

      if (state == STATE_CASTING) {
        Vector3 oldBobberPos = bobberPos;
        bobberVel = Vector3ClampValue(Vector3Add(bobberVel, (Vector3){ 0, -30*GetFrameTime(), 0 }), 0, 100000);
        bobberPos = Vector3Add(bobberPos, Vector3Scale(bobberVel, GetFrameTime()));

        RayCollision collision = GetRayCollisionModel((Ray){ oldBobberPos, (Vector3){ 0, -1, 0 } }, waves);
        if (collision.hit && collision.distance < 0.1) {
          bobberPos = collision.point;
          state = STATE_CAST;
        } else if (bobberPos.y < -13) {
          state = STATE_CAST;
        }
      }

      if (state == STATE_CAST || state == STATE_HOOKED) {
        bobberPos.y = 0;
        RayCollision collision = GetRayCollisionModel((Ray){ bobberPos, (Vector3){ 0, -1, 0 } }, waves);
        bobberPos.y = collision.point.y;

        if (state == STATE_CAST && randomEvent(15)) {
          state = STATE_HOOKED;
          PlayMusicStream(hookedSfx);
        }

        if (IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
          cdCaught = state == STATE_HOOKED;
          state = STATE_REELING;
        }
      }

      if (state == STATE_HOOKED) {
        exclamationPos = bobberPos;
        exclamationPos.y += 6;

        if (randomEvent(3)) state = STATE_CAST;
      }

      if (state == STATE_REELING) {
        bobberPos = Vector3MoveTowards(bobberPos, bobberOnPolePos, 2);
        if (Vector3Equals(bobberPos, bobberOnPolePos)) {
          if (cdCaught) {
            int rand = GetRandomValue(0, allSongCount+allStickerCount-1);
            if (rand < allSongCount) {
              award = allSongs[rand];
              addAward(caughtSongs, &caughtSongCount, award);
            } else {
              award = allStickers[rand-allSongCount];
              addAward(caughtStickers, &caughtStickerCount, award);
            }
            mode = MODE_AWARD;
          }
          cdCaught = false;
          state = STATE_POLE;
        }
        if (cdCaught) {
          cd.transform = MatrixMultiply(MatrixMultiply(MatrixScale(0.005f, 0.005f, 0.005f), MatrixRotateY(atan2f(forward.x, forward.z))), MatrixTranslate(bobberPos.x, bobberPos.y-0.8f, bobberPos.z));
        }
      }

      break;
    }

    // drawing
    //====================================================================================
    BeginDrawing();

      ClearBackground(SKYBLUE);

      BeginMode3D(camera);

        rlDisableBackfaceCulling();
          DrawModel(sky, camera.position, 1, WHITE);
          DrawModel(canvas, Vector3Zero(), 1, WHITE);
        rlEnableBackfaceCulling();
        DrawModel(waves, Vector3Zero(), 1, WHITE);

        DrawModel(ship, Vector3Zero(), 1, WHITE);
        DrawModel(ladder, Vector3Zero(), 1, WHITE);
        DrawModel(jukebox, Vector3Zero(), 1, WHITE);
        DrawModel(easel, Vector3Zero(), 1, WHITE);
        DrawModel(paint, Vector3Zero(), 1, WHITE);

        if (mode == MODE_FISHING || mode == MODE_AWARD) {
          DrawModel(pole, Vector3Zero(), 1, WHITE);
          DrawModel(bobber, bobberPos, 0.01f, RED);
          if (state != STATE_POLE && state != STATE_WINDING)
            DrawLine3D(bobberPos, bobberOnPolePos, WHITE);
          if (state == STATE_HOOKED) DrawModel(exclamation, exclamationPos, 1, WHITE);
          if (cdCaught) DrawModel(cd, Vector3Zero(), 1, WHITE);
        }

      EndMode3D();

      if (mode == MODE_AWARD) {
        DrawTextEx(font, "You Caught:", (Vector2){ 100, 200 }, 100, 10, GREEN);
        DrawTextEx(font, award, (Vector2){ 100, 300 }, 100, 10, GREEN);
        DrawTextEx(font, "Wow!", (Vector2){ 100, 400 }, 100, 10, GREEN);
      }

      if (mode == MODE_JUKEBOX || mode == MODE_CANVAS) {
        for (int i = 0; i < menuItemCount; i++) {
          DrawTextEx(font, menuItems[i], (Vector2){ 10, 50*i+60 }, 50, 5, i == menuSelected ? GREEN : RED);
        }
      }

      DrawFPS(10, 10);

    EndDrawing();
  }

  // de-initialization
  //======================================================================================
  // TODO
  CloseWindow();

  return 0;
}
