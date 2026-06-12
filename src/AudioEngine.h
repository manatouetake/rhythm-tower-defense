#pragma once
#include <SDL.h>
#include <atomic>
#include <cstdint>
#include <vector>

// ============================================================
//  AudioEngine.h : リアルタイム合成によるインタラクティブ音楽
//
//  仕組み:
//  - SDLのオーディオコールバック内でステップシーケンサーを駆動し、
//    BGM(コード進行 Am-F-C-G)を波形から直接合成する
//  - 効果音は trigger() で要求されると即発音せず「次の16分音符」
//    までクオンタイズされ、さらに音高は現在のコード構成音から
//    選ばれる → 全ての効果音がBGMの拍とハーモニーに同期する
//  - setBattle() でドラム等のレイヤーを足し引きする(垂直レイヤリング)
// ============================================================

enum class Sfx {
    GunShot,     // ガンナー発射  : 高音の短いブリップ(コード構成音)
    CannonShot,  // キャノン発射  : 低い爆発音(コードのルート)
    IceShot,     // アイス発射    : 下降グリッサンド
    SniperShot,  // スナイパー発射: 鋭い高音(長め)
    Kill,        // 敵撃破        : コードの上昇アルペジオ
    Leak,        // 敵がゴール到達: 不協和な低音
    Place,       // タワー設置    : 柔らかいクリック
    Coin,        // 金鉱クリック  : コインのチャイム(構成音)
    CoinPerfect, // ジャスト入力  : コイン+キラキラの3連
    BeamFire,    // ビーム発射    : 下降するレーザー音+低音
    WaveClear,   // ウェーブクリア: コード全体のヒット
    GameOver,    // ゲームオーバー: 下降フレーズ
};

class AudioEngine {
public:
    bool init();                 // オーディオデバイスを開く(失敗してもゲームは続行可)
    void shutdown();
    void trigger(Sfx s);         // 効果音を要求(次の16分音符で発音される)
    void setBattle(bool on) { battle_.store(on); }   // BGMレイヤー切替

    // ---- 映像同期用: ゲームスレッドから現在の音楽位置を読む ----
    bool    ready()      const { return dev_ != 0; }
    double  beatPhase()  const;   // 拍内の位置 0.0-1.0 (画面の脈動などに使う)
    int64_t beatIndex()  const;   // 通算の拍番号
    int     chordIndex() const;   // 現在のコード 0-3 (Am F C G)
    int64_t stepIndex16() const;  // 通算の16分音符番号(先行予約用)

private:
    // ---- 合成ボイス(同時発音の1音) ----
    struct Voice {
        bool     active = false;
        int      wave = 0;          // 0:sin 1:square 2:saw 3:tri 4:noise
        double   phase = 0.0;
        double   freq = 440.0;
        double   slide = 1.0;       // 毎サンプル周波数に掛ける(ピッチ変化)
        float    vol = 0.f;
        float    env = 1.f;         // 減衰エンベロープ
        float    decayMul = 0.999f;
        int      attack = 0;        // クリックノイズ防止の立ち上がり
        uint32_t rng = 0x12345u;    // ノイズ用乱数
    };
    // ---- 未来の拍に予約された音(アルペジオ等) ----
    struct ScheduledNote {
        int64_t step;
        float midi; int wave; float vol; float decay; double slide;
    };

    static void sdlCallback(void* userdata, Uint8* stream, int len);
    void render(float* out, int frames);   // コールバック本体(別スレッド)
    void onStep();                         // 16分音符ごとに呼ばれる
    void spawnVoice(float midi, int wave, float vol, float decaySec,
                    double slide = 1.0);
    void scheduleNote(int64_t step, float midi, int wave, float vol,
                      float decay, double slide = 1.0);

    SDL_AudioDeviceID dev_ = 0;
    int     sampleRate_ = 44100;
    double  samplesPerStep_ = 0.0;   // 16分音符1個分のサンプル数
    int     sampleInStep_ = 0;
    int64_t step_ = 0;               // 通算ステップ(16分音符)カウンタ

    std::vector<Voice>         voices_;
    std::vector<Sfx>           pending_;    // 発音待ちの効果音
    std::vector<ScheduledNote> schedule_;   // 予約済みノート
    std::atomic<bool>          battle_{ false };
    std::atomic<int64_t>       clock_{ 0 };   // 再生済みサンプル数(映像同期用)
    int64_t                    totalSamples_ = 0;
};
