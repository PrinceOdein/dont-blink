#include <raylib.h>
#include <raymath.h>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <string>

// ═══════════════════════════════════════════════════════════════
//  CONSTANTS
// ═══════════════════════════════════════════════════════════════
static const float ROOM_SIZE = 24.f;
static const float DOOR_HALF = 3.f;
static const float WALL_H = 3.8f;
static const float MOUSE_SENS = 0.0018f;
static const float BLINK_DUR = 0.18f;
static const float BLINK_BASE = 4.0f;
static const int   MAX_ENEMY = 24;
static const int   SW = 1900;
static const int   SH = 1000;

// ═══════════════════════════════════════════════════════════════
//  TYPES
// ═══════════════════════════════════════════════════════════════
struct Wall { Vector3 pos, size; Color col; };

enum EnemyType { PATROLLER, SENTINEL };
enum EnemyState { E_PATROL, E_CHASE, E_IDLE };
enum GameState { TITLE, HOW_TO_PLAY, PLAYING, PAUSED, GAME_OVER };

struct Enemy {
    Vector3    pos;
    float      speed, radius = 0.44f;
    EnemyType  type;
    EnemyState state;
    bool       activated = false;
    float      detectRange;
    float      lostTimer = 0.f;
    std::vector<Vector3> waypoints;
    int        wpIdx = 0;
    float      waitTime = 0.f;
    float      alertFlash = 0.f;  // flashes when first alerted
};

struct Player {
    Vector3 pos = { 0.f, 1.7f, 0.f };
    float   yaw = 0.f, pitch = 0.f;
    float   speed = 5.0f, radius = 0.35f;
    float   stamina = 100.f;
    bool    sprinting = false;
};

struct Room {
    int  gx, gz;
    bool doors[4] = {};
    std::vector<Wall> walls;
    struct EnemyDef {
        EnemyType type; Vector3 pos; float speed;
        std::vector<Vector3> waypoints;
    };
    std::vector<EnemyDef> enemyDefs;
    bool enemiesSpawned = false;
};
using RoomMap = std::unordered_map<uint64_t, Room>;

// ═══════════════════════════════════════════════════════════════
//  UTILITY
// ═══════════════════════════════════════════════════════════════
static uint64_t roomKey(int gx, int gz) {
    return ((uint64_t)(uint32_t)gx << 32) | (uint32_t)gz;
}
static uint32_t pcg(uint32_t s) {
    s = s * 747796405u + 2891336453u;
    uint32_t w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
struct RNG {
    uint32_t s;
    RNG(uint32_t seed) : s(pcg(seed ^ 0xDEADBEEFu)) {}
    uint32_t next() { return s = pcg(s); }
    int   rInt(int lo, int hi) { return lo + (int)(next() % (uint32_t)(hi - lo + 1)); }
    float rF() { return (float)(next() & 0xFFFFFF) / (float)0xFFFFFF; }
    bool  rPct(int p) { return (int)(next() % 100u) < p; }
};

// Draw text with drop-shadow
static void DrawTextShadow(const char* txt, int x, int y, int fs, Color c) {
    DrawText(txt, x + 2, y + 2, fs, { 0,0,0,160 });
    DrawText(txt, x, y, fs, c);
}
// Centered text with shadow
static void DrawTextCentered(const char* txt, int y, int fs, Color c) {
    DrawTextShadow(txt, SW / 2 - MeasureText(txt, fs) / 2, y, fs, c);
}
// Pulsing alpha (0..1 sine)
static unsigned char pulseAlpha(float t, float speed, unsigned char hi) {
    return (unsigned char)((sinf(t * speed) * .5f + .5f) * hi);
}

// ═══════════════════════════════════════════════════════════════
//  DOOR CONSISTENCY
// ═══════════════════════════════════════════════════════════════
static bool hasDoor(int gx, int gz, int dir) {
    if (dir == 2) return hasDoor(gx, gz + 1, 0);
    if (dir == 3) return hasDoor(gx - 1, gz, 1);
    uint32_t h = (dir == 0)
        ? pcg(pcg((uint32_t)(gx + 32768) * 1234567u ^ (uint32_t)(gz + 32768) * 7654321u) ^ 0xA1B2u)
        : pcg(pcg((uint32_t)(gx + 32768) * 9876543u ^ (uint32_t)(gz + 32768) * 3456789u) ^ 0xD4E5u);
    return (h % 100u) < 72u;
}

// ═══════════════════════════════════════════════════════════════
//  BUILD ROOM
// ═══════════════════════════════════════════════════════════════
static void buildRoom(Room& room) {
    const float RS2 = ROOM_SIZE * .5f;
    const float DH = DOOR_HALF;
    const float H = WALL_H, hy = H * .5f, T = .5f;
    const float cx = room.gx * ROOM_SIZE;
    const float cz = room.gz * ROOM_SIZE;
    const float lw = RS2 - DH;

    Color ext = { 48,43,38,255 }, inr = { 34,30,26,255 }, pil = { 60,55,50,255 };

    RNG rng(pcg((uint32_t)(room.gx + 32768) * 2654435761u ^
        (uint32_t)(room.gz + 32768) * 2246822519u));

    auto W = [&](float wx, float wz, float xs, float zs, Color c) {
        room.walls.push_back({ {wx,hy,wz},{xs,H,zs},c });
        };

    // ── NORTH ──
    if (!room.doors[0]) W(cx, cz - RS2, ROOM_SIZE, T, ext);
    else {
        W(cx - (RS2 + DH) * .5f, cz - RS2, lw, T, ext);
        W(cx + (RS2 + DH) * .5f, cz - RS2, lw, T, ext);
        W(cx - DH, cz - RS2, T, T * 2.f, pil);
        W(cx + DH, cz - RS2, T, T * 2.f, pil);
    }
    // ── SOUTH ──
    if (!room.doors[2]) W(cx, cz + RS2, ROOM_SIZE, T, ext);
    else {
        W(cx - (RS2 + DH) * .5f, cz + RS2, lw, T, ext);
        W(cx + (RS2 + DH) * .5f, cz + RS2, lw, T, ext);
        W(cx - DH, cz + RS2, T, T * 2.f, pil);
        W(cx + DH, cz + RS2, T, T * 2.f, pil);
    }
    // ── EAST ──
    if (!room.doors[1]) W(cx + RS2, cz, T, ROOM_SIZE, ext);
    else {
        W(cx + RS2, cz - (RS2 + DH) * .5f, T, lw, ext);
        W(cx + RS2, cz + (RS2 + DH) * .5f, T, lw, ext);
        W(cx + RS2, cz - DH, T * 2.f, T, pil);
        W(cx + RS2, cz + DH, T * 2.f, T, pil);
    }
    // ── WEST ──
    if (!room.doors[3]) W(cx - RS2, cz, T, ROOM_SIZE, ext);
    else {
        W(cx - RS2, cz - (RS2 + DH) * .5f, T, lw, ext);
        W(cx - RS2, cz + (RS2 + DH) * .5f, T, lw, ext);
        W(cx - RS2, cz - DH, T * 2.f, T, pil);
        W(cx - RS2, cz + DH, T * 2.f, T, pil);
    }

    // ── PILLARS ──
    float pm = RS2 * .55f;
    for (int xi = -1; xi <= 1; xi += 2)
        for (int zi = -1; zi <= 1; zi += 2)
            if (rng.rPct(52)) W(cx + xi * pm, cz + zi * pm, .68f, .68f, pil);

    // ── DEBRIS ──
    int doorCnt = room.doors[0] + room.doors[1] + room.doors[2] + room.doors[3];
    int debN = doorCnt <= 1 ? rng.rInt(3, 6)
        : doorCnt == 2 ? rng.rInt(1, 3) : rng.rInt(0, 2);
    for (int i = 0; i < debN; i++) {
        float dbX = cx + rng.rF() * ROOM_SIZE * .72f - ROOM_SIZE * .36f;
        float dbZ = cz + rng.rF() * ROOM_SIZE * .72f - ROOM_SIZE * .36f;
        float sw2 = .8f + rng.rF() * .8f, sh2 = H * .18f + rng.rF() * H * .28f, sd = .8f + rng.rF() * .8f;
        room.walls.push_back({ {dbX,sh2 * .5f,dbZ},{sw2,sh2,sd},inr });
    }

    // ── ENEMY DEFS ──
    if (room.gx == 0 && room.gz == 0) return;
    int eN = rng.rPct(68) ? (rng.rPct(35) ? 2 : 1) : 0;
    for (int i = 0; i < eN; i++) {
        Room::EnemyDef def;
        def.type = rng.rPct(42) ? PATROLLER : SENTINEL;
        def.speed = 1.8f + rng.rF() * 1.6f;
        float m = ROOM_SIZE - 4.f;
        def.pos = { cx + rng.rF() * m - m * .5f, 0.9f, cz + rng.rF() * m - m * .5f };
        if (def.type == PATROLLER)
            for (int w = 0; w < rng.rInt(3, 5); w++)
                def.waypoints.push_back({ cx + rng.rF() * m - m * .5f, 0.9f, cz + rng.rF() * m - m * .5f });
        room.enemyDefs.push_back(def);
    }
}

// ═══════════════════════════════════════════════════════════════
//  COLLISION
// ═══════════════════════════════════════════════════════════════
static bool hitsWall(Vector3 p, float r, const RoomMap& rooms) {
    int pgx = (int)roundf(p.x / ROOM_SIZE), pgz = (int)roundf(p.z / ROOM_SIZE);
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
        auto it = rooms.find(roomKey(pgx + dx, pgz + dz));
        if (it == rooms.end()) continue;
        for (auto& w : it->second.walls) {
            float nx = Clamp(p.x, w.pos.x - w.size.x * .5f, w.pos.x + w.size.x * .5f);
            float nz = Clamp(p.z, w.pos.z - w.size.z * .5f, w.pos.z + w.size.z * .5f);
            float ex = p.x - nx, ez = p.z - nz;
            if (ex * ex + ez * ez < r * r) return true;
        }
    }
    return false;
}
static Vector3 slideMove(Vector3 from, Vector3 to, float r, const RoomMap& rooms) {
    if (!hitsWall(to, r, rooms)) return to;
    Vector3 tx = { to.x, from.y, from.z };
    if (!hitsWall(tx, r, rooms)) return tx;
    Vector3 tz = { from.x, from.y, to.z };
    if (!hitsWall(tz, r, rooms)) return tz;
    return from;
}

