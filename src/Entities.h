#pragma once
#include <string>
#include "Config.h"

// ============================================================
//  Entities.h : ゲーム内オブジェクトのデータ構造
// ============================================================

struct Vec2 { float x = 0.f, y = 0.f; };

// ---------------- 敵 ----------------
struct Enemy {
    int       id = 0;
    EnemyKind kind = EnemyKind::Normal;
    Vec2      pos;
    float     hp = 0.f, maxHp = 0.f;
    float     speed = 0.f;
    float     radius = 10.f;
    int       reward = 0;
    bool      armored = false;
    float     slowTimer = 0.f;
    int       wpIndex = 1;
    bool      dead = false;
    Color     color{};
};

// ---------------- タワー ----------------
struct Tower {
    TowerKind kind = TowerKind::Gun;
    int       col = 0, row = 0;
    Vec2      pos;
    int       level = 1;
    float     cooldown = 0.f;
    float     angle = 0.f;
    int       spent = 0;
    int64_t   lastMineBeat = -100;   // 金鉱: 同じ拍の二重クリック防止
};

struct TowerStats { float damage = 0.f, range = 0.f, cooldown = 0.f; };

// ---------------- 弾 ----------------
struct Projectile {
    Vec2      pos, prev;             // prevはトレイル描画用
    int       targetId = -1;
    Vec2      lastTargetPos;
    float     damage = 0.f;
    TowerKind kind = TowerKind::Gun;
    float     splashRadius = 0.f;
    bool      slows = false;
    bool      pierce = false;
    bool      hit = false;
};

// ---------------- エフェクト ----------------
enum class PType {
    Ring,    // 拡大していく輪 (爆発・撃破)
    Spark,   // 速度を持って飛び散る火花
    Coin,    // 重力で落ちる金貨
    Text,    // 浮き上がる文字 (+15, PERFECT! など)
    BeamFx,  // ビームの残光
};

struct Particle {
    PType       type = PType::Ring;
    Vec2        pos, vel{}, pos2{};  // pos2はビームの終点
    float       t = 0.f, lifetime = 0.3f;
    float       size = 20.f;         // Ring:最大半径 / Spark,Coin:粒径 / Beam:太さ
    Color       color{};
    std::string text;
};
