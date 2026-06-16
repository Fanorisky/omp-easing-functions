// for animating textdraws in open.mp, pretty dope

#include <sdk.hpp>
#include <Server/Components/Pawn/pawn.hpp>
#include <Server/Components/Pawn/Impl/pawn_natives.hpp>
#include <Server/Components/Pawn/Impl/pawn_impl.hpp>
#include <Server/Components/TextDraws/textdraws.hpp>
#include <Server/Components/Timers/timers.hpp>

#include <cmath>
#include <memory>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <vector>
#include <functional>
#include <algorithm>

namespace Config {
    inline int expectedPlayers = 512;         // player count hint for pre-alloc
    inline int initialAnimCapacity = 64;      // starting vector capacity, grows on demand
    
    // performance tuning so it doesn't lag your server to hell
    inline int updateRateFps = 30;           // 30 FPS default
    inline int batchProcessLimit = 100;      // max animations per update cycle
    inline int callbackReserve = 128;        // pre-allocate callback buffer
}

class EasingComponent;

namespace Easing {
    using EasingFunc = float(*)(float);
    template<typename T> static inline T Lerp(T a, T b, float t);
    static inline Colour LerpColour(Colour a, Colour b, float t);
}

static Easing::EasingFunc GetEasingFunction(int easeType);
static EasingComponent* GetEasingComponent();
static EasingComponent* g_EasingComponent = nullptr;  // global instance

namespace Easing
{
    constexpr float PI = 3.14159265358979323846f;
    constexpr float HALF_PI = PI * 0.5f;
    
    // basic linear interpolation
    template<typename T>
    static inline T Lerp(T a, T b, float t) { return a + (b - a) * t; }
    
    // for colors, interpolate that RGBA
    static inline Colour LerpColour(Colour a, Colour b, float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);  // just in case, don't want out of bounds
        
        return Colour(
            static_cast<uint8_t>(a.r + (b.r - a.r) * t),  // Red
            static_cast<uint8_t>(a.g + (b.g - a.g) * t),  // Green
            static_cast<uint8_t>(a.b + (b.b - a.b) * t),  // Blue
            static_cast<uint8_t>(a.a + (b.a - a.a) * t)   // Alpha
        );
    }
    
    // easing functions -this is what makes shit smooth
    // wrote all of them, even though some are kinda pointless lol
    static inline float Linear(float t) { return t; }  // the most basic one
    static inline float InSine(float t) { return 1.0f - std::cos(t * HALF_PI); }
    static inline float OutSine(float t) { return std::sin(t * HALF_PI); }
    static inline float InOutSine(float t) { return -(std::cos(PI * t) - 1.0f) * 0.5f; }
    static inline float InQuad(float t) { return t * t; }
    static inline float OutQuad(float t) { const float inv = 1.0f - t; return 1.0f - inv * inv; }
    static inline float InOutQuad(float t) { return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f; }
    static inline float InCubic(float t) { return t * t * t; }
    static inline float OutCubic(float t) { const float inv = 1.0f - t; return 1.0f - inv * inv * inv; }
    static inline float InOutCubic(float t) { return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f; }
    static inline float InQuart(float t) { const float t2 = t * t; return t2 * t2; }
    static inline float OutQuart(float t) { const float inv = 1.0f - t; const float inv2 = inv * inv; return 1.0f - inv2 * inv2; }
    static inline float InOutQuart(float t) { return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) * 0.5f; }
    static inline float InQuint(float t) { const float t2 = t * t; return t2 * t2 * t; }
    static inline float OutQuint(float t) { const float inv = 1.0f - t; const float inv2 = inv * inv; return 1.0f - inv2 * inv2 * inv; }
    static inline float InOutQuint(float t) { return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) * 0.5f; }
    static inline float InExpo(float t) { return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f); }
    static inline float OutExpo(float t) { return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }
    static inline float InOutExpo(float t) { if (t == 0.0f) return 0.0f; if (t == 1.0f) return 1.0f; return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) * 0.5f : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) * 0.5f; }
    static inline float InCirc(float t) { return 1.0f - std::sqrt(1.0f - t * t); }
    static inline float OutCirc(float t) { const float tm1 = t - 1.0f; return std::sqrt(1.0f - tm1 * tm1); }
    static inline float InOutCirc(float t) { if (t < 0.5f) { const float t2 = 2.0f * t; return (1.0f - std::sqrt(1.0f - t2 * t2)) * 0.5f; } const float t2 = -2.0f * t + 2.0f; return (std::sqrt(1.0f - t2 * t2) + 1.0f) * 0.5f; }
    static inline float InBack(float t) { constexpr float c1 = 1.70158f; constexpr float c3 = c1 + 1.0f; return c3 * t * t * t - c1 * t * t; }
    static inline float OutBack(float t) { constexpr float c1 = 1.70158f; constexpr float c3 = c1 + 1.0f; const float tm1 = t - 1.0f; return 1.0f + c3 * tm1 * tm1 * tm1 + c1 * tm1 * tm1; }
    static inline float InOutBack(float t) { constexpr float c1 = 1.70158f; constexpr float c2 = c1 * 1.525f; if (t < 0.5f) { const float t2 = 2.0f * t; return (t2 * t2 * ((c2 + 1.0f) * t2 - c2)) * 0.5f; } const float t2 = 2.0f * t - 2.0f; return (t2 * t2 * ((c2 + 1.0f) * t2 + c2) + 2.0f) * 0.5f; }
    static inline float InElastic(float t) { constexpr float c4 = (2.0f * PI) / 3.0f; if (t == 0.0f) return 0.0f; if (t == 1.0f) return 1.0f; return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4); }
    static inline float OutElastic(float t) { constexpr float c4 = (2.0f * PI) / 3.0f; if (t == 0.0f) return 0.0f; if (t == 1.0f) return 1.0f; return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f; }
    static inline float InOutElastic(float t) { constexpr float c5 = (2.0f * PI) / 4.5f; if (t == 0.0f) return 0.0f; if (t == 1.0f) return 1.0f; if (t < 0.5f) { return -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) * 0.5f; } return (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) * 0.5f + 1.0f; }
    static inline float OutBounce(float t) { constexpr float n1 = 7.5625f; constexpr float d1 = 2.75f; constexpr float inv_d1 = 1.0f / d1; if (t < inv_d1) { return n1 * t * t; } else if (t < 2.0f * inv_d1) { const float t2 = t - 1.5f * inv_d1; return n1 * t2 * t2 + 0.75f; } else if (t < 2.5f * inv_d1) { const float t2 = t - 2.25f * inv_d1; return n1 * t2 * t2 + 0.9375f; } const float t2 = t - 2.625f * inv_d1; return n1 * t2 * t2 + 0.984375f; }
    static inline float InBounce(float t) { return 1.0f - OutBounce(1.0f - t); }
    static inline float InOutBounce(float t) { return t < 0.5f ? (1.0f - OutBounce(1.0f - 2.0f * t)) * 0.5f : (1.0f + OutBounce(2.0f * t - 1.0f)) * 0.5f; }
    
    // all the easing functions in one array, cuz why not
    static constexpr EasingFunc EASING_FUNCTIONS[] = {
        InSine, OutSine, InOutSine,
        InQuad, OutQuad, InOutQuad,
        InCubic, OutCubic, InOutCubic,
        InQuart, OutQuart, InOutQuart,
        InQuint, OutQuint, InOutQuint,
        InExpo, OutExpo, InOutExpo,
        InCirc, OutCirc, InOutCirc,
        InBack, OutBack, InOutBack,
        InElastic, OutElastic, InOutElastic,
        InBounce, OutBounce, InOutBounce,
        Linear  // always keep linear at the end i guess
    };
    
    static constexpr int TOTAL_EASING_FUNCTIONS = 31;
}