// ═══════════════════════════════════════════════════════════════
//  ROOM MANAGER
// ═══════════════════════════════════════════════════════════════
static void ensureRooms(int pgx, int pgz, RoomMap& rooms,
    std::vector<Enemy>& enemies, float survivalTime) {
    for (int dx = -3; dx <= 3; dx++) for (int dz = -3; dz <= 3; dz++) {
        uint64_t key = roomKey(pgx + dx, pgz + dz);
        if (rooms.count(key)) continue;
        Room room;
        room.gx = pgx + dx; room.gz = pgz + dz;
        for (int d = 0; d < 4; d++) room.doors[d] = hasDoor(room.gx, room.gz, d);
        if (!room.doors[0] && !room.doors[1] && !room.doors[2] && !room.doors[3])
            room.doors[0] = true;
        buildRoom(room);
        rooms[key] = std::move(room);
    }
    for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) {
        auto it = rooms.find(roomKey(pgx + dx, pgz + dz));
        if (it == rooms.end() || it->second.enemiesSpawned) continue;
        for (auto& def : it->second.enemyDefs) {
            if ((int)enemies.size() >= MAX_ENEMY) break;
            Enemy e;
            e.pos = def.pos;
            e.type = def.type;
            e.speed = std::min(def.speed + survivalTime * .009f, 5.8f);
            e.state = (def.type == PATROLLER) ? E_PATROL : E_IDLE;
            e.detectRange = (def.type == PATROLLER) ? 12.f : 7.5f;
            e.waypoints = def.waypoints;
            enemies.push_back(e);
        }
        it->second.enemiesSpawned = true;
    }
}

// ═══════════════════════════════════════════════════════════════
//  BUILD FLASHLIGHT TEXTURE
// ═══════════════════════════════════════════════════════════════
static Texture2D buildFlashTex() {
    const int FW = 960, FH = 500;
    Image fi = GenImageColor(FW, FH, BLANK);
    for (int y = 0; y < FH; y++) for (int x = 0; x < FW; x++) {
        float dx = (float)(x - FW / 2), dy = (float)(y - FH / 2);
        float d = sqrtf(dx * dx + dy * dy);
        const float iR = 168.f, oR = 268.f;
        unsigned char a;
        if (d < iR) a = 0;
        else if (d < oR) { float t = (d - iR) / (oR - iR); a = (unsigned char)(t * t * t * 252); }
        else a = 252;
        ImageDrawPixel(&fi, x, y, { 0,0,0,a });
    }
    Texture2D t = LoadTextureFromImage(fi);
    UnloadImage(fi);
    return t;
}

