// Benchmark.cpp - Out-of-game timing of the engine's own overhead (issue 088):
// availability checks, expression trees, named values, rule references, stats cost.
// Synthetic types stand in for game reads, so the numbers measure ConditionEngine
// itself, not Skyrim. Run: condition-engine-tests --bench

#include "ConditionEngine/ConditionEngine.h"
#include "ConditionEngine/Expression.h"
#include "ConditionEngine/Stats.h"
#include "ConditionEngine/Value.h"
#include "ConditionEngine/Values/PushedValues.h"

#include <chrono>
#include <cstdio>

namespace {
    using json = nlohmann::json;
    using namespace ConditionEngine;
    using Clock = std::chrono::steady_clock;

    volatile double g_sink = 0;  // keeps results observable

    void RegisterBenchTypes() {
        auto& r = Registry::Get();
        for (const auto& [name, result] : {std::pair{"BenchTrue", true}, std::pair{"BenchFalse", false}}) {
            TypeBuilder(name, name, "other")
                .Fields({Field::Int("id", "Id", 0)})
                .Factory([result](const json& j) {
                    return std::make_unique<CallbackCondition>(
                        "bench", [result](const EvaluationContext&) { return result; }, EventDependency::ActorValue, j);
                })
                .Register(r);
        }
        TypeBuilder("BenchNum", "BenchNum", "other")
            .Fields({Field::Int("id", "Id", 0)})
            .Compare("value")
            .Value({})
            .Factory([](const json& j) {
                return std::make_unique<CallbackValueCondition>(
                    "bench", [](const EvaluationContext&) { return ValueResult::Of(42.0); }, EventDependency::ActorValue, j,
                    "value");
            })
            .Register(r);

        std::string error;
        for (const auto& [name, text] : {std::pair{"bench_a", "BenchNum(1) * 2"}, std::pair{"bench_b", "$bench_a + 1"},
                                         std::pair{"bench_c", "$bench_b / 2"}, std::pair{"bench_d", "clamp($bench_c, 0, 100)"},
                                         std::pair{"bench_e", "$bench_d - 1"}}) {
            if (auto def = NamedValueDef::Parse(name, text, "bench", &error)) r.RegisterNamedValue(def);
        }
        r.RegisterNamed("BenchRule", std::shared_ptr<const Condition>(Expression::CompileAny("BenchTrue(1) && BenchTrue(2)", &error)),
                        "bench");
    }

    // Runs `fn` repeatedly for about `budget`, returns nanoseconds per call
    template <class Fn>
    double Time(Fn&& fn, std::chrono::milliseconds budget = std::chrono::milliseconds(300)) {
        for (int i = 0; i < 1000; ++i) fn();  // warm up
        std::uint64_t calls = 0;
        const auto start = Clock::now();
        auto now = start;
        while (now - start < budget) {
            for (int i = 0; i < 1000; ++i) fn();
            calls += 1000;
            now = Clock::now();
        }
        return std::chrono::duration<double, std::nano>(now - start).count() / static_cast<double>(calls);
    }

    void Row(const char* name, double ns) { std::printf("  %-58s %10.1f ns\n", name, ns); }
}

