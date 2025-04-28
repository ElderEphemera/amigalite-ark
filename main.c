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
#define SHOW_FPS 1

typedef enum { MODE_INTRO, MODE_FISHING, MODE_AWARD, MODE_GOFISH, MODE_JUKEBOX, MODE_CANVAS, MODE_PAINT } mode;

typedef enum { STATE_POLE, STATE_WINDING, STATE_CASTING, STATE_CAST, STATE_HOOKED, STATE_REELING } state;

bool randomEvent(float avgSeconds) {
  return GetRandomValue(1, (int)128*avgSeconds/GetFrameTime()) <= 128;
}

float waveHeight(fnl_state* noise, float x, float z, float t) {
  return 5*fnlGetNoise3D(noise, 2*x, 2*z, 30*t);
}

void genWaveVertices(fnl_state* noise, float* vertices) {
  FNLfloat t = (FNLfloat)GetTime();

  int i = 0;
  for (int z = 0; z < WAVE_PEAKS; z++) {
    float zPos = ((float)z/WAVE_PEAKS - 0.5f)*WAVE_SPAN;
    for (int x = 0; x < WAVE_PEAKS; x++) {
      float xPos = ((float)x/WAVE_PEAKS - 0.5f)*WAVE_SPAN;
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
  // mouse
  float rotateSensitivity = 0.003f;
  Vector2 mousePositionDelta = GetMouseDelta();
  CameraYaw(camera, -mousePositionDelta.x*rotateSensitivity, false);
  CameraPitch(camera, -mousePositionDelta.y*rotateSensitivity, true, false, false);

  // keyboard
  float moveAmount = 15*GetFrameTime();
  if (IsKeyDown(KEY_W)) CameraMoveForward(camera, moveAmount, true);
  if (IsKeyDown(KEY_A)) CameraMoveRight(camera, -moveAmount, true);
  if (IsKeyDown(KEY_S)) CameraMoveForward(camera, -moveAmount, true);
  if (IsKeyDown(KEY_D)) CameraMoveRight(camera, moveAmount, true);
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

void addAward(const char* caught[], int* caughtCount, int* selected, const char* award) {
  int i = 0;
  while (i < *caughtCount) if (strcmp(award, caught[i++]) == 0) break;
  if (i == *caughtCount) {
    caught[i] = award;
    *selected = (*caughtCount)++;
  }
}

void drawIntro(Font font, const char* intro) {
  ClearBackground(BEIGE);
  Vector2 v = MeasureTextEx(font, intro, 32, 1);
  DrawTextEx(font, intro, (Vector2){ 800-v.x/2, 45 }, 32, 1, BLACK);
  Vector2 w = MeasureTextEx(font, "- BenchMaster", 32, 1);
  DrawTextEx(font, "- BenchMaster", (Vector2){ 800+v.x/2-w.x, 55+v.y }, 32, 1, BLACK);
}

void drawTextCentered(Font font, const char *text, float y, float fontSize, float spacing, Color tint) {
  float x = 800-MeasureTextEx(font, text, fontSize, spacing).x/2;
  DrawTextEx(font, text, (Vector2){ x, y }, fontSize, spacing, tint);
}

int main(void) {
  SetTraceLogLevel(LOG_WARNING);

  // initialization
  //======================================================================================
  const int screenWidth = 1600;
  const int screenHeight = 900;

  InitWindow(screenWidth, screenHeight, "An Ark For The Amigalites");
  InitAudioDevice();

  Font font = LoadFontEx("assets/font.ttf", 100, NULL, 95);
  int introSize;
  char* intro = LoadFileData("assets/intro.txt", &introSize);
  for (int i = 0; i < introSize-1; i++) {
    if (intro[i] == '\n' && intro[i+1] == '\n') intro[i++] = ' ';
  }
  intro[introSize-1] = '\0';

  BeginDrawing();
    drawIntro(font, intro);
    Vector2 v = MeasureTextEx(font, "Loading...", 40, 1);
    DrawTextEx(font, "Loading...", (Vector2){ 1590-v.x, 10 }, 40, 1, BLACK);
  EndDrawing();

  mode mode = MODE_INTRO;
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
  Image paintStickerScaled;
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
  int stickerSelected = 0;
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
  waves.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = LoadTexture("assets/waves.jpg");
  int i = 0;
  float ztex = 0;
  bool zdir = true;
  for (int z = 0; z < WAVE_PEAKS; z++) {
    float xtex = 0;
    bool xdir = true;
    for (int x = 0; x < WAVE_PEAKS; x++) {
      waves.meshes[0].texcoords[i++] = xtex;
      waves.meshes[0].texcoords[i++] = ztex;
      if (FloatEquals(xtex, 0) || FloatEquals(xtex, 1)) xdir = !xdir;
      xtex += (xdir?-1:1)*(float)WAVE_TEX_TILES/WAVE_PEAKS;
    }
    if (FloatEquals(ztex, 0) || FloatEquals(ztex, 1)) zdir = !zdir;
    ztex += (zdir?-1:1)*(float)WAVE_TEX_TILES/WAVE_PEAKS;
  }
  UpdateMeshBuffer(waves.meshes[0], SHADER_LOC_VERTEX_TEXCOORD01, waves.meshes[0].texcoords, waves.meshes[0].vertexCount*2*sizeof(float), 0);
  waves.transform = MatrixTranslate(0, -13, 0);

  Texture2D skyTexture = LoadTexture("assets/sky.jpg");
  Model sky = LoadModelFromMesh(GenMeshSphere((float)WAVE_SPAN/2, 64, 64));
  sky.transform = MatrixTranslate(0, -200, 0);
  sky.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = skyTexture;

  Model collisionObjects[4] = { ship, jukebox, easel, canvas };
  int collisionObjectCount = sizeof(collisionObjects)/sizeof(Model);

  Texture2D keyW = LoadTexture("assets/controls/key_w.gif");
  Texture2D keyA = LoadTexture("assets/controls/key_a.gif");
  Texture2D keyS = LoadTexture("assets/controls/key_s.gif");
  Texture2D keyD = LoadTexture("assets/controls/key_d.gif");
  Texture2D keyQ = LoadTexture("assets/controls/key_q.gif");
  Texture2D keyE = LoadTexture("assets/controls/key_e.gif");
  Texture2D keyH = LoadTexture("assets/controls/key_h.gif");
  Texture2D keySpace = LoadTexture("assets/controls/key_space.gif");
  Texture2D keyEnter = LoadTexture("assets/controls/key_enter.gif");
  Texture2D keyUp = LoadTexture("assets/controls/key_up.gif");
  Texture2D keyDown = LoadTexture("assets/controls/key_down.gif");
  Texture2D mouse0 = LoadTexture("assets/controls/mouse_0.png");
  Texture2D mouse1 = LoadTexture("assets/controls/mouse_1.png");
  Texture2D mouse2 = LoadTexture("assets/controls/mouse_2.png");
  bool showHelp = true;
  bool interactable = false;

  const char* award;
  int awardIndex;

  const char* goFishFor;

  const char** menuItems;
  int menuItemCount = 0;
  int* menuSelected;

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
  int songSelected = 0;
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

  SetTargetFPS(60);

  DisableCursor();

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

    if (IsKeyPressed(KEY_H)) showHelp = !showHelp;

    switch (mode) {

    case MODE_INTRO:
    case MODE_AWARD:
    case MODE_GOFISH:
      if (IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        mode = MODE_FISHING;
      }
      break;

    case MODE_JUKEBOX:
    case MODE_CANVAS:
      /*
      if (mode == MODE_JUKEBOX) {
        camera.position = (Vector3){ 9.651401f, 2.000000f, 13.026970f };
        camera.target = (Vector3){ 10.879786f, 1.585038f, 11.504169f };
      } else { // mode == MODE_CANVAS
        camera.position = (Vector3){ -13.245653f, 2.000000f, -0.788615f };
        camera.target = (Vector3){ -15.243155f, 1.900042f, -0.787433f };
      }
      */

      if (IsKeyPressed(KEY_Q)) {
        mode = MODE_FISHING;
        break;
      }

      if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) (*menuSelected)++;
      if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) (*menuSelected)--;
      *menuSelected += menuItemCount;
      *menuSelected %= menuItemCount;

      if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
        if (mode == MODE_JUKEBOX) {
          UnloadMusicStream(music);
          char file[100] = "assets/songs/";
          strncat(file, menuItems[*menuSelected], 87);
          music = LoadMusicStream(file);
          music.looping = false;
          PlayMusicStream(music);
          mode = MODE_FISHING;
        } else /* mode == MODE_CANVAS */ {
          char file[100] = "assets/stickers/";
          strncat(file, menuItems[*menuSelected], 84);
          paintSticker = LoadImage(file);
          ImageRotateCCW(&paintSticker);
          paintImgModified = ImageCopy(paintImg);
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

      Rectangle oldRect = paintStickerRect;

      paintStickerScale *= 1+GetMouseWheelMove()/4;
      paintStickerScale = Clamp(paintStickerScale, 0.1f, 10);
      paintStickerRect.width = 0.1f*paintStickerScale*paintImg.width;
      paintStickerRect.height = 0.1f*paintStickerScale*((float)paintSticker.height/paintSticker.width)*paintImg.height;

      RayCollision collision = GetRayCollisionModel(GetScreenToWorldRay(GetMousePosition(), camera), paint);
      if (collision.hit) {
        Vector3 p = Vector3Transform(collision.point, MatrixInvert(paint.transform));
        paintStickerRect.x = paintImg.width*(p.x+0.5f) - paintStickerRect.width/2;
        paintStickerRect.y = paintImg.height*(p.z+0.5f) - paintStickerRect.height/2;
      }

      if (
        !FloatEquals(paintStickerRect.width, paintStickerScaled.width) ||
        !FloatEquals(paintStickerRect.height, paintStickerScaled.height)
      ) {
        if (IsImageValid(paintStickerScaled)) UnloadImage(paintStickerScaled);
        paintStickerScaled = ImageCopy(paintSticker);
        ImageResizeNN(&paintStickerScaled, paintStickerRect.width, paintStickerRect.height);
      }

      if (
        !FloatEquals(paintStickerRect.x, oldRect.x) ||
        !FloatEquals(paintStickerRect.y, oldRect.y) ||
        !FloatEquals(paintStickerRect.width, oldRect.width) ||
        !FloatEquals(paintStickerRect.height, oldRect.height)
      ) {
        UnloadImage(paintImgModified);
        paintImgModified = ImageCopy(paintImg);
        ImageDraw(&paintImgModified, paintStickerScaled, (Rectangle){ 0, 0, paintStickerScaled.width, paintStickerScaled.height }, paintStickerRect, WHITE);
        UpdateTexture(paint.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture, paintImgModified.data);
      }

      if (IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        UnloadImage(paintImg);
        paintImg = ImageCopy(paintImgModified);
        UnloadImage(paintImgModified);
        DisableCursor();
        mode = MODE_FISHING;
      }

      break;

    case MODE_FISHING:
      ;Vector3 oldPos = camera.position;
      updateCamera(&camera);
      playerPhysics(&camera, oldPos, collisionObjects, collisionObjectCount, ladder);

      Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

      collision = GetRayCollisionModel((Ray){ camera.position, forward }, jukebox);
      bool jukeboxInteractable = collision.hit && collision.distance < 8;
      if (jukeboxInteractable && IsKeyPressed(KEY_E)) {
        interactable = false;
        if (caughtSongCount >= 1) {
          menuItems = caughtSongs;
          menuItemCount = caughtSongCount;
          menuSelected = &songSelected;
          mode = MODE_JUKEBOX;
        } else {
          goFishFor = "Songs";
          mode = MODE_GOFISH;
        }
        break;
      }

      collision = GetRayCollisionModel((Ray){ camera.position, forward }, canvas);
      bool canvasInteractable = collision.hit && collision.distance < 8;
      if (canvasInteractable && IsKeyPressed(KEY_E)) {
        interactable = false;
        if (caughtStickerCount >= 1) {
          menuItems = caughtStickers;
          menuItemCount = caughtStickerCount;
          menuSelected = &stickerSelected;
          mode = MODE_CANVAS;
        } else {
          goFishFor = "Stickers";
          mode = MODE_GOFISH;
        }
        break;
      }

      interactable = jukeboxInteractable || canvasInteractable;

      if (state == STATE_POLE && (IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
        state = STATE_WINDING;
      }

      if (state == STATE_WINDING && (IsKeyDown(KEY_SPACE) || IsMouseButtonDown(MOUSE_BUTTON_LEFT))) {
        poleCastAngle += 0.75f*PI*GetFrameTime();
      } else {
        poleCastAngle -= 6*PI*GetFrameTime();
      }
      poleCastAngle = Clamp(poleCastAngle, 0, 2*PI/3);

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

      if (state == STATE_WINDING && (IsKeyReleased(KEY_SPACE) || IsMouseButtonReleased(MOUSE_BUTTON_LEFT))) {
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

        if (IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
          cdCaught = state == STATE_HOOKED;
          state = STATE_REELING;
        }
      }

      if (state == STATE_HOOKED) {
        exclamationPos = bobberPos;
        exclamationPos.y += 6;

        if (randomEvent(4)) state = STATE_CAST;
      }

      if (state == STATE_REELING) {
        bobberPos = Vector3MoveTowards(bobberPos, bobberOnPolePos, 2);
        if (Vector3Equals(bobberPos, bobberOnPolePos)) {
          if (cdCaught) {
            int rand = GetRandomValue(0, allSongCount+allStickerCount-1);
            if (rand < allSongCount) {
              award = allSongs[rand];
              addAward(caughtSongs, &caughtSongCount, &songSelected, award);
            } else {
              award = allStickers[rand-allSongCount];
              addAward(caughtStickers, &caughtStickerCount, &stickerSelected, award);
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

      if (mode == MODE_INTRO) {
        drawIntro(font, intro);
        Vector2 v = MeasureTextEx(font, "Press Space To Continue", 40, 1);
        DrawTextEx(font, "Press Space To Continue", (Vector2){ 1590-v.x, 10 }, 40, 1, BLACK);
      } else {
        ClearBackground(BLACK);

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
      }

      switch (mode) {
      case MODE_AWARD:
        DrawRectangle(450, 350, 700, 200, BEIGE);
        drawTextCentered(font, "You Caught:", 370, 50, 5, BLACK);
        drawTextCentered(font, award, 425, 50, 5, BLACK);
        drawTextCentered(font, "Wow!", 480, 50, 5, BLACK);
        break;

      case MODE_GOFISH:
        DrawRectangle(550, 350, 500, 200, BEIGE);
        drawTextCentered(font, "You Don't Have Any", 370, 50, 5, BLACK);
        drawTextCentered(font, goFishFor, 425, 50, 5, BLACK);
        drawTextCentered(font, "Go Fish!", 480, 50, 5, BLACK);
        break;

      case MODE_JUKEBOX:
      case MODE_CANVAS:
        DrawRectangle(400, 50, 800, 800, BEIGE);
        int start = Clamp(*menuSelected-6, 0, Clamp(menuItemCount-13, 0, menuItemCount));
        int end = Clamp(start+13, 0, menuItemCount);
        for (int y = 60, i = start; i < end; y+=55, i++) {
          bool ellipses = i+1 == end && end != menuItemCount || i == start && start != 0;
          const char* item = ellipses ? "..." : menuItems[i];
          drawTextCentered(font, item, y, 50, 5, i == *menuSelected ? DARKPURPLE : BLACK);
        }
        break;
      }

      if (showHelp) {
        switch (mode) {

        case MODE_FISHING:
          if (interactable)
          DrawTextureEx(keyE    , (Vector2){ 1600-10-28         , 10+32+32+42+32 }, 0, 2, WHITE);
          DrawTextureEx(keyW    , (Vector2){ 1600-10-30-32-32   , 10             }, 0, 2, WHITE);
          DrawTextureEx(keyA    , (Vector2){ 1600-10-30-32-32-32, 10+32          }, 0, 2, WHITE);
          DrawTextureEx(keyS    , (Vector2){ 1600-10-30-32-32   , 10+32          }, 0, 2, WHITE);
          DrawTextureEx(keyD    , (Vector2){ 1600-10-30-32      , 10+32          }, 0, 2, WHITE);
          DrawTextureEx(mouse0  , (Vector2){ 1600-10-30         , 10+12          }, 0, 2, WHITE);
          DrawTextureEx(keySpace, (Vector2){ 1600-10-30-32-32+6 , 10+32+32+4     }, 0, 2, WHITE);
          DrawTextureEx(mouse1  , (Vector2){ 1600-10-30         , 10+32+32       }, 0, 2, WHITE);
          DrawTextureEx(keyH    , (Vector2){ 1600-10-28         , 10+32+32+42    }, 0, 2, WHITE);
          break;

        case MODE_AWARD:
        case MODE_GOFISH:
          DrawTextureEx(keySpace, (Vector2){ 1600-10-30-32-32+6, 10+4  }, 0, 2, WHITE);
          DrawTextureEx(mouse1  , (Vector2){ 1600-10-30        , 10    }, 0, 2, WHITE);
          DrawTextureEx(keyH    , (Vector2){ 1600-10-28        , 10+52 }, 0, 2, WHITE);
          break;

        case MODE_JUKEBOX:
        case MODE_CANVAS:
          DrawTextureEx(keyW    , (Vector2){ 1600-10-72-32-32, 10          }, 0, 2, WHITE);
          DrawTextureEx(keyA    , (Vector2){ 1600-10-72-32   , 10          }, 0, 2, WHITE);
          DrawTextureEx(keySpace, (Vector2){ 1600-10-54      , 10          }, 0, 2, WHITE);
          DrawTextureEx(keyUp   , (Vector2){ 1600-10-72-32-32, 10+32       }, 0, 2, WHITE);
          DrawTextureEx(keyDown , (Vector2){ 1600-10-72-32   , 10+32       }, 0, 2, WHITE);
          DrawTextureEx(keyEnter, (Vector2){ 1600-10-72      , 10+32       }, 0, 2, WHITE);
          DrawTextureEx(keyQ    , (Vector2){ 1600-10-30      , 10+32+32    }, 0, 2, WHITE);
          DrawTextureEx(keyH    , (Vector2){ 1600-10-28      , 10+32+32+32 }, 0, 2, WHITE);
          break;

        case MODE_PAINT:
          DrawTextureEx(mouse0  , (Vector2){ 1600-10-30-30      , 10          }, 0, 2, WHITE);
          DrawTextureEx(mouse2  , (Vector2){ 1600-10-30         , 10          }, 0, 2, WHITE);
          DrawTextureEx(keySpace, (Vector2){ 1600-10-30-32-32+6 , 10+42       }, 0, 2, WHITE);
          DrawTextureEx(mouse1  , (Vector2){ 1600-10-30         , 10+42       }, 0, 2, WHITE);
          DrawTextureEx(keyQ    , (Vector2){ 1600-10-30         , 10+42+42    }, 0, 2, WHITE);
          DrawTextureEx(keyH    , (Vector2){ 1600-10-28         , 10+42+42+32 }, 0, 2, WHITE);
          break;

        }
      }

      #if SHOW_FPS
      DrawFPS(10, 10);
      #endif

    EndDrawing();
  }

  // de-initialization
  //======================================================================================
  CloseWindow();

  return 0;
}