// ═══════════════════════════════════════════════════════════════
//  TITLE SCREEN RENDERER
// ═══════════════════════════════════════════════════════════════
static void DrawTitleScreen(float t, bool* startPressed, bool* howToPressed) {
    // Animated noise scanlines
    for (int y = 0; y < SH; y += 4) {
        unsigned char grain = (unsigned char)(20 + (rand() % 8));
        DrawRectangle(0, y, SW, 2, { grain, grain, grain, 255 });
    }

    // Vignette
    DrawRectangleGradientV(0, 0, SW, SH / 2, { 0,0,0,200 }, { 0,0,0,0 });
    DrawRectangleGradientV(0, SH / 2, SW, SH / 2, { 0,0,0,0 }, { 0,0,0,220 });
    DrawRectangleGradientH(0, 0, SW / 3, SH, { 0,0,0,160 }, { 0,0,0,0 });
    DrawRectangleGradientH(2 * SW / 3, 0, SW / 3, SH, { 0,0,0,0 }, { 0,0,0,160 });

    // Flicker effect
    if (rand() % 90 == 0) DrawRectangle(0, 0, SW, SH, { 255,255,255,(unsigned char)(rand() % 22) });

    // Subtitle line
    const char* sub = "A  F I R S T - P E R S O N  S U R V I V A L  E X P E R I E N C E";
    DrawTextCentered(sub, 285, 18, { 120,100,80,180 });

    // GIANT TITLE — letter by letter vertical offset
    const char* title = "DON'T BLINK";
    int titleFontSize = 148;
    int totalW = MeasureText(title, titleFontSize);
    int startX = SW / 2 - totalW / 2;

    // Draw each char with slight vertical wave
    int cx2 = startX;
    for (int i = 0; title[i]; i++) {
        char ch[2] = { title[i], 0 };
        int  cw = MeasureText(ch, titleFontSize);
        float wave = sinf(t * 1.8f + i * 0.55f) * 5.f;
        unsigned char redness = (unsigned char)(180 + sinf(t * 2.5f + i) * .5f * 75);
        // Shadow
        DrawText(ch, cx2 + 4, (int)(240 + wave) + 4, titleFontSize, { 0,0,0,180 });
        // Main
        DrawText(ch, cx2, (int)(240 + wave), titleFontSize,
            (title[i] == ' ') ? BLANK : Color{ redness, 15, 15, 255 });
        cx2 += cw;
    }

    // Red flickering underline
    float lineAlpha = 160.f + sinf(t * 4.f) * 95.f;
    DrawRectangle(SW / 2 - 420, 395, 840, 3, { 220, 20, 20, (unsigned char)lineAlpha });

    // Tagline
    DrawTextCentered("You can only move when your eyes are closed.", 430, 24, { 200,185,165,210 });
    DrawTextCentered("They only move when they're open.", 462, 24, { 200,185,165,210 });

    // Menu buttons
    struct Btn { const char* label; int y; Color base; Color hover; };
    Btn btns[] = {
        {"[ ENTER ]   START GAME",    570, {220,30,30,230},  {255,80,80,255}},
        {"[ H ]       HOW TO PLAY",   630, {160,145,130,210},{210,195,175,255}},
        {"[ ESC ]     QUIT",           690, {100,90,80,180},  {150,140,130,230}},
    };
    for (auto& b : btns) {
        bool hovering = false;
        float pulse = sinf(t * 2.8f) * 0.5f + 0.5f;
        Color c = hovering ? b.hover
            : Color{ b.base.r, b.base.g, b.base.b,
                    (unsigned char)(b.base.a * (0.8f + 0.2f * pulse)) };
        DrawTextCentered(b.label, b.y, 30, c);
    }

    // Version / footer
    DrawText("v1.0  —  Infinite Procedural Building", 20, SH - 28, 16, { 60,55,50,160 });

    // Best time if exists
    *startPressed = IsKeyPressed(KEY_ENTER);
    *howToPressed = IsKeyPressed(KEY_H);
}

