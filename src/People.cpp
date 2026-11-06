#include "ConditionEngine/People.h"
#include "ConditionEngine/Availability.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Forms.h"
#include "ConditionEngine/Presets.h"
#include "ConditionEngine/Registry.h"

#include <format>
#include <fstream>
#include <map>
#include <shared_mutex>

namespace ConditionEngine::People {

    namespace {
        using json = nlohmann::json;

        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        std::string Trim(std::string s) {
            const auto first = s.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return {};
            return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
        }

        // "Jarls & Courts" -> "jarls_courts"
        std::string GroupId(const std::string& text) {
            std::string id;
            for (const unsigned char c : Trim(text)) {
                if (std::isalnum(c)) id += static_cast<char>(std::tolower(c));
                else if (!id.empty() && id.back() != '_') id += '_';
            }
            while (!id.empty() && id.back() == '_') id.pop_back();
            return id;
        }

        std::string LabelFromId(const std::string& id) {
            std::string label;
            bool upper = true;
            for (const char c : id) {
                if (c == '_') {
                    label += ' ';
                    upper = true;
                } else {
                    label += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
                    upper = false;
                }
            }
            return label;
        }

        struct Entry {
            Group group;
            std::vector<std::string> removed;  // members taken out (the player's list), any source
        };

        // Members resolved to forms; rebuilt when groups change or plugins load
        struct Resolved {
            std::string id;
            std::vector<RE::TESFaction*> factions;
            std::vector<RE::TESNPC*> npcs;
            std::vector<RE::Actor*> refs;
            std::vector<RE::BGSKeyword*> keywords;
            bool teammates = false;
            bool essential = false;
        };

        std::shared_mutex g_mutex;
        std::map<std::string, Entry> g_groups;
        std::uint64_t g_version = 1;

        std::vector<Resolved> g_resolved;  // display order
        std::uint64_t g_resolvedVersion = 0;
        std::uint64_t g_resolvedRevision = 0;

        // The player's own additions and removals ({"group": {"members": [], "remove": []}})
        json g_user = json::object();

        bool Contains(const std::vector<std::string>& list, const std::string& value) {
            const auto lower = Lower(value);
            return std::any_of(list.begin(), list.end(), [&](const std::string& v) { return Lower(v) == lower; });
        }

        bool Erase(std::vector<std::string>& list, const std::string& value) {
            const auto lower = Lower(value);
            const auto before = list.size();
            std::erase_if(list, [&](const std::string& v) { return Lower(v) == lower; });
            return list.size() != before;
        }

        std::vector<std::string> ReadMembers(const json& j, std::initializer_list<const char*> keys) {
            std::vector<std::string> out;
            for (const char* key : keys) {
                if (!j.contains(key)) continue;
                for (auto& m : ReadStringList(j, key)) {
                    m = Trim(m);
                    if (!m.empty()) out.push_back(std::move(m));
                }
            }
            return out;
        }

        Entry& EntryFor(const std::string& id) {
            auto& entry = g_groups[id];
            if (entry.group.id.empty()) {
                entry.group.id = id;
                entry.group.label = LabelFromId(id);
            }
            return entry;
        }

        std::vector<const Entry*> Ordered() {
            std::vector<const Entry*> out;
            for (const auto& [_, e] : g_groups) out.push_back(&e);
            std::stable_sort(out.begin(), out.end(), [](const Entry* a, const Entry* b) {
                if (a->group.order != b->group.order) return a->group.order < b->group.order;
                return Lower(a->group.label) < Lower(b->group.label);
            });
            return out;
        }

        template <class T>
        void AddUnique(std::vector<T*>& list, T* form) {
            if (form && std::find(list.begin(), list.end(), form) == list.end()) list.push_back(form);
        }