class AnimationSystem
{
private:
    struct Animation
    {
        bool active = false;
        bool silent = false;  // no callback when done, for chaining or smth
        uint16_t generation = 0;  // bumped on slot reuse so stale ids become invalid (no ABA)
        int activeIndex = -1;     // position inside activeAnimationIndices, -1 when inactive
        int playerId = -1;
        int textDrawId = -1;
        int animationType = -1;
        
        Vector2 startPos, targetPos;
        Vector2 startLetterSize, targetLetterSize;
        Vector2 startTextSize, targetTextSize;
        Colour startColor, targetColor;
        Colour startBoxColor, targetBoxColor;
        Colour startBgColor, targetBgColor;
        
        std::chrono::steady_clock::time_point startTime;
        int durationMs = 0;
        Easing::EasingFunc easingFunc = Easing::Linear;
        
        uint8_t flags = 0;
        static constexpr uint8_t FLAG_POS = 1 << 0;
        static constexpr uint8_t FLAG_LETTER = 1 << 1;
        static constexpr uint8_t FLAG_TEXT = 1 << 2;
        static constexpr uint8_t FLAG_COLOR = 1 << 3;
        static constexpr uint8_t FLAG_BOX = 1 << 4;
        static constexpr uint8_t FLAG_BG = 1 << 5;
        
        inline bool shouldAnimatePosition() const { return flags & FLAG_POS; }
        inline bool shouldAnimateLetterSize() const { return flags & FLAG_LETTER; }
        inline bool shouldAnimateTextSize() const { return flags & FLAG_TEXT; }
        inline bool shouldAnimateColor() const { return flags & FLAG_COLOR; }
        inline bool shouldAnimateBoxColor() const { return flags & FLAG_BOX; }
        inline bool shouldAnimateBgColor() const { return flags & FLAG_BG; }
    };
    
    // hash set keeps dedupe O(1) even with lots of dirty textdraws per player
    struct BatchUpdate
    {
        std::unordered_set<int> dirtyTextDraws;
        
        void addTextDraw(int tdId) {
            dirtyTextDraws.insert(tdId);  // set handles duplicates for us
        }
    };
    
    std::vector<Animation> animations;           // grows on demand
    std::vector<int> freeList;                    // recycled slot indices (O(1) alloc)
    std::unordered_map<int, BatchUpdate> playerBatches;
    size_t updateCursor = 0;                      // round-robin position for batch-limited updates
    
    ICore* core = nullptr;
    ITextDrawsComponent* textDraws = nullptr;
    
    struct CallbackInfo {
        int playerId;
        int animatorId;
        int textDrawId;
        int type;
    };
    std::vector<CallbackInfo> pendingCallbacks;
    
    // track active animations
    // cuz iterating over thousand elements is fkin stupid
    std::vector<int> activeAnimationIndices;
    
