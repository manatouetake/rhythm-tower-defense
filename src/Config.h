#pragma once
#include <SDL.h>
#include <array>
#include <cstdint>

// ============================================================
//  Config.h : ゲーム全体の定数とタワー定義
// ============================================================

constexpr int   CELL    = 50;
constexpr int   COLS    = 14;
constexpr int   ROWS    = 9;
constexpr int   FIELD_W = COLS * CELL;
constexpr int   FIELD_H = ROWS * CELL;
constexpr int   HUD_H   = 44;
constexpr int   SHOP_H  = 140;
constexpr int   WIN_W   = FIELD_W;
constexpr int   WIN_H   = HUD_H + FIELD_H + SHOP_H;

constexpr int    START_MONEY      = 300;
constexpr int    START_LIVES     = 10;
constexpr float  SPAWN_INTERVAL  = 0.8f;
constexpr float  PROJECTILE_SPEED= 420.f;
constexpr float  SLOW_FACTOR     = 0.45f;
constexpr float  SLOW_DURATION   = 1.6f;
constexpr int    MAX_TOWER_LEVEL = 5;
constexpr float  SELL_RATE       = 0.6f;
constexpr double MUSIC_BPM       = 112.0;   // BGMテンポ(音と映像の同期基準)
constexpr int    BEAM_PERIOD     = 8;       // ビーム発射間隔(拍数)

constexpr float UPG_DAMAGE = 1.35f;
constexpr float UPG_RANGE  = 1.08f;
constexpr float UPG_RATE   = 0.92f;
constexpr float WAVE_HP_BASE   = 28.f;
constexpr float WAVE_HP_GROWTH = 1.17f;

struct Color { uint8_t r, g, b, a = 255; };

// ---------------- タワー ----------------
enum class TowerKind { Gun = 0, Cannon, Ice, Sniper, Beam, Mine, COUNT };

struct TowerDef {
    const char* name;
    const char* desc;
    int         cost;
    float       damage;
    float       range;        // 0なら射程円を持たない特殊タワー
    float       cooldown;
    float       splashRadius;
    bool        slows;
    bool        pierce;
    Color       color;
    bool        isBeam;       // マウス照準ビーム(8拍に1回全体同時発射)
    bool        isMine;       // 金鉱(拍に合わせてクリックで収入)
};

const std::array<TowerDef, 6>& towerDefs();

// ---------------- 敵 ----------------
enum class EnemyKind { Normal = 0, Fast, Tank, Armored, Boss };

struct EnemyDef {
    float hpMul;
    float speed;
    float radius;
    float rewardMul;
    bool  armored;
    Color color;
};

const EnemyDef& enemyDef(EnemyKind k);