        // Sorts one form into the lists. Form lists are opened one level.
        void Classify(RE::TESForm* form, Resolved& r, int depth = 0) {
            if (!form) return;
            if (auto* faction = form->As<RE::TESFaction>()) return AddUnique(r.factions, faction);
            if (auto* npc = form->As<RE::TESNPC>()) return AddUnique(r.npcs, npc);
            if (auto* actor = form->As<RE::Actor>()) return AddUnique(r.refs, actor);
            if (auto* keyword = form->As<RE::BGSKeyword>()) return AddUnique(r.keywords, keyword);
            if (auto* list = form->As<RE::BGSListForm>(); list && depth == 0) {
                for (auto* item : list->forms) Classify(item, r, depth + 1);
                return;
            }
            SKSE::log::warn("ConditionEngine: people: {:08X} is not an NPC, actor, faction, keyword or form list",
                            form->GetFormID());
        }

        bool IsRemoved(RE::TESForm* form, const std::vector<RE::TESForm*>& removed) {
            return form && std::find(removed.begin(), removed.end(), form) != removed.end();
        }

        // With g_mutex held exclusively
        void Resolve() {
            g_resolved.clear();
            for (const auto* e : Ordered()) {
                Resolved r;
                r.id = e->group.id;
                r.teammates = e->group.teammates;
                r.essential = e->group.essential;
                for (const auto& member : e->group.members) {
                    if (auto* form = Forms::LookupAny(member)) Classify(form, r);
                }
                if (!e->removed.empty()) {
                    std::vector<RE::TESForm*> removed;
                    for (const auto& member : e->removed) {
                        if (auto* form = Forms::LookupAny(member)) removed.push_back(form);
                    }
                    std::erase_if(r.factions, [&](auto* f) { return IsRemoved(f, removed); });
                    std::erase_if(r.npcs, [&](auto* f) { return IsRemoved(f, removed); });
                    std::erase_if(r.refs, [&](auto* f) { return IsRemoved(f, removed); });
                    std::erase_if(r.keywords, [&](auto* f) { return IsRemoved(f, removed); });
                }
                g_resolved.push_back(std::move(r));
            }
            g_resolvedVersion = g_version;
            g_resolvedRevision = Availability::Get().Revision();
        }

        bool Matches(const Resolved& r, RE::Actor* actor) {
            if (r.teammates && actor->IsPlayerTeammate()) return true;
            if (r.essential && actor->IsEssential()) return true;
            if (std::find(r.refs.begin(), r.refs.end(), actor) != r.refs.end()) return true;
            if (!r.npcs.empty()) {
                auto* base = actor->GetActorBase();
                auto* root = base ? base->GetRootFaceNPC() : nullptr;  // leveled / templated NPCs
                for (auto* npc : r.npcs) {
                    if (npc == base || npc == root) return true;
                }
            }
            for (auto* faction : r.factions) {
                if (actor->GetFactionRank(faction, actor->IsPlayerRef()) >= 0) return true;
            }
            for (auto* keyword : r.keywords) {
                if (actor->HasKeyword(keyword)) return true;
            }
            return false;
        }

        // Calls fn(resolved) for each group in display order under a shared lock, resolving first when stale
        template <class Fn>
        void ForEachResolved(Fn&& fn) {
            const auto revision = Availability::Get().Revision();
            {
                std::shared_lock lock(g_mutex);
                if (g_resolvedVersion == g_version && g_resolvedRevision == revision) {
                    for (const auto& r : g_resolved) {
                        if (!fn(r)) return;
                    }
                    return;
                }
            }
            std::unique_lock lock(g_mutex);
            if (g_resolvedVersion != g_version || g_resolvedRevision != revision) Resolve();
            for (const auto& r : g_resolved) {
                if (!fn(r)) return;
            }
        }

        bool WriteUser(std::string* error) {
            json people = json::object();
            {
                std::shared_lock lock(g_mutex);
                people = g_user;
            }
            json file = {{"_comment", "Your important people: added with \"ce people add\" or by mods for you. "
                                      "Safe to edit by hand (docs/people.md)."},
                         {"people", people}};
            const auto path = UserFile();
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            const auto temp = path.string() + ".tmp";
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                if (!out) {
                    if (error) *error = "cannot write " + path.string();
                    return false;
                }
                out << file.dump(2);
            }
            std::filesystem::rename(temp, path, ec);
            if (ec) {
                if (error) *error = "cannot replace " + path.string() + ": " + ec.message();
                return false;
            }
            return true;
        }