    struct Stats {
        size_t totalAnimations = 0;
        size_t peakConcurrent = 0;
        size_t totalCallbacks = 0;
        size_t silentAnimations = 0;
        std::chrono::milliseconds avgUpdateTime{0};
    } stats;
    
public:
    AnimationSystem(ICore* c) : core(c)
    {
        animations.reserve(Config::initialAnimCapacity);
        freeList.reserve(Config::initialAnimCapacity);
        pendingCallbacks.reserve(Config::callbackReserve);
        activeAnimationIndices.reserve(Config::initialAnimCapacity);
        playerBatches.reserve(Config::expectedPlayers);
    }
    
    void SetTextDrawsComponent(ITextDrawsComponent* td) { textDraws = td; }
    
    // anim ids encode { generation | slot } so recycled slots don't alias old ids
    static constexpr int SLOT_BITS = 16;
    static constexpr int SLOT_MASK = 0xFFFF;
    static constexpr int GEN_MASK = 0x7FFF;  // 15 bits, keeps ids non-negative
    
    static inline int MakeAnimId(int slot, uint16_t gen)
    {
        return ((static_cast<int>(gen) & GEN_MASK) << SLOT_BITS) | (slot & SLOT_MASK);
    }
    static inline int SlotFromId(int animId) { return animId & SLOT_MASK; }
    static inline uint16_t GenFromId(int animId) { return static_cast<uint16_t>((animId >> SLOT_BITS) & GEN_MASK); }
    
    // create a new animation, returns anim id or -1 if fucked
    int CreateAnimation(int playerId, int textDrawId,
        const Vector2& startPos, const Vector2& targetPos,
        const Vector2& startLetterSize, const Vector2& targetLetterSize,
        const Vector2& startTextSize, const Vector2& targetTextSize,
        const Colour& startColor, const Colour& targetColor,
        const Colour& startBoxColor, const Colour& targetBoxColor,
        const Colour& startBgColor, const Colour& targetBgColor,
        int durationMs, Easing::EasingFunc easingFunc,
        bool animPos, bool animLetterSize, bool animTextSize,
        bool animColor, bool animBoxColor, bool animBgColor,
        int animType = -1, bool silent = false)
    {
        if (durationMs < 0) durationMs = 0;
        if (!easingFunc) easingFunc = Easing::Linear;
        
        // grab a recycled slot OR grow the vector
        int slot;
        if (!freeList.empty())
        {
            slot = freeList.back();
            freeList.pop_back();
            // bump generation so stale ids pointing at this slot become invalid
            animations[slot].generation = static_cast<uint16_t>((animations[slot].generation + 1) & GEN_MASK);
        }
        else
        {
            if (animations.size() > static_cast<size_t>(SLOT_MASK)) return -1;  // slot space exhausted
            slot = static_cast<int>(animations.size());
            animations.emplace_back();  // grow by 1 on demand
        }
        
        auto& a = animations[slot];
        a.active = true;
        a.silent = silent;
        a.playerId = playerId;
        a.textDrawId = textDrawId;
        a.animationType = animType;
        a.startPos = startPos;
        a.targetPos = targetPos;
        a.startLetterSize = startLetterSize;
        a.targetLetterSize = targetLetterSize;
        a.startTextSize = startTextSize;
        a.targetTextSize = targetTextSize;
        a.startColor = startColor;
        a.targetColor = targetColor;
        a.startBoxColor = startBoxColor;
        a.targetBoxColor = targetBoxColor;
        a.startBgColor = startBgColor;
        a.targetBgColor = targetBgColor;
        a.startTime = std::chrono::steady_clock::now();
        a.durationMs = durationMs;
        a.easingFunc = easingFunc;
        
        a.flags = 0;
        if (animPos) a.flags |= Animation::FLAG_POS;
        if (animLetterSize) a.flags |= Animation::FLAG_LETTER;
        if (animTextSize) a.flags |= Animation::FLAG_TEXT;
        if (animColor) a.flags |= Animation::FLAG_COLOR;
        if (animBoxColor) a.flags |= Animation::FLAG_BOX;
        if (animBgColor) a.flags |= Animation::FLAG_BG;
        
        a.activeIndex = static_cast<int>(activeAnimationIndices.size());
        activeAnimationIndices.push_back(slot);
        
        stats.totalAnimations++;
        if (silent) stats.silentAnimations++;
        if (activeAnimationIndices.size() > stats.peakConcurrent) {
            stats.peakConcurrent = activeAnimationIndices.size();
        }
        
        return MakeAnimId(slot, a.generation);
    }
    
    bool StopAnimation(int animId)
    {
        if (animId < 0) return false;
        
        const int slot = SlotFromId(animId);
        if (slot >= static_cast<int>(animations.size())) return false;
        
        auto& a = animations[slot];
        if (!a.active || a.generation != GenFromId(animId)) return false;  // stale/reused id
        
        a.active = false;
        RemoveFromActive(slot);     // O(1) instead of a linear search
        freeList.push_back(slot);   // recycle slot
        return true;
    }
    