// ═══════════════════════════════════════════════════════════════
//  HOW TO PLAY SCREEN
// ═══════════════════════════════════════════════════════════════
static void DrawHowToPlay(float t, bool* backPressed) {
    ClearBackground({ 8, 6, 5, 255 });

    // Subtle scanlines
    for (int y = 0; y < SH; y += 6)
        DrawRectangle(0, y, SW, 1, { 0,0,0,40 });

    DrawRectangleGradientV(0, 0, SW, 120, { 0,0,0,200 }, { 0,0,0,0 });
    DrawRectangleGradientV(0, SH - 120, SW, 120, { 0,0,0,0 }, { 0,0,0,200 });

    // Title
    DrawTextCentered("HOW  TO  PLAY", 40, 58, { 220,30,30,255 });
    DrawRectangle(SW / 2 - 360, 105, 720, 2, { 180,30,30,180 });

    // ── COLUMNS ──────────────────────────────────────────────
    int col1 = 120, col2 = SW / 2 + 60, rowH = 50, rowStart = 140;

    // Left column header
    DrawTextShadow("CONTROLS", col1, rowStart, 26, { 220,200,170,255 });
    DrawRectangle(col1, rowStart + 32, 340, 1, { 100,90,80,180 });

    struct Row { const char* key; const char* desc; };
    Row controls[] = {
        {"SPACE / LMB",   "Blink  (move while closed)"},
        {"W A S D",        "Move  (only while blinking)"},
        {"MOUSE",          "Look around"},
        {"SHIFT",          "Sprint  (loud — wakes sentinels)"},
        {"P / ESC",        "Pause game"},
        {"R",              "Restart  (game over screen)"},
    };
    for (int i = 0; i < 6; i++) {
        int y = rowStart + 56 + i * rowH;
        DrawTextShadow(controls[i].key, col1, y, 20, { 255,200,80,230 });
        DrawTextShadow(controls[i].desc, col1 + 170, y, 20, { 200,185,165,220 });
    }

    // Right column header
    DrawTextShadow("THE RULES", col2, rowStart, 26, { 220,200,170,255 });
    DrawRectangle(col2, rowStart + 32, 340, 1, { 100,90,80,180 });

    Row rules[] = {
        {"Blink to move",    "Eyes closed = you walk freely"},
        {"Eyes open",        "YOU freeze. THEY move."},
        {"Blink meter",      "Fills up — forces a blink"},
        {"Stamina bar",      "Sprinting drains it fast"},
        {"Survive",          "No objectives. Just last."},
        {"No escape",        "They never stop."},
    };
    for (int i = 0; i < 6; i++) {
        int y = rowStart + 56 + i * rowH;
        DrawTextShadow(rules[i].key, col2, y, 20, { 255,200,80,230 });
        DrawTextShadow(rules[i].desc, col2 + 170, y, 20, { 200,185,165,220 });
    }

    // ── ENEMY SECTION ────────────────────────────────────────
    int ey = rowStart + 370;
    DrawRectangle(col1, ey - 12, SW - col1 * 2, 1, { 70,65,60,160 });
    DrawTextShadow("THE ENEMIES", col1, ey, 26, { 220,200,170,255 });
    DrawRectangle(col1, ey + 32, SW - col1 * 2, 1, { 70,65,60,160 });

    // Sentinel card
    int cx1 = col1, cx2c = col1 + 50, cw = SW / 2 - col1 - 60;
    DrawRectangle(cx1, ey + 48, cw, 145, { 30,8,8,220 });
    DrawRectangle(cx1, ey + 48, cw, 145, { 0,0,0,0 }); // border
    DrawRectangleLines(cx1, ey + 48, cw, 145, { 120,20,20,180 });
    DrawCircle(cx1 + 42, ey + 48 + 72, 28, { 80,10,10,255 });
    DrawCircle(cx1 + 42, ey + 48 + 72, 28, { 0,0,0,0 });
    DrawCircleLines(cx1 + 42, ey + 48 + 72, 28, { 180,20,20,200 });
    // Eyes
    DrawCircle(cx1 + 34, ey + 48 + 68, 6, { 200,0,0,255 });
    DrawCircle(cx1 + 50, ey + 48 + 68, 6, { 200,0,0,255 });
    DrawTextShadow("SENTINEL", cx1 + 80, ey + 58, 22, { 220,30,30,255 });
    DrawTextShadow("Stands perfectly still in the dark.", cx2c + 40, ey + 86, 17, { 190,170,155,220 });
    DrawTextShadow("Wakes permanently if you step within", cx2c + 40, ey + 108, 17, { 190,170,155,220 });
    DrawTextShadow("range OR sprint nearby. Never retreats.", cx2c + 40, ey + 128, 17, { 190,170,155,220 });
    DrawTextShadow("Faint red circle = its wake range.", cx2c + 40, ey + 150, 17, { 160,60,60,200 });

    // Patroller card
    int px1 = SW / 2 + 20;
    DrawRectangle(px1, ey + 48, cw, 145, { 5,20,30,220 });
    DrawRectangleLines(px1, ey + 48, cw, 145, { 20,100,130,180 });
    DrawCircle(px1 + 42, ey + 48 + 72, 28, { 5,50,70,255 });
    DrawCircleLines(px1 + 42, ey + 48 + 72, 28, { 20,160,190,200 });
    // Eyes
    DrawCircle(px1 + 34, ey + 48 + 68, 6, { 0,210,255,255 });
    DrawCircle(px1 + 50, ey + 48 + 68, 6, { 0,210,255,255 });
    DrawTextShadow("PATROLLER", px1 + 80, ey + 58, 22, { 0,200,240,255 });
    DrawTextShadow("Patrols a route through the building.", px1 + 120, ey + 86, 17, { 190,170,155,220 });
    DrawTextShadow("Detects you by sight or sound.", px1 + 120, ey + 108, 17, { 190,170,155,220 });
    DrawTextShadow("Chases aggressively — loses you", px1 + 120, ey + 128, 17, { 190,170,155,220 });
    DrawTextShadow("after ~7 sec then resumes patrol.", px1 + 120, ey + 150, 17, { 160,160,60,200 });

    // ── MINIMAP LEGEND ───────────────────────────────────────
    int ly = ey + 215;
    DrawRectangle(col1, ly - 8, SW - col1 * 2, 1, { 60,55,50,140 });
    DrawTextShadow("MINIMAP  (bottom-right corner)", col1, ly, 22, { 180,165,145,240 });
    struct MLeg { Color c; const char* desc; bool circle; };
    MLeg mleg[] = {
        {{80,255,80,255},  "You (arrow shows direction)", true},
        {{0,200,220,220},  "Patroller  —  dot grows when chasing", true},
        {{220,30,30,200},  "Sentinel   —  dot grows when activated", true},
        {{165,155,145,225},"Wall segment",  false},
    };
    for (int i = 0; i < 4; i++) {
        int lx = col1 + i * 460;
        if (i == 0) lx = col1;
        if (i == 1) lx = col1 + 380;
        if (i == 2) lx = col1 + 810;
        if (i == 3) lx = col1 + 1240;
        if (mleg[i].circle) DrawCircle(lx + 10, ly + 36, 7, mleg[i].c);
        else DrawRectangle(lx + 4, ly + 31, 14, 3, mleg[i].c);
        DrawTextShadow(mleg[i].desc, lx + 24, ly + 27, 16, { 185,170,150,220 });
    }

    // ── TIP BOX ──────────────────────────────────────────────
    int ty = ly + 68;
    DrawRectangle(col1, ty, SW - col1 * 2, 58, { 25,22,18,200 });
    DrawRectangleLines(col1, ty, SW - col1 * 2, 58, { 90,80,65,160 });
    float pulse = sinf(t * 2.5f) * .5f + .5f;
    Color tipCol = { (unsigned char)(200 + pulse * 55), (unsigned char)(180 + pulse * 40), 100, 240 };
    DrawTextCentered("TIP:  Move fast and think slow.  Blink deliberately — every wasted", ty + 10, 18, tipCol);
    DrawTextCentered("movement is a second enemies spend closing the gap.", ty + 34, 18, tipCol);

    // Back button
    unsigned char ba = pulseAlpha(t, 2.2f, 220);
    DrawTextCentered("[ ENTER ]   START GAME         [ BACKSPACE ]   BACK TO TITLE", SH - 46, 22,
        { 200, 185, 155, ba });

    *backPressed = IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_ESCAPE);
    if (IsKeyPressed(KEY_ENTER)) *backPressed = false, (*backPressed = false);
}

// ═══════════════════════════════════════════════════════════════
//  MINIMAP
// ═══════════════════════════════════════════════════════════════
static void DrawMinimap(const RoomMap& rooms, const std::vector<Enemy>& enemies,
    const Player& player, int pgx, int pgz) {
    const int MP = 190, CP = 14, MX = SW - MP - 14, MY = SH - MP - 14;
    DrawRectangle(MX, MY, MP, MP, { 0,0,0,165 });
    DrawRectangle(MX, MY, MP, 16, { 20,18,15,200 });
    DrawText("MAP", MX + 4, MY + 2, 12, { 80,75,65,180 });

    int VR = MP / (2 * CP);
    Color wc = { 165,155,145,230 };
    int dh = CP / 4;

    for (int dz = -VR; dz <= VR; dz++) for (int dx = -VR; dx <= VR; dx++) {
        auto it = rooms.find(roomKey(pgx + dx, pgz + dz));
        if (it == rooms.end()) continue;
        const Room& r = it->second;
        int px2 = MX + MP / 2 + dx * CP, pz2 = MY + MP / 2 + dz * CP;
        DrawRectangle(px2 - CP / 2 + 1, pz2 - CP / 2 + 1, CP - 2, CP - 2, { 32,28,24,210 });
        if (!r.doors[0]) DrawLine(px2 - CP / 2, pz2 - CP / 2, px2 + CP / 2, pz2 - CP / 2, wc);
        else { DrawLine(px2 - CP / 2, pz2 - CP / 2, px2 - dh, pz2 - CP / 2, wc); DrawLine(px2 + dh, pz2 - CP / 2, px2 + CP / 2, pz2 - CP / 2, wc); }
        if (!r.doors[2]) DrawLine(px2 - CP / 2, pz2 + CP / 2, px2 + CP / 2, pz2 + CP / 2, wc);
        else { DrawLine(px2 - CP / 2, pz2 + CP / 2, px2 - dh, pz2 + CP / 2, wc); DrawLine(px2 + dh, pz2 + CP / 2, px2 + CP / 2, pz2 + CP / 2, wc); }
        if (!r.doors[1]) DrawLine(px2 + CP / 2, pz2 - CP / 2, px2 + CP / 2, pz2 + CP / 2, wc);
        else { DrawLine(px2 + CP / 2, pz2 - CP / 2, px2 + CP / 2, pz2 - dh, wc); DrawLine(px2 + CP / 2, pz2 + dh, px2 + CP / 2, pz2 + CP / 2, wc); }
        if (!r.doors[3]) DrawLine(px2 - CP / 2, pz2 - CP / 2, px2 - CP / 2, pz2 + CP / 2, wc);
        else { DrawLine(px2 - CP / 2, pz2 - CP / 2, px2 - CP / 2, pz2 - dh, wc); DrawLine(px2 - CP / 2, pz2 + dh, px2 - CP / 2, pz2 + CP / 2, wc); }
    }

    for (auto& e : enemies) {
        int epx = MX + MP / 2 + (int)((e.pos.x - pgx * ROOM_SIZE) / ROOM_SIZE * CP);
        int epz = MY + MP / 2 + (int)((e.pos.z - pgz * ROOM_SIZE) / ROOM_SIZE * CP);
        if (epx<MX + 3 || epx>MX + MP - 3 || epz<MY + 3 || epz>MY + MP - 3) continue;
        bool act = (e.state == E_CHASE) || (e.type == SENTINEL && e.activated);
        Color ec = (e.type == SENTINEL) ? Color{ 220,30,30,200 } : Color{ 0,200,220,200 };
        if (act) ec.a = 255;
        DrawCircle(epx, epz, act ? 5 : 2, ec);
        if (act) DrawCircleLines(epx, epz, 7, { ec.r,ec.g,ec.b,100 });
    }
    DrawCircle(MX + MP / 2, MY + MP / 2, 4, { 80,255,80,255 });
    DrawLine(MX + MP / 2, MY + MP / 2,
        MX + MP / 2 + (int)(sinf(player.yaw) * 9),
        MY + MP / 2 + (int)(cosf(player.yaw) * 9), { 80,255,80,200 });
    DrawRectangleLines(MX, MY, MP, MP, { 70,65,58,180 });
}