        // ── The condition ──────────────────────────────────────────────────
        class ImportantPersonCondition : public Condition {
        public:
            ImportantPersonCondition(std::vector<std::string> groups, bool includePlayer)
                : groups_(std::move(groups)), includePlayer_(includePlayer) {
                for (auto& g : groups_) g = GroupId(g);
                std::erase_if(groups_, [](const std::string& g) { return g.empty(); });
            }

            bool Evaluate(const EvaluationContext& context) const override {
                auto* actor = context.GetActor();
                return actor && !MatchGroup(actor, groups_, includePlayer_).empty();
            }

            std::string GetDescription() const override {
                if (groups_.empty()) return "Is an important person";
                std::string list;
                for (const auto& g : groups_) {
                    if (!list.empty()) list += ", ";
                    auto group = FindGroup(g);
                    list += group ? group->label : g;
                }
                return "Is an important person (" + list + ")";
            }

            json ToJson() const override {
                json j{{"type", "ImportantPerson"}};
                if (!groups_.empty()) j["groups"] = groups_;
                if (includePlayer_) j["includePlayer"] = true;
                return j;
            }

            std::unique_ptr<Condition> Clone() const override {
                return std::make_unique<ImportantPersonCondition>(groups_, includePlayer_);
            }

            EventDependency GetEventDependencies() const override {
                return EventDependency::Faction | EventDependency::ActorState;
            }

            std::vector<std::string> Validate() const override {
                std::vector<std::string> problems;
                if (ListGroups().empty()) return problems;  // groups not read yet
                for (const auto& g : groups_) {
                    if (!FindGroup(g)) problems.push_back("ImportantPerson: no people group '" + g + "'");
                }
                return problems;
            }