    // batch-limited update
    // cuz don't want to freeze the server with maxed animations
    void Update()
    {
        if (activeAnimationIndices.empty()) return;
        
        auto updateStart = std::chrono::steady_clock::now();
        const auto now = updateStart;
        
        playerBatches.clear();
        
        // honor batch_process_limit: process at most N animations per tick,
        // round-robin via updateCursor so every animation still progresses
        const size_t totalAnims = activeAnimationIndices.size();
        const size_t limit = (Config::batchProcessLimit > 0)
            ? std::min(static_cast<size_t>(Config::batchProcessLimit), totalAnims)
            : totalAnims;
        
        if (updateCursor >= activeAnimationIndices.size())
        {
            updateCursor = 0;
        }
        
        size_t pos = updateCursor;
        size_t processed = 0;
        
        while (processed < limit && !activeAnimationIndices.empty())
        {
            if (pos >= activeAnimationIndices.size())
            {
                pos = 0;  // wrap around
            }
            
            const int slot = activeAnimationIndices[pos];
            auto& anim = animations[slot];
            ++processed;
            
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - anim.startTime).count();
            
            const float t = (anim.durationMs > 0) ? 
                std::min(static_cast<float>(elapsed) / anim.durationMs, 1.0f) : 1.0f;
            
            IPlayer* player = core->getPlayers().get(anim.playerId);
            IPlayerTextDrawData* tdData = player ? queryExtension<IPlayerTextDrawData>(player) : nullptr;
            IPlayerTextDraw* td = tdData ? tdData->get(anim.textDrawId) : nullptr;
            
            if (!td)
            {
                // player gone or textdraw destroyed, drop the animation
                anim.active = false;
                RemoveFromActive(slot);
                freeList.push_back(slot);
                continue;  // don't advance pos: a swapped-in element now lives here
            }
            
            UpdateTextDrawProperties(td, anim, anim.easingFunc(t));
            
            // Add to batch
            playerBatches[anim.playerId].addTextDraw(anim.textDrawId);
            
            if (t >= 1.0f)
            {
                if (!anim.silent)
                {
                    pendingCallbacks.push_back({
                        anim.playerId,
                        MakeAnimId(slot, anim.generation),
                        anim.textDrawId,
                        anim.animationType
                    });
                    
                    stats.totalCallbacks++;
                }
                
                anim.active = false;
                RemoveFromActive(slot);
                freeList.push_back(slot);  // recycle slot back to pool
                // don't advance pos here either
            }
            else
            {
                ++pos;
            }
        }
        
        updateCursor = pos;
        
        FlushBatchUpdates();
        
        auto updateEnd = std::chrono::steady_clock::now();
        const auto elapsedUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(updateEnd - updateStart);
        // exponential moving average instead of overwriting every tick
        stats.avgUpdateTime = (stats.avgUpdateTime * 7 + elapsedUpdate) / 8;
    }
    
    std::vector<CallbackInfo> GetPendingCallbacks()
    {
        return std::move(pendingCallbacks);
    }
    
    void CleanupPlayerAnimations(int playerId, bool triggerCallbacks = false)
    {
        for (size_t pos = 0; pos < activeAnimationIndices.size();)
        {
            const int slot = activeAnimationIndices[pos];
            auto& anim = animations[slot];
            
            if (anim.active && anim.playerId == playerId)
            {
                if (triggerCallbacks && !anim.silent)
                {
                    pendingCallbacks.push_back({
                        playerId,
                        MakeAnimId(slot, anim.generation),
                        anim.textDrawId,
                        anim.animationType
                    });
                }
                anim.active = false;
                RemoveFromActive(slot);
                freeList.push_back(slot);  // recycle slot
            }
            else
            {
                ++pos;
            }
        }
        
        if (updateCursor >= activeAnimationIndices.size())
        {
            updateCursor = 0;
        }
    }
    
    // Stop all running animations and reset the pool
    void ResetAll()
    {
        for (int slot : activeAnimationIndices)
        {
            animations[slot].active = false;
            animations[slot].activeIndex = -1;
            freeList.push_back(slot);
        }
        activeAnimationIndices.clear();
        pendingCallbacks.clear();
        updateCursor = 0;
    }
    
    int GetActiveAnimationCount() const
    {
        return static_cast<int>(activeAnimationIndices.size());
    }
    
    bool IsAnimationActive(int animId) const
    {
        if (animId < 0) return false;
        const int slot = SlotFromId(animId);
        return slot < static_cast<int>(animations.size())
            && animations[slot].active
            && animations[slot].generation == GenFromId(animId);  // reject stale ids
    }
    
    const Stats& GetStats() const { return stats; }
    