int RunBenchmark() {
    RegisterBenchTypes();
    EvaluationContext context(nullptr);
    std::string error;

    auto compile = [&](const char* text) {
        auto c = Expression::CompileAny(text, &error);
        if (!c) std::printf("compile failed: %s: %s\n", text, error.c_str());
        return std::shared_ptr<const Condition>(std::move(c));
    };
    auto value = [&](const char* text) {
        auto v = Expression::CompileValueAny(text, &error);
        if (!v) std::printf("compile failed: %s: %s\n", text, error.c_str());
        return std::shared_ptr<const Value>(std::move(v));
    };

    const auto leaf = compile("BenchTrue(1)");
    const auto and8 = compile(
        "BenchTrue(1) && BenchTrue(2) && BenchTrue(3) && BenchTrue(4) && BenchTrue(5) && BenchTrue(6) && BenchTrue(7) && "
        "BenchTrue(8)");
    // Written expensive-last but false-first: what cheapest-first ordering would exploit
    const auto shortCircuit = compile("BenchFalse(1) && $bench_e > 10 && BenchTrue(2)");
    const auto lateFalse = compile("$bench_e > 10 && BenchTrue(2) && BenchFalse(1)");
    const auto mixed = compile("(BenchTrue(1) || BenchFalse(2)) && !BenchFalse(3) && BenchNum(4) > 10 && @BenchRule");
    const auto compareNamed = compile("$bench_e > 10");
    const auto literal = value("2 + 3 * 4");
    const auto named1 = value("$bench_a");
    const auto named5 = value("$bench_e");
    const auto typeRead = value("BenchNum(1)");

    std::printf("ConditionEngine benchmark (out of game; synthetic types, ns per call)\n\n");
    std::printf("Conditions\n");
    Row("Evaluate: 1 leaf", Time([&] { g_sink = leaf->Evaluate(context); }));
    Row("EvaluateAvailable: 1 leaf (availability + evaluate)", Time([&] { g_sink = leaf->EvaluateAvailable(context); }));
    Row("Evaluate: And of 8 leaves", Time([&] { g_sink = and8->Evaluate(context); }));
    Row("EvaluateAvailable: And of 8 leaves", Time([&] { g_sink = and8->EvaluateAvailable(context); }));
    Row("Availability::Check alone: And of 8 leaves", Time([&] { g_sink = Availability::Get().Check(*and8).Ready(); }));
    Row("Evaluate: false first, then 5-deep $value compare", Time([&] { g_sink = shortCircuit->Evaluate(context); }));
    Row("Evaluate: 5-deep $value compare first, false last", Time([&] { g_sink = lateFalse->Evaluate(context); }));
    Row("Evaluate: mixed (or, not, compare, @rule)", Time([&] { g_sink = mixed->Evaluate(context); }));
    Row("EvaluateAvailable: mixed", Time([&] { g_sink = mixed->EvaluateAvailable(context); }));
    Row("Evaluate: $bench_e > 10", Time([&] { g_sink = compareNamed->Evaluate(context); }));

    std::printf("\nValues\n");
    Row("Read: 2 + 3 * 4 (literals)", Time([&] { g_sink = literal->Read(context).value; }));
    Row("Read: BenchNum(1) (type read)", Time([&] { g_sink = typeRead->Read(context).value; }));
    Row("Read: $bench_a (1 named value)", Time([&] { g_sink = named1->Read(context).value; }));
    {
        // A pushed value, as NTJH's stress scene reads it
        PushedValues::Get().Set(nullptr, "bench_pushed", 3.0);
        auto pushedValue = value("$bench_pushed");
        Row("Read: $bench_pushed (a pushed value)", Time([&] { g_sink = pushedValue->Read(context).value; }));
        Stats::SetEnabled(true);
        Row("Read: $bench_pushed, stats on", Time([&] { g_sink = pushedValue->ReadCounted(context).value; }));
        Stats::SetEnabled(false);
        PushedValues::Get().ClearAll();
    }
    Row("Read: $bench_e (5 named values deep)", Time([&] { g_sink = named5->Read(context).value; }));
    Row("ReadValueAvailable: $bench_e", Time([&] { g_sink = ReadValueAvailable(*named5, context).value; }));
    Row("Availability::CheckValue alone: $bench_e", Time([&] { g_sink = Availability::Get().CheckValue(*named5).Ready(); }));

    std::printf("\nCompile\n");
    Row("CompileAny: mixed expression", Time([&] { g_sink = Expression::CompileAny(
        "(BenchTrue(1) || BenchFalse(2)) && !BenchFalse(3) && BenchNum(4) > 10 && @BenchRule", &error) != nullptr; },
        std::chrono::milliseconds(200)));

    std::printf("\nStats overhead (per-type timing on)\n");
    Stats::SetEnabled(true);
    Row("Evaluate: And of 8 leaves, stats on", Time([&] { g_sink = and8->EvaluateCounted(context); }));
    Row("Read: $bench_e, stats on", Time([&] { g_sink = named5->ReadCounted(context).value; }));
    Stats::SetEnabled(false);

    std::printf("\n");
    return 0;
}
