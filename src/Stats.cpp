#include "ConditionEngine/Stats.h"
#include "ConditionEngine/Availability.h"
#include "ConditionEngine/Watcher.h"

#include <array>
#include <mutex>

namespace ConditionEngine::Stats {

    namespace {
        using Clock = std::chrono::steady_clock;
        constexpr std::uint32_t kMaxSlots = 1024;
        constexpr auto relaxed = std::memory_order_relaxed;

        struct TypeCounters {
            std::atomic<std::uint64_t> evaluations{0};
            std::atomic<std::uint64_t> reads{0};
            std::atomic<std::uint64_t> totalNs{0};
            std::atomic<std::uint64_t> selfNs{0};
        };

        std::atomic<bool> g_enabled{false};
        std::array<TypeCounters, kMaxSlots> g_types;

        std::mutex g_namesMutex;
        std::vector<std::string> g_names{"(unregistered)", "Literal", "Math", "NamedValue", "Truth"};

        std::atomic<std::uint64_t> g_ticks{0}, g_tickTotalNs{0}, g_tickMaxNs{0}, g_lastTickNs{0};
        std::atomic<std::uint64_t> g_conditionEvaluations{0}, g_valueSamples{0}, g_posted{0}, g_drains{0};
        std::atomic<std::uint64_t> g_availabilityChecks{0}, g_availabilityNs{0}, g_rebinds{0};
        std::atomic<std::uint64_t> g_papyrus[4]{};
        std::atomic<std::int64_t> g_since{Clock::now().time_since_epoch().count()};

        // Time spent in nested nodes, per open Scope on this thread (for self time)
        thread_local std::vector<std::uint64_t> t_childNs;

        std::uint64_t Ns(Clock::duration d) {
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
            return ns > 0 ? static_cast<std::uint64_t>(ns) : 0;
        }

        void Max(std::atomic<std::uint64_t>& target, std::uint64_t value) {
            auto current = target.load(relaxed);
            while (value > current && !target.compare_exchange_weak(current, value, relaxed)) {}
        }
    }

    bool Enabled() { return g_enabled.load(relaxed); }
    void SetEnabled(bool enabled) { g_enabled.store(enabled, relaxed); }

    std::uint32_t RegisterSlot(std::string_view typeName) {
        std::lock_guard lock(g_namesMutex);
        if (g_names.size() >= kMaxSlots) return kUnregistered;
        g_names.emplace_back(typeName);
        return static_cast<std::uint32_t>(g_names.size() - 1);
    }

    Scope::Scope(std::uint32_t slot, bool read) : slot_(slot < kMaxSlots ? slot : kUnregistered), read_(read) {
        t_childNs.push_back(0);
        start_ = Clock::now();
    }

    Scope::~Scope() {
        const auto elapsed = Ns(Clock::now() - start_);
        const auto children = t_childNs.back();
        t_childNs.pop_back();
        if (!t_childNs.empty()) t_childNs.back() += elapsed;

        auto& counters = g_types[slot_];
        (read_ ? counters.reads : counters.evaluations).fetch_add(1, relaxed);
        counters.totalNs.fetch_add(elapsed, relaxed);
        counters.selfNs.fetch_add(elapsed > children ? elapsed - children : 0, relaxed);
    }

    void RecordTick(Clock::duration duration) {
        const auto ns = Ns(duration);
        g_ticks.fetch_add(1, relaxed);
        g_tickTotalNs.fetch_add(ns, relaxed);
        g_lastTickNs.store(ns, relaxed);
        Max(g_tickMaxNs, ns);
    }

    void AddConditionEvaluations(std::uint64_t count) { g_conditionEvaluations.fetch_add(count, relaxed); }
    void AddValueSamples(std::uint64_t count) { g_valueSamples.fetch_add(count, relaxed); }
    void CountPostedEvent() { g_posted.fetch_add(1, relaxed); }
    void CountDrain() { g_drains.fetch_add(1, relaxed); }

