#include "AudioEngine.h"
#include "Config.h"
#include <algorithm>
#include <cmath>

// ============================================================
//  AudioEngine.cpp : 実装
//  注意: render()/onStep() はSDLのオーディオスレッドで動く。
//  ゲームスレッドとの共有データ(pending_等)は trigger() 側で
//  SDL_LockAudioDevice により排他する。
// ============================================================

namespace {
constexpr double BPM = MUSIC_BPM;
constexpr int    STEPS_PER_BAR = 16;   // 1小節 = 16分音符×16

// コード進行: Am → F → C → G (1小節ずつループ)
// 値はMIDIノート番号 (57=A3, 60=C4, 64=E4 ...)
constexpr int kProg[4][3] = {
    { 57, 60, 64 },   // Am
    { 53, 57, 60 },   // F
    { 60, 64, 67 },   // C
    { 55, 59, 62 },   // G
};

enum Wave { SIN = 0, SQR, SAW, TRI, NSE };

double midiToFreq(float m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); }
} // namespace

// ------------------------------------------------------------
//  初期化 / 終了 / 要求
// ------------------------------------------------------------

bool AudioEngine::init() {
    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 512;            // レイテンシ約12ms
    want.callback = sdlCallback;
    want.userdata = this;
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (dev_ == 0) return false;   // 音無しでもゲームは動かせる

    sampleRate_ = have.freq;
    samplesPerStep_ = sampleRate_ * 60.0 / BPM / 4.0;  // 16分音符の長さ
    voices_.resize(48);
    pending_.reserve(32);
    schedule_.reserve(64);
    SDL_PauseAudioDevice(dev_, 0);  // 再生開始
    return true;
}

void AudioEngine::shutdown() {
    if (dev_) { SDL_CloseAudioDevice(dev_); dev_ = 0; }
}

void AudioEngine::trigger(Sfx s) {
    if (!dev_) return;
    SDL_LockAudioDevice(dev_);     // オーディオスレッドと排他
    pending_.push_back(s);
    SDL_UnlockAudioDevice(dev_);
}

// ------------------------------------------------------------
//  合成本体 (オーディオスレッド)
// ------------------------------------------------------------

void AudioEngine::sdlCallback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<AudioEngine*>(userdata);
    self->render(reinterpret_cast<float*>(stream), len / int(sizeof(float)));
}

void AudioEngine::render(float* out, int frames) {
    for (int i = 0; i < frames; ++i) {
        if (sampleInStep_ == 0) onStep();   // 16分音符の頭で発音処理

        // ---- 全ボイスをミックス ----
        float mix = 0.f;
        for (auto& v : voices_) {
            if (!v.active) continue;
            float s = 0.f;
            switch (v.wave) {
                case SIN: s = float(std::sin(v.phase * 6.2831853)); break;
                case SQR: s = (v.phase < 0.5) ? 0.6f : -0.6f; break;
                case SAW: s = float(v.phase * 2.0 - 1.0) * 0.7f; break;
                case TRI: s = float(std::fabs(v.phase - 0.5) * 4.0 - 1.0); break;
                case NSE:
                    v.rng = v.rng * 1664525u + 1013904223u;   // xorshift系LCG
                    s = (int32_t(v.rng) / 2147483648.f) * 0.6f;
                    break;
            }
            float gain = v.env;
            if (v.attack < 64) gain *= v.attack++ / 64.f;     // クリック防止
            mix += s * v.vol * gain;

            v.phase += v.freq / sampleRate_;
            if (v.phase >= 1.0) v.phase -= 1.0;
            v.freq *= v.slide;
            v.env *= v.decayMul;
            if (v.env < 0.001f) v.active = false;
        }
        out[i] = std::tanh(mix * 0.9f) * 0.85f;   // ソフトクリップで歪み防止

        if (++sampleInStep_ >= int(samplesPerStep_)) {
            sampleInStep_ = 0;
            step_++;
        }
        clock_.store(++totalSamples_, std::memory_order_relaxed);
    }
}