// ═══════════════════════════════════════════════════════════════
//  PAUSE SCREEN
// ═══════════════════════════════════════════════════════════════
static void DrawPauseScreen() {
    DrawRectangle(0, 0, SW, SH, { 0,0,0,185 });
    DrawTextCentered("PAUSED", SH / 2 - 120, 72, { 220,30,30,255 });
    DrawTextCentered("[ P / ESC ]   RESUME", SH / 2 - 10, 28, { 200,185,165,230 });
    DrawTextCentered("[ R ]         RESTART", SH / 2 + 36, 28, { 160,145,125,220 });
    DrawTextCentered("[ M ]         MAIN MENU", SH / 2 + 82, 28, { 130,115,95,210 });
}

// ═══════════════════════════════════════════════════════════════
//  MAIN
// ═══════════════════════════════════════════════════════════════
int main() {
	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    srand((unsigned)time(nullptr));
    InitWindow(SW, SH, "DON'T BLINK");
    SetTargetFPS(60);

    Texture2D flashTex = buildFlashTex();

    Camera cam = { 0 };
    cam.up = { 0,1,0 }; cam.fovy = 80.f; cam.projection = CAMERA_PERSPECTIVE;

    GameState state = TITLE;
    Player    player;
    std::vector<Enemy> enemies;
    RoomMap rooms;

    float globalT = 0.f;
    float survivalTime = 0.f;
    float bestTime = 0.f;
    float forcedBlink = BLINK_BASE;
    bool  isBlinking = false;
    float blinkTimer = 0.f;
    float flashAlpha = 0.f;
    float breathPhase = 0.f;
    float flickerTimer = 15.f;
    float flickerAlpha = 0.f;
    float minDist = 999.f;
    float bobPhase = 0.f;
    bool  wasMoving = false;
    bool  cursorEnabled = false;
    int   pgx = 0, pgz = 0;

    // Title screen background camera (slow drift)
    Camera titleCam = { 0 };
    titleCam.position = { 0,1.7f,0 }; titleCam.target = { 1,1.7f,0 };
    titleCam.up = { 0,1,0 }; titleCam.fovy = 85.f; titleCam.projection = CAMERA_PERSPECTIVE;
    RoomMap titleRooms;
    std::vector<Enemy> titleDummy;
    ensureRooms(0, 0, titleRooms, titleDummy, 0.f);

    auto startGame = [&]() {
        player = {}; enemies.clear(); rooms.clear();
        isBlinking = false; blinkTimer = 0; forcedBlink = BLINK_BASE;
        flashAlpha = 0; survivalTime = 0; breathPhase = 0;
        flickerTimer = 15; flickerAlpha = 0; minDist = 999;
        bobPhase = 0; wasMoving = false; pgx = pgz = 0;
        cursorEnabled = false; state = PLAYING;
        DisableCursor();
        ensureRooms(0, 0, rooms, enemies, 0.f);
        };

    auto fullReset = [&]() {
        player = {}; enemies.clear(); rooms.clear();
        isBlinking = false; blinkTimer = 0; forcedBlink = BLINK_BASE;
        flashAlpha = 0; survivalTime = 0; breathPhase = 0;
        flickerTimer = 15; flickerAlpha = 0; minDist = 999;
        bobPhase = 0; wasMoving = false; pgx = pgz = 0; state = TITLE;
        if (!cursorEnabled) { EnableCursor(); cursorEnabled = true; }
        ensureRooms(0, 0, titleRooms, titleDummy, 0.f);
        };

    // ════════════════════════════════════════════════════════
    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        globalT += dt;

        // ── TITLE ───────────────────────────────────────────
        if (state == TITLE) {
            if (!cursorEnabled) { EnableCursor(); cursorEnabled = true; }

            // Drift title camera slowly
            titleCam.position = { sinf(globalT * .07f) * 4.f, 1.7f, cosf(globalT * .05f) * 4.f };
            float tx = sinf(globalT * .09f) * 2.f, tz = cosf(globalT * .08f) * 2.f;
            titleCam.target = { titleCam.position.x + tx, 1.7f, titleCam.position.z + tz };

            BeginDrawing();
            ClearBackground({ 5,4,3,255 });

            // Render 3D building in background (dim)
            BeginMode3D(titleCam);
            DrawPlane({ 0,0,0 }, { 120,120 }, { 12,11,10,255 });
            DrawPlane({ 0,WALL_H,0 }, { 120,120 }, { 8,7,6,255 });
            for (auto& [key, room] : titleRooms)
                for (auto& w : room.walls)
                    DrawCube(w.pos, w.size.x, w.size.y, w.size.z,
                        { (unsigned char)(w.col.r / 3),(unsigned char)(w.col.g / 3),(unsigned char)(w.col.b / 3),255 });
            EndMode3D();

            // Heavy dark overlay on the 3D
            DrawRectangle(0, 0, SW, SH, { 0,0,0,175 });

            bool doStart = false, doHow = false;
            DrawTitleScreen(globalT, &doStart, &doHow);

            if (doStart)    startGame();
            if (doHow)      state = HOW_TO_PLAY;
            if (IsKeyPressed(KEY_ESCAPE)) break;

            EndDrawing();
            continue;
        }

        // ── HOW TO PLAY ─────────────────────────────────────
        if (state == HOW_TO_PLAY) {
            BeginDrawing();
            bool goBack = false;
            DrawHowToPlay(globalT, &goBack);
            if (goBack) state = TITLE;
            if (IsKeyPressed(KEY_ENTER)) startGame();
            EndDrawing();
            continue;
        }

        // ── PAUSE ───────────────────────────────────────────
        if (state == PAUSED) {
            if (!cursorEnabled) { EnableCursor(); cursorEnabled = true; }
            BeginDrawing();
            ClearBackground({ 3,3,5,255 });
            DrawPauseScreen();
            if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)) {
                state = PLAYING; DisableCursor(); cursorEnabled = false;
            }
            if (IsKeyPressed(KEY_R)) { startGame(); }
            if (IsKeyPressed(KEY_M)) { fullReset(); }
            EndDrawing();
            continue;
        }

        // ── GAME OVER ───────────────────────────────────────
        if (state == GAME_OVER) {
            if (!cursorEnabled) { EnableCursor(); cursorEnabled = true; }
            if (IsKeyPressed(KEY_R))                         startGame();
            if (IsKeyPressed(KEY_M) || IsKeyPressed(KEY_ESCAPE)) fullReset();
        }

        // ── PAUSE INPUT (during PLAYING) ─────────────────────
        if (state == PLAYING && (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)))
            state = PAUSED;

        // ════════════════════════════════════════════════════
        //  PLAYING UPDATE
        // ════════════════════════════════════════════════════
        if (state == PLAYING)
        {
            survivalTime += dt; breathPhase += dt;
            pgx = (int)roundf(player.pos.x / ROOM_SIZE);
            pgz = (int)roundf(player.pos.z / ROOM_SIZE);
            ensureRooms(pgx, pgz, rooms, enemies, survivalTime);

            // Mouse look
            Vector2 md = GetMouseDelta();
            player.yaw -= md.x * MOUSE_SENS;
            player.pitch = Clamp(player.pitch - md.y * MOUSE_SENS, -.42f, .42f);

            Vector3 lookDir = {
                sinf(player.yaw) * cosf(player.pitch),
                sinf(player.pitch),
                cosf(player.yaw) * cosf(player.pitch)
            };

            float dangerF = Clamp(1.f - minDist / 8.f, 0.f, 1.f);
            float bobY = wasMoving ? sinf(bobPhase) * 0.045f : 0.f;
            cam.fovy = 80.f + sinf(breathPhase * (1.5f + dangerF * 2.f)) * (0.5f + dangerF * 2.5f);
            cam.position = { player.pos.x, player.pos.y + bobY, player.pos.z };
            cam.target = Vector3Add(cam.position, lookDir);

            // ── BLINK ────────────────────────────────────────
            if ((IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) && !isBlinking) {
                isBlinking = true; blinkTimer = BLINK_DUR; forcedBlink = BLINK_BASE; flashAlpha = 1.f;
            }
            forcedBlink -= dt;
            if (forcedBlink <= 0.f) {
                isBlinking = true; blinkTimer = BLINK_DUR + .1f; forcedBlink = BLINK_BASE; flashAlpha = 1.f;
            }
            if (isBlinking) { blinkTimer -= dt; if (blinkTimer <= 0.f) { isBlinking = false; blinkTimer = 0.f; } }
            flashAlpha -= dt * 9.f; if (flashAlpha < 0.f) flashAlpha = 0.f;

            // ── MOVEMENT ─────────────────────────────────────
            bool moving = false;
            if (isBlinking) {
                Vector3 fwd = { sinf(player.yaw),0,cosf(player.yaw) };
                Vector3 rgt = { cosf(player.yaw),0,-sinf(player.yaw) };
                Vector3 mv = { 0,0,0 };
                if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    mv = Vector3Add(mv, fwd);
                if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  mv = Vector3Subtract(mv, fwd);
                if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) mv = Vector3Subtract(mv, rgt);
                if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  mv = Vector3Add(mv, rgt);
                player.sprinting = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) && player.stamina > 0.f;
                float spd = player.sprinting ? 8.0f : player.speed;
                if (player.sprinting) player.stamina = std::max(0.f, player.stamina - 40.f * dt);
                else                  player.stamina = std::min(100.f, player.stamina + 20.f * dt);
                if (Vector3Length(mv) > .001f) {
                    moving = true;
                    bobPhase += dt * (player.sprinting ? 14.f : 8.f);
                    Vector3 np = Vector3Add(player.pos, Vector3Scale(Vector3Normalize(mv), spd * dt));
                    np.y = 1.7f;
                    player.pos = slideMove(player.pos, np, player.radius, rooms);
                }
            }
            else {
                player.stamina = std::min(100.f, player.stamina + 10.f * dt);
            }
            wasMoving = moving;

            float noiseR = moving ? (player.sprinting ? 15.f : 7.f) : 2.f;

            // ── ENEMY AI ─────────────────────────────────────
            minDist = 999.f;
            for (auto& e : enemies) {
                float pd = Vector3Distance({ player.pos.x,0,player.pos.z }, { e.pos.x,0,e.pos.z });
                if (pd < minDist) minDist = pd;
                e.alertFlash -= dt; if (e.alertFlash < 0.f)e.alertFlash = 0.f;

                if (!isBlinking) {
                    if (e.type == SENTINEL) {
                        if (!e.activated && (pd < e.detectRange || pd < noiseR)) {
                            e.activated = true; e.alertFlash = 1.0f;
                        }
                        if (e.activated) {
                            Vector3 dir = { player.pos.x - e.pos.x,0,player.pos.z - e.pos.z };
                            if (Vector3Length(dir) > .01f) {
                                Vector3 np = Vector3Add(e.pos, Vector3Scale(Vector3Normalize(dir), e.speed * dt));
                                np.y = 0.9f;
                                e.pos = slideMove(e.pos, np, e.radius, rooms);
                            }
                        }
                    }
                    else {
                        bool detected = (pd < e.detectRange || pd < noiseR);
                        if (detected) {
                            e.state = E_CHASE; e.lostTimer = 0.f;
                            if (e.alertFlash <= 0.f)e.alertFlash = 0.8f;
                        }
                        else if (e.state == E_CHASE) {
                            e.lostTimer += dt;
                            if (e.lostTimer > 7.f) { e.state = E_PATROL; e.lostTimer = 0.f; }
                        }
                        if (e.state == E_CHASE) {
                            Vector3 dir = { player.pos.x - e.pos.x,0,player.pos.z - e.pos.z };
                            if (Vector3Length(dir) > .01f) {
                                Vector3 np = Vector3Add(e.pos, Vector3Scale(Vector3Normalize(dir), e.speed * 1.35f * dt));
                                np.y = 0.9f; e.pos = slideMove(e.pos, np, e.radius, rooms);
                            }
                        }
                        else if (!e.waypoints.empty()) {
                            Vector3& wp = e.waypoints[e.wpIdx];
                            Vector3  dir = { wp.x - e.pos.x,0,wp.z - e.pos.z };
                            float dl = Vector3Length(dir);
                            if (dl < .8f) { e.wpIdx = (e.wpIdx + 1) % (int)e.waypoints.size(); e.waitTime = 1.f + (float)(rand() % 3); }
                            if (e.waitTime > 0.f) e.waitTime -= dt;
                            else if (dl > .01f) {
                                Vector3 np = Vector3Add(e.pos, Vector3Scale(Vector3Normalize(dir), e.speed * .7f * dt));
                                np.y = 0.9f; e.pos = slideMove(e.pos, np, e.radius, rooms);
                            }
                        }
                    }
                }
                if (pd < player.radius + e.radius) {
                    if (survivalTime > bestTime) bestTime = survivalTime;
                    state = GAME_OVER;
                }
            }

            // Flicker
            flickerTimer -= dt;
            if (flickerTimer <= 0.f) {
                if (rand() % 3 == 0) flickerAlpha = .60f + .3f * (rand() % 100 * .01f);
                flickerTimer = 8.f + (float)(rand() % 30) * .5f;
            }
            flickerAlpha -= dt * 13.f; if (flickerAlpha < 0.f)flickerAlpha = 0.f;
        }

        // ════════════════════════════════════════════════════
        //  DRAW
        // ════════════════════════════════════════════════════
        BeginDrawing();
        ClearBackground({ 3,3,5,255 });

        if (state == PLAYING || state == GAME_OVER)
        {
            BeginMode3D(cam);

            // Floor + ceiling
            DrawPlane({ roundf(player.pos.x * .1f) * 10.f,0,roundf(player.pos.z * .1f) * 10.f }, { 220,220 }, { 14,13,11,255 });
            DrawPlane({ roundf(player.pos.x * .1f) * 10.f,WALL_H,roundf(player.pos.z * .1f) * 10.f }, { 220,220 }, { 9,8,7,255 });

            // Floor grid
            for (float g = -32.f; g <= 32.f; g += 2.f) {
                float lx = roundf(player.pos.x * .5f) * 2.f + g;
                float lz = roundf(player.pos.z * .5f) * 2.f + g;
                DrawLine3D({ lx,.01f,player.pos.z - 44 }, { lx,.01f,player.pos.z + 44 }, { 20,18,16,255 });
                DrawLine3D({ player.pos.x - 44,.01f,lz }, { player.pos.x + 44,.01f,lz }, { 20,18,16,255 });
            }

            // Room walls + lights
            for (auto& [key, room] : rooms) {
                float rcx = room.gx * ROOM_SIZE, rcz = room.gz * ROOM_SIZE;
                if (Vector3Distance({ player.pos.x,0,player.pos.z }, { rcx,0,rcz }) > 80.f) continue;
                for (auto& w : room.walls)
                    DrawCube(w.pos, w.size.x, w.size.y, w.size.z, w.col);
                RNG lr(pcg((uint32_t)(room.gx + 99) * 333u ^ (uint32_t)(room.gz + 99) * 777u));
                bool flk = lr.rPct(40);
                float fk = flk ? Clamp(.35f + sinf(globalT * (5.f + lr.rF() * 4.f)) * .65f, 0.f, 1.f) : 1.f;
                Color glw = { (unsigned char)(135 * fk),(unsigned char)(115 * fk),(unsigned char)(52 * fk),255 };
                DrawSphere({ rcx,WALL_H - .12f,rcz }, .18f, glw);
                DrawCylinder({ rcx,.1f,rcz }, 0.f, 1.1f, WALL_H - .1f, 8,
                    { (unsigned char)(22 * fk),(unsigned char)(18 * fk),(unsigned char)(6 * fk),(unsigned char)(44 * fk) });
            }

            // Enemies
            for (auto& e : enemies) {
                float pd = Vector3Distance({ player.pos.x,0,player.pos.z }, { e.pos.x,0,e.pos.z });
                if (pd > 65.f) continue;
                bool frozen = isBlinking;
                bool chasing = (e.state == E_CHASE) || (e.type == SENTINEL && e.activated);

                Color body, wire, eyeC;
                if (e.type == SENTINEL) {
                    body = frozen ? Color{ 62,5,5,255 } : Color{ 200,15,15,255 };
                    wire = frozen ? Color{ 90,20,20,180 } : Color{ 255,55,55,200 };
                    eyeC = frozen ? Color{ 30,0,0,255 } :
                        (e.activated ? Color{ 255,175,0,255 } : Color{ 80,0,0,200 });
                }
                else {
                    body = frozen ? Color{ 5,40,55,255 } : (chasing ? Color{ 190,20,20,255 } : Color{ 14,92,115,255 });
                    wire = frozen ? Color{ 10,60,80,180 } : (chasing ? Color{ 255,65,65,200 } : Color{ 28,170,200,200 });
                    eyeC = frozen ? Color{ 0,20,30,255 } : (chasing ? Color{ 255,195,0,255 } : Color{ 0,210,255,255 });
                }

                // Alert flash (lerp toward white when alertFlash > 0)
                if (e.alertFlash > 0.f) {
                    float af = e.alertFlash;
                    body = { (unsigned char)(body.r + (255 - body.r) * af * .6f),
                            (unsigned char)(body.g + (255 - body.g) * af * .3f),
                            (unsigned char)(body.b + (255 - body.b) * af * .1f), 255 };
                }

                DrawCube(e.pos, .88f, 1.8f, .88f, body);
                DrawCubeWires(e.pos, .88f, 1.8f, .88f, wire);
                Vector3 hPos = { e.pos.x, e.pos.y + 1.1f, e.pos.z };
                DrawSphere(hPos, .40f, body);
                DrawSphereWires(hPos, .40f, 5, 5, wire);

                Vector3 toP = Vector3Normalize({ player.pos.x - e.pos.x,0,player.pos.z - e.pos.z });
                Vector3 side = { toP.z,0,-toP.x };
                Vector3 eL = Vector3Add(hPos, Vector3Add(Vector3Scale(toP, .38f), Vector3Scale(side, .15f)));
                Vector3 eR = Vector3Add(hPos, Vector3Subtract(Vector3Scale(toP, .38f), Vector3Scale(side, .15f)));
                DrawSphere(eL, .10f, eyeC); DrawSphere(eR, .10f, eyeC);
                DrawSphere(Vector3Add(eL, Vector3Scale(toP, .06f)), .05f, { 0,0,0,255 });
                DrawSphere(Vector3Add(eR, Vector3Scale(toP, .06f)), .05f, { 0,0,0,255 });

                if (e.type == SENTINEL && !e.activated)
                    DrawCircle3D({ e.pos.x,.01f,e.pos.z }, e.detectRange, { 1,0,0 }, 90, { 50,0,0,18 });
                DrawCircle3D({ e.pos.x,.01f,e.pos.z }, .55f, { 1,0,0 }, 90,
                    chasing ? Color{ 120,0,0,100 } : Color{ 0,55,75,60 });
            }

            EndMode3D();

            // ── 2D OVERLAYS ──────────────────────────────────

            // Flashlight
            DrawTexturePro(flashTex, { 0,0,(float)960,(float)500 }, { 0,0,(float)SW,(float)SH }, { 0,0 }, 0, WHITE);

            // Flicker
            if (flickerAlpha > 0.f)
                DrawRectangle(0, 0, SW, SH, { 0,0,0,(unsigned char)(flickerAlpha * 230) });

            // Danger vignette
            if (!enemies.empty() && minDist < 6.f) {
                float threat = 1.f - minDist / 6.f;
                float pulse2 = (minDist < 2.5f) ? (sinf(globalT * 8.f) * .4f + .6f) : 1.f;
                auto  va = (unsigned char)(threat * pulse2 * 195.f);
                DrawRectangleGradientH(0, 0, 320, SH, { 180,0,0,va }, { 180,0,0,0 });
                DrawRectangleGradientH(SW - 320, 0, 320, SH, { 180,0,0,0 }, { 180,0,0,va });
                DrawRectangleGradientV(0, 0, SW, 260, { 180,0,0,va }, { 180,0,0,0 });
                DrawRectangleGradientV(0, SH - 260, SW, 260, { 180,0,0,0 }, { 180,0,0,va });
            }

            // Sprint tunnel-vision
            if (player.sprinting && isBlinking) {
                DrawRectangleGradientH(0, 0, 200, SH, { 0,0,0,90 }, { 0,0,0,0 });
                DrawRectangleGradientH(SW - 200, 0, 200, SH, { 0,0,0,0 }, { 0,0,0,90 });
                DrawRectangleGradientV(0, 0, SW, 160, { 0,0,0,90 }, { 0,0,0,0 });
                DrawRectangleGradientV(0, SH - 160, SW, 160, { 0,0,0,0 }, { 0,0,0,90 });
            }

            // Blink flash
            if (flashAlpha > 0.f)
                DrawRectangle(0, 0, SW, SH, { 255,255,255,(unsigned char)(flashAlpha * 255) });

            // Crosshair
            {
                int cx = SW / 2, cy = SH / 2; Color ch = { 255,255,255,120 };
                DrawLine(cx - 13, cy, cx + 13, cy, ch); DrawLine(cx, cy - 13, cx, cy + 13, ch);
                DrawCircleLines(cx, cy, 4, { 255,255,255,60 });
            }

            // ── HUD ──────────────────────────────────────────

            // Blink meter
            {
                float bw = 320, bh = 22, bx = SW / 2.f - 160, by = (float)SH - 68;
                float frac = 1.f - (forcedBlink / BLINK_BASE);
                Color bc = frac > .8f ? RED : frac > .55f ? ORANGE : Color{ 255,200,0,255 };
                DrawRectangle((int)bx, (int)by, (int)bw, (int)bh, { 16,16,16,230 });
                DrawRectangle((int)bx, (int)by, (int)(bw * frac), (int)bh, bc);
                DrawRectangleLines((int)bx, (int)by, (int)bw, (int)bh, { 180,180,180,180 });
                DrawTextShadow("BLINK METER", (int)bx, (int)(by - 22), 15, { 140,130,120,200 });
            }

            // Stamina bar
            {
                float sw2 = 200, sh2 = 12, sx2 = SW / 2.f - 100, sy2 = (float)SH - 38;
                Color sc = player.stamina < 20.f ? RED : Color{ 0,195,175,220 };
                DrawRectangle((int)sx2, (int)sy2, (int)sw2, (int)sh2, { 16,16,16,210 });
                DrawRectangle((int)sx2, (int)sy2, (int)(sw2 * player.stamina / 100.f), (int)sh2, sc);
                DrawRectangleLines((int)sx2, (int)sy2, (int)sw2, (int)sh2, { 90,90,90,160 });
                DrawTextShadow("STAMINA", (int)sx2, (int)(sy2 - 16), 13, { 95,90,82,180 });
            }

            // Survival timer
            {
                int t2 = (int)survivalTime;
                DrawTextShadow(TextFormat("SURVIVED  %02d:%02d", t2 / 60, t2 % 60), 20, 20, 30, WHITE);
                DrawTextShadow(TextFormat("BEST  %02d:%02d", (int)bestTime / 60, (int)bestTime % 60), 20, 56, 20, { 110,110,110,220 });
                DrawTextShadow(TextFormat("THREATS: %d", (int)enemies.size()), SW - 220, 20, 22, { 200,50,50,220 });
            }

            // Blink status
            if (state == PLAYING) {
                const char* msg = isBlinking ? "-- BLINKING  |  MOVE NOW --" : "THEY  ARE  MOVING";
                Color mc = isBlinking ? Color{ 70,215,70,240 } : Color{ 200,28,28,230 };
                DrawTextCentered(msg, 15, 24, mc);
            }

            // Minimap
            DrawMinimap(rooms, enemies, player, pgx, pgz);

            // Controls hint (bottom)
            DrawTextCentered("SPACE=BLINK  WASD=MOVE(blinking)  SHIFT=SPRINT  P=PAUSE",
                SH - 22, 14, { 55,52,48,155 });

            // ── GAME OVER OVERLAY ─────────────────────────────
            if (state == GAME_OVER) {
                DrawRectangle(0, 0, SW, SH, { 0,0,0,210 });

                // Animated title
                float scale = 1.f + sinf(globalT * 1.4f) * .012f;
                (void)scale;
                DrawTextCentered("YOU  BLINKED.", SH / 2 - 130, 72, { 220,20,20,255 });

                int ts = (int)survivalTime;
                DrawTextCentered(TextFormat("SURVIVED   %02d : %02d", ts / 60, ts % 60), SH / 2 - 20, 38, WHITE);

                int bs = (int)bestTime;
                Color bc2 = (survivalTime >= bestTime && survivalTime > 0.f) ?
                    Color{ 255,200,0,255 } : Color{ 120,115,110,230 };
                const char* btxt = (survivalTime >= bestTime && survivalTime > 0.f)
                    ? TextFormat("NEW BEST!   %02d : %02d", bs / 60, bs % 60)
                    : TextFormat("BEST   %02d : %02d", bs / 60, bs % 60);
                DrawTextCentered(btxt, SH / 2 + 28, 26, bc2);

                unsigned char pa = pulseAlpha(globalT, 2.4f, 215);
                DrawTextCentered("[ R ]   TRY AGAIN         [ M ]   MAIN MENU", SH / 2 + 90, 26, { 185,170,145,pa });
            }
        }

        EndDrawing();
    }

    UnloadTexture(flashTex);
    CloseWindow();
    return 0;
}