    void RecordAvailabilityCheck(Clock::duration duration) {
        g_availabilityChecks.fetch_add(1, relaxed);
        g_availabilityNs.fetch_add(Ns(duration), relaxed);
    }

    void CountRebind() { g_rebinds.fetch_add(1, relaxed); }

    void CountPapyrusCall(PapyrusOutcome outcome) { g_papyrus[static_cast<int>(outcome)].fetch_add(1, relaxed); }

    nlohmann::json Snapshot() {
        using json = nlohmann::json;
        auto us = [](std::uint64_t ns) { return static_cast<double>(ns) / 1000.0; };
        const auto now = Clock::now().time_since_epoch().count();
        const auto sinceMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 Clock::duration(now - g_since.load(relaxed)))
                                 .count();

        auto& watcher = Watcher::Get();
        const auto ticks = g_ticks.load(relaxed);
        json out{
            {"enabled", Enabled()},
            {"sinceMs", sinceMs},
            {"watcher",
             {{"ticks", ticks},
              {"tickTotalUs", us(g_tickTotalNs.load(relaxed))},
              {"tickAverageUs", ticks ? us(g_tickTotalNs.load(relaxed)) / static_cast<double>(ticks) : 0.0},
              {"tickMaxUs", us(g_tickMaxNs.load(relaxed))},
              {"lastTickUs", us(g_lastTickNs.load(relaxed))},
              {"conditionEvaluations", g_conditionEvaluations.load(relaxed)},
              {"valueSamples", g_valueSamples.load(relaxed)},
              {"postedEvents", g_posted.load(relaxed)},
              {"drains", g_drains.load(relaxed)},
              {"conditionWatches", watcher.GetWatchCount() - watcher.GetValueWatchCount()},
              {"valueWatches", watcher.GetValueWatchCount()}}},
            {"availability",
             {{"checks", g_availabilityChecks.load(relaxed)},
              {"totalUs", us(g_availabilityNs.load(relaxed))},
              {"revision", Availability::Get().Revision()},
              {"rebinds", g_rebinds.load(relaxed)}}},
            {"papyrusCalls",
             {{"dispatched", g_papyrus[0].load(relaxed)},
              {"completed", g_papyrus[1].load(relaxed)},
              {"failed", g_papyrus[2].load(relaxed)},
              {"timedOut", g_papyrus[3].load(relaxed)}}},
        };

        json types = json::array();
        std::lock_guard lock(g_namesMutex);
        for (std::size_t i = 0; i < g_names.size(); ++i) {
            const auto& c = g_types[i];
            const auto evaluations = c.evaluations.load(relaxed);
            const auto reads = c.reads.load(relaxed);
            if (evaluations == 0 && reads == 0) continue;
            types.push_back({{"name", g_names[i]},
                             {"evaluations", evaluations},
                             {"reads", reads},
                             {"totalUs", us(c.totalNs.load(relaxed))},
                             {"selfUs", us(c.selfNs.load(relaxed))}});
        }
        out["types"] = std::move(types);
        return out;
    }

    void Reset() {
        for (auto& c : g_types) {
            c.evaluations.store(0, relaxed);
            c.reads.store(0, relaxed);
            c.totalNs.store(0, relaxed);
            c.selfNs.store(0, relaxed);
        }
        for (auto* a : {&g_ticks, &g_tickTotalNs, &g_tickMaxNs, &g_lastTickNs, &g_conditionEvaluations, &g_valueSamples,
                        &g_posted, &g_drains, &g_availabilityChecks, &g_availabilityNs, &g_rebinds}) {
            a->store(0, relaxed);
        }
        for (auto& p : g_papyrus) p.store(0, relaxed);
        g_since.store(Clock::now().time_since_epoch().count(), relaxed);
    }

} // namespace ConditionEngine::Stats
