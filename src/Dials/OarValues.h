#pragma once

// OarValues.h - The dial values Open Animation Replacer reads (docs/oar-conditions.md).
//
// OAR evaluates its conditions inside the animation graph update, on worker threads, and the
// dials engine holds its lock while it runs rules. So OAR does not read the engine: the service
// copies the values of the dials that say "oar": true here as it publishes them (after every
// tick), and OAR's ConditionEngineDial condition reads this copy from any thread under a short
// shared lock. Dials without "oar": true are known here by name only, so OAR can say why it
// reads nothing.
//
// Pure data, no game code (tested in DialsTests.cpp).

#include "Definition.h"

#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ConditionEngine::Dials {

    class OarValues {
    public:
        static OarValues& Get();

        // A dial was defined or redefined (any thread): which actors' values it has now
        void Define(const DialDef& def, const std::vector<std::pair<ActorKey, double>>& values);
        void Remove(std::string_view id);
        // A published change (main thread). Dials not shared with OAR are ignored.
        void Update(std::string_view id, ActorKey actor, double value);
        // Before a load / new game: the dials stay, their values go back to the defaults
        void ClearValues();
        void Clear();  // everything (tests)

        enum class Status { Ok, Unknown, NotShared };
        struct Reading {
            Status status = Status::Unknown;
            double value = 0;
            std::string text;  // "42 %", "1 (Formal)", or why there is no value
        };
        // Any thread. `withText` fills `text` (OAR's editor shows it); evaluations skip it.
        Reading Read(std::string_view id, ActorKey actor, bool withText = false) const;

        std::size_t SharedCount() const;

    private:
        struct Entry {
            std::string id;  // as written
            bool shared = false;
            bool global = false;
            Kind kind = Kind::Number;
            double defaultValue = 0;
            std::string unit;
            std::map<int, std::string> options;
            double globalValue = 0;
            std::unordered_map<ActorKey, double> values;
        };

        mutable std::shared_mutex mutex_;
        std::unordered_map<std::string, Entry> dials_;  // lower-case id
    };

} // namespace ConditionEngine::Dials