// 16分音符ごとの処理: 効果音のクオンタイズ発音 + BGMシーケンス
void AudioEngine::onStep() {
    const int  p     = int(step_ % STEPS_PER_BAR);          // 小節内位置 0-15
    const int* chord = kProg[(step_ / STEPS_PER_BAR) % 4];  // 現在のコード
    const bool battle = battle_.load();

    // ---- (1) 要求された効果音を「今この拍」で発音 ----
    //      音高は現在のコード構成音から選ぶのでBGMと必ずハモる
    for (Sfx s : pending_) {
        switch (s) {
        case Sfx::GunShot:     // 構成音をランダムに選んだ高音ブリップ
            spawnVoice(float(chord[(step_ * 7) % 3] + 24), SQR, 0.13f, 0.07f);
            break;
        case Sfx::CannonShot:  // ルート音1オクターブ下の爆発音 + ノイズ
            spawnVoice(float(chord[0] - 24), SIN, 0.50f, 0.22f, 0.9996);
            spawnVoice(60.f, NSE, 0.22f, 0.10f);
            break;
        case Sfx::IceShot:     // 5度の音から下降グリッサンド
            spawnVoice(float(chord[2] + 24), TRI, 0.20f, 0.16f, 0.99993);
            break;
        case Sfx::SniperShot:  // 鋭く長い高音
            spawnVoice(float(chord[2] + 12), SAW, 0.20f, 0.28f);
            break;
        case Sfx::Kill:        // コードの上昇アルペジオ(16分×3連)
            scheduleNote(step_,     float(chord[0] + 12), TRI, 0.20f, 0.14f);
            scheduleNote(step_ + 1, float(chord[1] + 12), TRI, 0.20f, 0.14f);
            scheduleNote(step_ + 2, float(chord[2] + 12), TRI, 0.22f, 0.20f);
            break;
        case Sfx::Leak:        // ルートの半音下=不協和音で「失敗」を表現
            spawnVoice(float(chord[0] - 13), SQR, 0.26f, 0.35f);
            spawnVoice(40.f, NSE, 0.15f, 0.15f);
            break;
        case Sfx::Place:
            spawnVoice(float(chord[0] + 12), SIN, 0.18f, 0.10f);
            break;
        case Sfx::Coin:        // コイン音: 構成音→4度上(マリオのコイン的跳躍)
            spawnVoice(float(chord[2] + 24), SQR, 0.15f, 0.05f);
            scheduleNote(step_ + 1, float(chord[0] + 36), SIN, 0.20f, 0.22f);
            break;
        case Sfx::CoinPerfect: // ジャスト時はさらにキラッと一音足す
            spawnVoice(float(chord[2] + 24), SQR, 0.15f, 0.05f);
            scheduleNote(step_ + 1, float(chord[0] + 36), SIN, 0.22f, 0.25f);
            scheduleNote(step_ + 2, float(chord[2] + 36), TRI, 0.18f, 0.35f);
            break;
        case Sfx::BeamFire:    // レーザー: 下降スライド+サブベース+ノイズ
            spawnVoice(float(chord[0] + 24), SAW, 0.26f, 0.45f, 0.99965);
            spawnVoice(float(chord[0] - 12), SIN, 0.40f, 0.30f);
            spawnVoice(70.f, NSE, 0.18f, 0.12f);
            break;
        case Sfx::WaveClear:   // コード全体を同時に鳴らすファンファーレ
            spawnVoice(float(chord[0]),      TRI, 0.18f, 0.50f);
            spawnVoice(float(chord[1]),      TRI, 0.18f, 0.50f);
            spawnVoice(float(chord[2]),      TRI, 0.18f, 0.50f);
            spawnVoice(float(chord[0] + 24), SIN, 0.15f, 0.60f);
            break;
        case Sfx::GameOver:    // 8分音符刻みの下降フレーズ
            scheduleNote(step_,     float(chord[2] + 12), SAW, 0.22f, 0.40f);
            scheduleNote(step_ + 2, float(chord[1] + 12), SAW, 0.22f, 0.40f);
            scheduleNote(step_ + 4, float(chord[0] + 12), SAW, 0.22f, 0.40f);
            scheduleNote(step_ + 6, float(chord[0]),      SAW, 0.26f, 0.80f);
            break;
        }
    }
    pending_.clear();

    // ---- (2) 予約済みノートのうち、時刻が来たものを発音 ----
    for (auto it = schedule_.begin(); it != schedule_.end();) {
        if (it->step <= step_) {
            spawnVoice(it->midi, it->wave, it->vol, it->decay, it->slide);
            it = schedule_.erase(it);
        } else ++it;
    }

    // ---- (3) BGMシーケンス(垂直レイヤリング) ----
    if (battle) {
        // 戦闘中レイヤー: キック+ハイハット+シンコペーションするベース+速いアルペジオ
        if (p % 8 == 0)
            spawnVoice(31.f, SIN, 0.50f, 0.12f, 0.9990);            // キック
        if (p % 2 == 0)
            spawnVoice(96.f, NSE, (p % 4 == 2) ? 0.09f : 0.05f, 0.03f); // ハット
        if (p == 0 || p == 6 || p == 8 || p == 14)
            spawnVoice(float(chord[0] - 12), SAW, 0.20f, 0.18f);    // ベース
        if (p % 2 == 0)
            spawnVoice(float(chord[(p / 2) % 3] + 24), TRI, 0.07f, 0.09f); // アルペジオ
    } else {
        // 平時レイヤー: ゆったりしたベースと4分音符のアルペジオのみ
        if (p == 0)
            spawnVoice(float(chord[0] - 12), TRI, 0.20f, 0.80f);
        if (p % 4 == 0)
            spawnVoice(float(chord[(p / 4) % 3] + 12), TRI, 0.07f, 0.35f);
    }
}