private:
    // O(1) removal from the active list using the stored position
    void RemoveFromActive(int slot)
    {
        const int pos = animations[slot].activeIndex;
        if (pos < 0) return;
        
        const int last = activeAnimationIndices.back();
        activeAnimationIndices[pos] = last;
        animations[last].activeIndex = pos;
        activeAnimationIndices.pop_back();
        animations[slot].activeIndex = -1;
    }
    
    void UpdateTextDrawProperties(IPlayerTextDraw* td, const Animation& anim, float t)
    {
        if (anim.shouldAnimatePosition())
        {
            td->setPosition({
                Easing::Lerp(anim.startPos.x, anim.targetPos.x, t),
                Easing::Lerp(anim.startPos.y, anim.targetPos.y, t)
            });
        }
        
        if (anim.shouldAnimateLetterSize())
        {
            td->setLetterSize({
                Easing::Lerp(anim.startLetterSize.x, anim.targetLetterSize.x, t),
                Easing::Lerp(anim.startLetterSize.y, anim.targetLetterSize.y, t)
            });
        }
        
        if (anim.shouldAnimateTextSize())
        {
            td->setTextSize({
                Easing::Lerp(anim.startTextSize.x, anim.targetTextSize.x, t),
                Easing::Lerp(anim.startTextSize.y, anim.targetTextSize.y, t)
            });
        }
        
        if (anim.shouldAnimateColor())
        {
            td->setColour(Easing::LerpColour(anim.startColor, anim.targetColor, t));
        }
        
        if (anim.shouldAnimateBoxColor())
        {
            td->setBoxColour(Easing::LerpColour(anim.startBoxColor, anim.targetBoxColor, t));
        }
        
        if (anim.shouldAnimateBgColor())
        {
            td->setBackgroundColour(Easing::LerpColour(anim.startBgColor, anim.targetBgColor, t));
        }
    }
    
    void FlushBatchUpdates()
    {
        for (auto& [playerId, batch] : playerBatches)
        {
            IPlayer* player = core->getPlayers().get(playerId);
            if (!player) continue;
            
            auto tdData = queryExtension<IPlayerTextDrawData>(player);
            if (!tdData) continue;
            
            // Show all dirty textdraws in one batch
            for (int tdId : batch.dirtyTextDraws)
            {
                IPlayerTextDraw* td = tdData->get(tdId);
                if (td)
                {
                    td->show();
                }
            }
        }
    }
};

class EasingComponent final : public IComponent, public PawnEventHandler, public PlayerConnectEventHandler
{
private:
    ICore* core = nullptr;
    IPawnComponent* pawn = nullptr;
    ITimersComponent* timers = nullptr;
    ITextDrawsComponent* textDraws = nullptr;
    
    std::unique_ptr<AnimationSystem> animSystem;
    ITimer* updateTimer = nullptr;
    
    class AnimationTimerHandler : public TimerTimeOutHandler
    {
    private:
        EasingComponent* easingComponent;
        
    public:
        AnimationTimerHandler(EasingComponent* component) : easingComponent(component) {}
        
        void timeout(ITimer& timer) override
        {
            if (easingComponent)
            {
                easingComponent->UpdateAnimations();
            }
        }
        
        void free(ITimer& timer) override
        {
            delete this;
        }
    };
    
public:
    PROVIDE_UID(0xE45F1A2D3C8B9E70);
    
    ~EasingComponent()
    {
        if (pawn)
        {
            pawn->getEventDispatcher().removeEventHandler(this);
        }
        
        if (updateTimer)
        {
            updateTimer->kill();
            updateTimer = nullptr;
        }
        
        g_EasingComponent = nullptr;
    }
    
    void onAmxLoad(IPawnScript& script) override
    {
        pawn_natives::AmxLoad(script.GetAMX());
    }
    
    void onAmxUnload(IPawnScript& script) override {}
    
    StringView componentName() const override
    {
        return "open.mp easing-functions";
    }
    
    SemanticVersion componentVersion() const override
    {
        return SemanticVersion(1, 0, 3, 0);
    }
    
    void onLoad(ICore* c) override
    {
        core = c;
        
        // Extract config from config.json, otherwise use defaults
        auto& config = core->getConfig();
        if (int* fps = config.getInt("easing.update_rate_fps"))
            Config::updateRateFps = *fps;
        if (int* batchLimit = config.getInt("easing.batch_process_limit"))
            Config::batchProcessLimit = *batchLimit;
        if (int* players = config.getInt("easing.expected_players"))
            Config::expectedPlayers = *players;
        if (int* initialCap = config.getInt("easing.initial_capacity"))
            Config::initialAnimCapacity = *initialCap;
        if (int* cbReserve = config.getInt("easing.callback_reserve"))
            Config::callbackReserve = *cbReserve;

        // Clamp config to safe ranges so bad config.json values can't crash the
        // server (negative reserve() throws, fps <= 0 breaks the timer interval).
        if (Config::updateRateFps < 1) Config::updateRateFps = 30;
        if (Config::batchProcessLimit < 0) Config::batchProcessLimit = 0;  // 0 = no limit
        if (Config::expectedPlayers < 0) Config::expectedPlayers = 0;
        if (Config::initialAnimCapacity < 0) Config::initialAnimCapacity = 0;
        if (Config::callbackReserve < 0) Config::callbackReserve = 0;

        // Calculate MS from FPS for internal timer
        int intervalMs = 1000 / (Config::updateRateFps > 0 ? Config::updateRateFps : 30);
            
        core->printLn(" ");
        core->printLn("  open.mp easing-functions component loaded!");
        core->printLn("  Animation Pool: dynamic (starts at %d, grows on demand)", Config::initialAnimCapacity);
        core->printLn("  Update Rate: %d FPS (%d ms)", Config::updateRateFps, intervalMs);
        core->printLn("  Batch Limit: %d per frame", Config::batchProcessLimit);
        core->printLn("  Author: Fanorisky (https://github.com/Fanorisky/omp-easing-functions)");
        core->printLn(" ");
        
        setAmxLookups(core);
        animSystem = std::make_unique<AnimationSystem>(core);
        g_EasingComponent = this;
    }
    