        private:
            std::vector<std::string> groups_;
            bool includePlayer_;
        };
    }

    json Group::ToJson() const {
        json j{{"id", id}, {"label", label}, {"order", order}, {"members", members}, {"sources", sources}};
        if (!description.empty()) j["description"] = description;
        if (!icon.empty()) j["icon"] = icon;
        if (teammates) j["teammates"] = true;
        if (essential) j["essential"] = true;
        return j;
    }

    std::size_t AddFromJson(const json& section, const std::string& source, std::vector<std::string>* warnings) {
        if (!section.is_object()) {
            if (warnings) warnings->push_back("\"people\" must be an object of groups");
            return 0;
        }
        std::size_t added = 0;
        std::unique_lock lock(g_mutex);
        for (const auto& [name, def] : section.items()) {
            if (name.starts_with('_')) continue;
            const auto id = GroupId(name);
            if (id.empty()) {
                if (warnings) warnings->push_back("people group '" + name + "' has no usable id");
                continue;
            }
            auto& entry = EntryFor(id);
            auto& g = entry.group;
            if (!Contains(g.sources, source)) g.sources.push_back(source);

            // A bare list is the members
            if (def.is_array() || def.is_string()) {
                for (auto& m : ReadMembers(json{{"members", def}}, {"members"})) {
                    if (!Contains(g.members, m)) {
                        g.members.push_back(std::move(m));
                        ++added;
                    }
                }
                continue;
            }
            if (!def.is_object()) {
                if (warnings) warnings->push_back("people group '" + name + "' must be an object or a list");
                continue;
            }
            // The first file to label a group names it; later files only fill gaps
            const bool labelled = g.label != LabelFromId(id);
            if (def.contains("label") && def["label"].is_string() && !labelled) g.label = def["label"].get<std::string>();
            if (def.contains("description") && def["description"].is_string() && g.description.empty()) {
                g.description = def["description"].get<std::string>();
            }
            if (def.contains("icon") && def["icon"].is_string() && g.icon.empty()) g.icon = def["icon"].get<std::string>();
            if (def.contains("order") && def["order"].is_number_integer() && !labelled) g.order = def["order"].get<int>();
            if (def.value("teammates", false)) g.teammates = true;
            if (def.value("essential", false)) g.essential = true;
            for (auto& m : ReadMembers(def, {"members", "npcs", "actors", "factions", "keywords"})) {
                if (!Contains(g.members, m)) {
                    g.members.push_back(std::move(m));
                    ++added;
                }
            }
            for (auto& m : ReadMembers(def, {"remove"})) {
                if (!Contains(entry.removed, m)) entry.removed.push_back(std::move(m));
            }
        }
        ++g_version;
        return added;
    }

    bool AddMember(const std::string& group, const std::string& member, const std::string& source) {
        const auto id = GroupId(group.empty() ? kUserGroup : group);
        const auto text = Trim(member);
        if (id.empty() || text.empty()) return false;
        std::unique_lock lock(g_mutex);
        auto& entry = EntryFor(id);
        const bool wasRemoved = Erase(entry.removed, text);
        if (!Contains(entry.group.sources, source)) entry.group.sources.push_back(source);
        if (Contains(entry.group.members, text) && !wasRemoved) return false;
        if (!Contains(entry.group.members, text)) entry.group.members.push_back(text);
        ++g_version;
        return true;
    }

    bool RemoveMember(const std::string& group, const std::string& member) {
        const auto id = GroupId(group);
        std::unique_lock lock(g_mutex);
        auto it = g_groups.find(id);
        if (it == g_groups.end() || !Erase(it->second.group.members, Trim(member))) return false;
        ++g_version;
        return true;
    }

    std::string MemberString(RE::TESForm* form) {
        if (!form) return {};
        if (auto* actor = form->As<RE::Actor>()) {
            auto* base = actor->GetActorBase();
            if (base && base->IsDynamicForm()) base = base->GetRootFaceNPC();
            form = base;
        }
        if (!form || form->IsDynamicForm()) return {};
        const auto* file = form->GetFile(0);
        if (!file) return {};
        const auto local = file->IsLight() ? (form->GetFormID() & 0xFFF) : (form->GetFormID() & 0xFFFFFF);
        return std::format("0x{:X}|{}", local, file->GetFilename());
    }

    std::vector<Group> ListGroups() {
        std::shared_lock lock(g_mutex);
        std::vector<Group> out;
        for (const auto* e : Ordered()) out.push_back(e->group);
        return out;
    }

    std::optional<Group> FindGroup(const std::string& id) {
        std::shared_lock lock(g_mutex);
        auto it = g_groups.find(GroupId(id));
        if (it == g_groups.end()) return std::nullopt;
        return it->second.group;
    }

    std::string MatchGroup(RE::Actor* actor, const std::vector<std::string>& wanted, bool includePlayer) {
        if (!actor || (!includePlayer && actor->IsPlayerRef())) return {};
        std::vector<std::string> groups;
        for (const auto& g : wanted) {
            if (auto id = GroupId(g); !id.empty()) groups.push_back(std::move(id));
        }
        std::string found;
        ForEachResolved([&](const Resolved& r) {
            if (!groups.empty() && std::find(groups.begin(), groups.end(), r.id) == groups.end()) return true;
            if (!Matches(r, actor)) return true;
            found = r.id;
            return false;
        });
        return found;
    }

    std::vector<std::string> GroupsOf(RE::Actor* actor, bool includePlayer) {
        std::vector<std::string> out;
        if (!actor || (!includePlayer && actor->IsPlayerRef())) return out;
        ForEachResolved([&](const Resolved& r) {
            if (Matches(r, actor)) out.push_back(r.id);
            return true;
        });
        return out;
    }

    void Clear() {
        std::unique_lock lock(g_mutex);
        g_groups.clear();
        g_resolved.clear();
        g_user = json::object();
        ++g_version;
    }

    std::filesystem::path UserFile() {
        return GetDefaultPresetDirectory() / "User" / "People.json";
    }

    std::size_t LoadUserFile() {
        const auto path = UserFile();
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return 0;
        json root;
        try {
            std::ifstream in(path);
            root = json::parse(in, nullptr, true, /*ignore_comments=*/true);
        } catch (const std::exception& e) {
            SKSE::log::error("ConditionEngine: cannot read {}: {}", path.string(), e.what());
            return 0;
        }
        if (!root.is_object() || !root.contains("people") || !root["people"].is_object()) return 0;
        std::vector<std::string> warnings;
        const auto count = AddFromJson(root["people"], kUserSource, &warnings);
        for (const auto& w : warnings) SKSE::log::warn("ConditionEngine: User/People.json: {}", w);
        std::unique_lock lock(g_mutex);
        g_user = root["people"];
        return count;
    }

    bool AddUserMember(const std::string& group, const std::string& member, std::string* error) {
        const auto id = GroupId(group.empty() ? kUserGroup : group);
        const auto text = Trim(member);
        if (id.empty() || text.empty()) {
            if (error) *error = "nothing to add";
            return false;
        }
        if (!AddMember(id, text, kUserSource)) {
            if (error) *error = "already in '" + id + "'";
            return false;
        }
        {
            std::unique_lock lock(g_mutex);
            auto& g = g_user[id];
            if (!g.is_object()) g = json::object();
            auto members = g.contains("members") ? ReadStringList(g, "members") : std::vector<std::string>{};
            if (!Contains(members, text)) members.push_back(text);
            g["members"] = members;
            if (g.contains("remove")) {
                auto removed = ReadStringList(g, "remove");
                Erase(removed, text);
                if (removed.empty()) g.erase("remove");
                else g["remove"] = removed;
            }
        }
        return WriteUser(error);
    }

    bool RemoveUserMember(const std::string& group, const std::string& member, std::string* error) {
        const auto id = GroupId(group.empty() ? kUserGroup : group);
        const auto text = Trim(member);
        bool changed = false;
        {
            std::unique_lock lock(g_mutex);
            auto it = g_groups.find(id);
            if (it == g_groups.end()) {
                if (error) *error = "no group '" + id + "'";
                return false;
            }
            auto& g = g_user[id];
            if (!g.is_object()) g = json::object();
            // Our own addition: forget it
            if (g.contains("members")) {
                auto members = ReadStringList(g, "members");
                if (Erase(members, text)) {
                    Erase(it->second.group.members, text);
                    g["members"] = members;
                    changed = true;
                }
            }
            // Still listed by a preset or mod: take it out for this player
            if (Contains(it->second.group.members, text) && !Contains(it->second.removed, text)) {
                it->second.removed.push_back(text);
                auto removed = g.contains("remove") ? ReadStringList(g, "remove") : std::vector<std::string>{};
                removed.push_back(text);
                g["remove"] = removed;
                changed = true;
            }
            if (g.empty()) g_user.erase(id);
            if (changed) ++g_version;
        }
        if (!changed) {
            if (error) *error = "not in '" + id + "'";
            return false;
        }
        return WriteUser(error);
    }

    void RegisterTypes(Registry& r) {
        using namespace Field;
        r.RegisterCategory({"people", "Important People", 45, {}, "👑",
                            "Jarls, leaders, guild and quest people, your followers, spouse and household, "
                            "and anyone you or your mods add"});

        auto options = [] {
            std::vector<FieldOption> out;
            for (const auto& g : ListGroups()) out.push_back({g.id, g.label, g.description});
            return out;
        };
        TypeBuilder("ImportantPerson", "Important Person", "people")
            .Aliases({"IsImportant", "Important", "ImportantPeople"})
            .Icon("👑")
            .Describe("Actor is one of the important people: in any of the selected groups (none selected = any group)")
            .Fields({EnumList("groups", "Groups", {})
                         .OptionsFrom(options)
                         .Custom()
                         .Describe("None selected = any group. Presets and mods can add groups (docs/people.md)"),
                     Bool("includePlayer", "Count the player too", false)})
            .Positional({"groups"})
            .Factory([](const json& j) {
                std::vector<std::string> groups;
                if (j.contains("groups")) groups = ReadStringList(j, "groups");
                else if (j.contains("group")) groups = ReadStringList(j, "group");
                return std::make_unique<ImportantPersonCondition>(std::move(groups), j.value("includePlayer", false));
            })
            .Register(r);
        r.SetTypeCost("ImportantPerson", 2);
    }

} // namespace ConditionEngine::People