// ------------------------------------------------------------
//  ボイス管理
// ------------------------------------------------------------

void AudioEngine::spawnVoice(float midi, int wave, float vol, float decaySec,
                             double slide) {
    for (auto& v : voices_) {
        if (v.active) continue;
        v.active = true;
        v.wave = wave;
        v.phase = 0.0;
        v.freq = midiToFreq(midi);
        v.slide = slide;
        v.vol = vol;
        v.env = 1.f;
        // decaySec秒でエンベロープが0.1%まで減衰する係数
        v.decayMul = float(std::pow(0.001, 1.0 / (decaySec * sampleRate_)));
        v.attack = 0;
        return;
    }
    // 空きボイスが無ければ諦める(最大48同時発音)
}

void AudioEngine::scheduleNote(int64_t step, float midi, int wave, float vol,
                               float decay, double slide) {
    schedule_.push_back({ step, midi, wave, vol, decay, slide });
}

// ------------------------------------------------------------
//  映像同期用アクセサ (ゲームスレッドから呼ばれる)
// ------------------------------------------------------------
double AudioEngine::beatPhase() const {
    if (samplesPerStep_ <= 0.0) return 0.0;
    const double spb = samplesPerStep_ * 4.0;   // 1拍 = 16分音符×4
    const double c = double(clock_.load(std::memory_order_relaxed));
    return std::fmod(c, spb) / spb;
}
int64_t AudioEngine::beatIndex() const {
    if (samplesPerStep_ <= 0.0) return 0;
    return int64_t(clock_.load(std::memory_order_relaxed) / (samplesPerStep_ * 4.0));
}
int64_t AudioEngine::stepIndex16() const {
    if (samplesPerStep_ <= 0.0) return 0;
    return int64_t(clock_.load(std::memory_order_relaxed) / samplesPerStep_);
}
int AudioEngine::chordIndex() const {
    if (samplesPerStep_ <= 0.0) return 0;
    const int64_t st = int64_t(clock_.load(std::memory_order_relaxed) / samplesPerStep_);
    return int((st / 16) % 4);
}