    void onInit(IComponentList* components) override
    {
        pawn = components->queryComponent<IPawnComponent>();
        timers = components->queryComponent<ITimersComponent>();
        textDraws = components->queryComponent<ITextDrawsComponent>();
        
        if (pawn)
        {
            setAmxFunctions(pawn->getAmxFunctions());
            setAmxLookups(components);
            pawn->getEventDispatcher().addEventHandler(this);
        }
        
        if (animSystem && textDraws)
        {
            animSystem->SetTextDrawsComponent(textDraws);
        }
        
        if (timers)
        {
            int intervalMs = 1000 / (Config::updateRateFps > 0 ? Config::updateRateFps : 30);
            updateTimer = timers->create(
                new AnimationTimerHandler(this),
                std::chrono::milliseconds(intervalMs),
                true
            );
        }
        
        // Register as player event handler for cleanup
        if (core)
        {
            core->getPlayers().getPlayerConnectDispatcher().addEventHandler(this);
        }
    }
    
    // Auto-cleanup on player disconnect
    void onPlayerDisconnect(IPlayer& player, PeerDisconnectReason reason) override
    {
        if (animSystem)
        {
            animSystem->CleanupPlayerAnimations(player.getID(), false);
        }
    }
    
    void UpdateAnimations()
    {
        if (!animSystem) return;
        animSystem->Update();
        ProcessCallbacks();
    }
    
    void ProcessCallbacks()
    {
        if (!animSystem || !pawn) return;
        
        auto callbacks = animSystem->GetPendingCallbacks();
        if (callbacks.empty()) return;
        
        for (const auto& cb : callbacks)
        {
            if (auto script = pawn->mainScript())
            {
                script->Call("OnAnimatorFinish", DefaultReturnValue_False, 
                           cb.playerId, cb.animatorId, cb.textDrawId, cb.type);
            }
            
            for (IPawnScript* script : pawn->sideScripts())
            {
                script->Call("OnAnimatorFinish", DefaultReturnValue_False, 
                           cb.playerId, cb.animatorId, cb.textDrawId, cb.type);
            }
        }
    }
    
    void onReady() override
    {
    }
    
    void onFree(IComponent* component) override
    {
        if (component == pawn)
        {
            pawn = nullptr;
            setAmxFunctions();
            setAmxLookups();
        }
        else if (component == timers)
        {
            timers = nullptr;
            updateTimer = nullptr;
        }
        else if (component == textDraws)
        {
            textDraws = nullptr;
        }
    }
    
    void free() override
    {
        delete this;
    }
    
    void reset() override
    {
        if (animSystem)
        {
            animSystem->ResetAll();
        }
    }
    
    AnimationSystem* GetAnimationSystem() const { return animSystem.get(); }
    int GetActiveAnimations() const { return animSystem ? animSystem->GetActiveAnimationCount() : 0; }
};

COMPONENT_ENTRY_POINT()
{
    return new EasingComponent();
}

static inline Easing::EasingFunc GetEasingFunction(int easeType)
{
    if (easeType >= 0 && easeType < Easing::TOTAL_EASING_FUNCTIONS)
    {
        return Easing::EASING_FUNCTIONS[easeType];
    }
    return Easing::Linear;
}

static inline EasingComponent* GetEasingComponent()
{
    return g_EasingComponent;
}

// these are the pawn natives, exposed to the scripts
SCRIPT_API(GetEasingValue, float(float t, int easeType)){
    t = std::clamp(t, 0.0f, 1.0f);
    return GetEasingFunction(easeType)(t);
}

SCRIPT_API(Lerp, float(float start, float end, float t))
{
    t = std::clamp(t, 0.0f, 1.0f);
    return Easing::Lerp(start, end, t);
}

SCRIPT_API(LerpColor, int(int color1, int color2, float t))
{
    t = std::clamp(t, 0.0f, 1.0f);
    Colour c1 = Colour::FromRGBA(color1);
    Colour c2 = Colour::FromRGBA(color2);
    Colour result = Easing::LerpColour(c1, c2, t);
    return result.RGBA();
}

static constexpr int ANIMATOR_POSITION = 0;
static constexpr int ANIMATOR_LETTER_SIZE = 1;
static constexpr int ANIMATOR_TEXT_SIZE = 2;
static constexpr int ANIMATOR_FULL_SIZE = 3;
static constexpr int ANIMATOR_COLOR = 4;
static constexpr int ANIMATOR_BOX_COLOR = 5;
static constexpr int ANIMATOR_BACKGROUND_COLOR = 6;

namespace {
    inline int CreateMoveAnimation(
        IPlayer& player, IPlayerTextDraw& td,
        std::optional<float> targetX,
        std::optional<float> targetY,
        int duration, int easeType, bool silent = false)
    {
        auto component = GetEasingComponent();
        if (!component || !component->GetAnimationSystem()) return -1;
        
        if (duration < 0) duration = 0;
        
        Vector2 startPos = td.getPosition();
        Vector2 targetPos = startPos;
        
        if (targetX.has_value()) targetPos.x = *targetX;
        if (targetY.has_value()) targetPos.y = *targetY;
        
        Easing::EasingFunc easeFunc = GetEasingFunction(easeType);
        Colour transparent(0, 0, 0, 0);
        
        return component->GetAnimationSystem()->CreateAnimation(
            player.getID(), td.getID(),
            startPos, targetPos,
            Vector2(0, 0), Vector2(0, 0),
            Vector2(0, 0), Vector2(0, 0),
            transparent, transparent,
            transparent, transparent,
            transparent, transparent,
            duration, easeFunc,
            true, false, false,
            false, false, false,
            ANIMATOR_POSITION, silent
        );
    }
    
    inline int CreateColorAnimation(
        IPlayer& player, IPlayerTextDraw& td,
        int targetColorRGBA, int duration, int easeType,
        int animType,
        std::function<Colour(IPlayerTextDraw&)> getCurrentColor,
        bool animColor, bool animBox, bool animBg,
        bool silent = false)
    {
        auto component = GetEasingComponent();
        if (!component || !component->GetAnimationSystem()) return -1;
        
        if (duration < 0) duration = 0;
        
        Colour startColor = getCurrentColor(td);
        Colour targetColor = Colour::FromRGBA(targetColorRGBA);
        Easing::EasingFunc easeFunc = GetEasingFunction(easeType);
        
        Colour transparent(0, 0, 0, 0);
        Colour startCol = animColor ? startColor : transparent;
        Colour targetCol = animColor ? targetColor : transparent;
        Colour startBox = animBox ? startColor : transparent;
        Colour targetBox = animBox ? targetColor : transparent;
        Colour startBg = animBg ? startColor : transparent;
        Colour targetBg = animBg ? targetColor : transparent;
        
        return component->GetAnimationSystem()->CreateAnimation(
            player.getID(), td.getID(),
            Vector2(0, 0), Vector2(0, 0),
            Vector2(0, 0), Vector2(0, 0),
            Vector2(0, 0), Vector2(0, 0),
            startCol, targetCol,
            startBox, targetBox,
            startBg, targetBg,
            duration, easeFunc,
            false, false, false,
            animColor, animBox, animBg,
            animType, silent
        );
    }
}

// Position animations
SCRIPT_API(PlayerText_MoveTo, int(IPlayer& player, IPlayerTextDraw& td, Vector2 pos, int duration, int easeType, bool silent))
{
    return CreateMoveAnimation(player, td, pos.x, pos.y, duration, easeType, silent);
}

SCRIPT_API(PlayerText_MoveToX, int(IPlayer& player, IPlayerTextDraw& td, float x, int duration, int easeType, bool silent))
{
    return CreateMoveAnimation(player, td, x, std::nullopt, duration, easeType, silent);
}

SCRIPT_API(PlayerText_MoveToY, int(IPlayer& player, IPlayerTextDraw& td, float y, int duration, int easeType, bool silent))
{
    return CreateMoveAnimation(player, td, std::nullopt, y, duration, easeType, silent);
}

// Size animations
SCRIPT_API(PlayerText_MoveLetterSizeX, int(IPlayer& player, IPlayerTextDraw& td, float sizeX, int duration, int easeType, bool silent))
{
    auto component = GetEasingComponent();
    if (!component || !component->GetAnimationSystem()) return -1;
    
    if (duration < 0) duration = 0;
    
    Vector2 startSize = td.getLetterSize();
    Vector2 targetSize = {sizeX, startSize.y};
    Easing::EasingFunc easeFunc = GetEasingFunction(easeType);
    Colour transparent(0, 0, 0, 0);
    
    return component->GetAnimationSystem()->CreateAnimation(
        player.getID(), td.getID(),
        Vector2(0, 0), Vector2(0, 0),
        startSize, targetSize,
        Vector2(0, 0), Vector2(0, 0),
        transparent, transparent,
        transparent, transparent,
        transparent, transparent,
        duration, easeFunc,
        false, true, false,
        false, false, false,
        ANIMATOR_LETTER_SIZE, silent
    );
}

SCRIPT_API(PlayerText_MoveLetterSizeY, int(IPlayer& player, IPlayerTextDraw& td, float sizeY, int duration, int easeType, bool silent))
{
    auto component = GetEasingComponent();
    if (!component || !component->GetAnimationSystem()) return -1;
    
    if (duration < 0) duration = 0;
    
    Vector2 startSize = td.getLetterSize();
    Vector2 targetSize = {startSize.x, sizeY};
    Easing::EasingFunc easeFunc = GetEasingFunction(easeType);
    Colour transparent(0, 0, 0, 0);
    
    return component->GetAnimationSystem()->CreateAnimation(
        player.getID(), td.getID(),
        Vector2(0, 0), Vector2(0, 0),
        startSize, targetSize,
        Vector2(0, 0), Vector2(0, 0),
        transparent, transparent,
        transparent, transparent,
        transparent, transparent,
        duration, easeFunc,
        false, true, false,
        false, false, false,
        ANIMATOR_LETTER_SIZE, silent
    );
}

SCRIPT_API(PlayerText_MoveTextSizeX, int(IPlayer& player, IPlayerTextDraw& td, float sizeX, int duration, int easeType, bool silent))
{
    auto component = GetEasingComponent();
    if (!component || !component->GetAnimationSystem()) return -1;
    
    if (duration < 0) duration = 0;
    
    Vector2 startSize = td.getTextSize();
    Vector2 targetSize = {sizeX, startSize.y};
    Easing::EasingFunc easeFunc = GetEasingFunction(easeType);
    Colour transparent(0, 0, 0, 0);
    
    return component->GetAnimationSystem()->CreateAnimation(
        player.getID(), td.getID(),
        Vector2(0, 0), Vector2(0, 0),
        Vector2(0, 0), Vector2(0, 0),
        startSize, targetSize,
        transparent, transparent,
        transparent, transparent,
        transparent, transparent,
        duration, easeFunc,
        false, false, true,
        false, false, false,
        ANIMATOR_TEXT_SIZE, silent
    );
}

SCRIPT_API(PlayerText_MoveTextSizeY, int(IPlayer& player, IPlayerTextDraw& td, float sizeY, int duration, int easeType, bool silent))
{
    auto component = GetEasingComponent();
    if (!component || !component->GetAnimationSystem()) return -1;
    
    if (duration < 0) duration = 0;
    
    Vector2 startSize = td.getTextSize();
    Vector2 targetSize = {startSize.x, sizeY};
    Easing::EasingFunc easeFunc = GetEasingFunction(easeType);
    Colour transparent(0, 0, 0, 0);
    
    return component->GetAnimationSystem()->CreateAnimation(
        player.getID(), td.getID(),
        Vector2(0, 0), Vector2(0, 0),
        Vector2(0, 0), Vector2(0, 0),
        startSize, targetSize,
        transparent, transparent,
        transparent, transparent,
        transparent, transparent,
        duration, easeFunc,
        false, false, true,
        false, false, false,
        ANIMATOR_TEXT_SIZE, silent
    );
}

SCRIPT_API(PlayerText_MoveSize, int(IPlayer& player, IPlayerTextDraw& td, Vector2 size, int duration, int easeType, bool silent))
{
    auto component = GetEasingComponent();
    if (!component || !component->GetAnimationSystem()) return -1;
    
    if (duration < 0) duration = 0;
    
    Vector2 startLetterSize = td.getLetterSize();
    Vector2 startTextSize = td.getTextSize();
    Easing::EasingFunc easeFunc = GetEasingFunction(easeType);
    Colour transparent(0, 0, 0, 0);
    
    return component->GetAnimationSystem()->CreateAnimation(
        player.getID(), td.getID(),
        Vector2(0, 0), Vector2(0, 0),
        startLetterSize, {startLetterSize.x, size.y},
        startTextSize, {size.x, startTextSize.y},
        transparent, transparent,
        transparent, transparent,
        transparent, transparent,
        duration, easeFunc,
        false, true, true,
        false, false, false,
        ANIMATOR_FULL_SIZE, silent
    );
}

// Color animations
SCRIPT_API(PlayerText_InterpolateColor, int(IPlayer& player, IPlayerTextDraw& td, int color, int duration, int easeType, bool silent))
{
    return CreateColorAnimation(
        player, td, color, duration, easeType,
        ANIMATOR_COLOR,
        [](IPlayerTextDraw& td) { return td.getLetterColour(); },
        true, false, false, silent
    );
}

SCRIPT_API(PlayerText_InterpolateBoxColor, int(IPlayer& player, IPlayerTextDraw& td, int color, int duration, int easeType, bool silent))
{
    return CreateColorAnimation(
        player, td, color, duration, easeType,
        ANIMATOR_BOX_COLOR,
        [](IPlayerTextDraw& td) { return td.getBoxColour(); },
        false, true, false, silent
    );
}

SCRIPT_API(PlayerText_InterpolateBGColor, int(IPlayer& player, IPlayerTextDraw& td, int color, int duration, int easeType, bool silent))
{
    return CreateColorAnimation(
        player, td, color, duration, easeType,
        ANIMATOR_BACKGROUND_COLOR,
        [](IPlayerTextDraw& td) { return td.getBackgroundColour(); },
        false, false, true, silent
    );
}

// Utility functions
SCRIPT_API(PlayerText_PlaceOnTop, bool(IPlayer& player, IPlayerTextDraw& td))
{
    td.show();
    return true;
}

SCRIPT_API(PlayerText_StopAnimation, bool(int animId))
{
    auto component = GetEasingComponent();
    if (!component || !component->GetAnimationSystem()) return false;
    return component->GetAnimationSystem()->StopAnimation(animId);
}

SCRIPT_API(IsAnimationActive, bool(int animId))
{
    auto component = GetEasingComponent();
    if (!component || !component->GetAnimationSystem()) return false;
    return component->GetAnimationSystem()->IsAnimationActive(animId);
}

// Some debug and monitoring
SCRIPT_API(GetActiveAnimationsCount, int())
{
    auto component = GetEasingComponent();
    if (!component) return 0;
    return component->GetActiveAnimations();
}

SCRIPT_API(GetAnimationStats, bool(int& totalCreated, int& peakConcurrent, int& totalCallbacks, int& silentAnims))
{
    auto component = GetEasingComponent();
    if (!component || !component->GetAnimationSystem()) return false;
    
    const auto& stats = component->GetAnimationSystem()->GetStats();
    totalCreated = static_cast<int>(stats.totalAnimations);
    peakConcurrent = static_cast<int>(stats.peakConcurrent);
    totalCallbacks = static_cast<int>(stats.totalCallbacks);
    silentAnims = static_cast<int>(stats.silentAnimations);
    
    return true;
}