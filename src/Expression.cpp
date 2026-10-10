#include "ConditionEngine/Expression.h"
#include "ConditionEngine/Registry.h"

#include <cctype>
#include <charconv>

namespace ConditionEngine::Expression {

    namespace {
        using json = nlohmann::json;

        // ── Lexer ────────────────────────────────────────────────────────────

        enum class Tok {
            End, Ident, Number, String, Param,
            LParen, RParen, LBracket, RBracket, Comma, Assign, At,
            Not, And, Or,
            Eq, Ne, Lt, Le, Gt, Ge,
        };

        struct Token {
            Tok type = Tok::End;
            std::string text;      // identifier / string contents / number text / param text
            std::size_t pos = 0;
        };

        struct SyntaxError {
            std::string message;
            std::size_t pos;
        };

        bool IEquals(std::string_view a, std::string_view b) {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
                    return false;
                }
            }
            return true;
        }

        bool IsIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
        bool IsIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

        std::vector<Token> Lex(std::string_view src) {
            std::vector<Token> tokens;
            std::size_t i = 0;
            auto push = [&](Tok t, std::size_t start, std::string text = {}) {
                tokens.push_back(Token{t, std::move(text), start});
            };

            while (i < src.size()) {
                const char c = src[i];
                if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
                const std::size_t start = i;
                auto peek = [&](std::size_t off) { return i + off < src.size() ? src[i + off] : '\0'; };

                if (IsIdentStart(c)) {
                    while (i < src.size() && IsIdentChar(src[i])) ++i;
                    std::string word(src.substr(start, i - start));
                    if (IEquals(word, "and")) push(Tok::And, start);
                    else if (IEquals(word, "or")) push(Tok::Or, start);
                    else if (IEquals(word, "not")) push(Tok::Not, start);
                    else push(Tok::Ident, start, std::move(word));
                    continue;
                }

                if (std::isdigit(static_cast<unsigned char>(c)) ||
                    ((c == '-' || c == '+' || c == '.') && std::isdigit(static_cast<unsigned char>(peek(1))))) {
                    ++i;
                    if (c == '0' && (peek(0) == 'x' || peek(0) == 'X')) {
                        ++i;
                        while (i < src.size() && std::isxdigit(static_cast<unsigned char>(src[i]))) ++i;
                    } else {
                        while (i < src.size() && (std::isdigit(static_cast<unsigned char>(src[i])) || src[i] == '.')) ++i;
                    }
                    push(Tok::Number, start, std::string(src.substr(start, i - start)));
                    continue;
                }

                if (c == '"' || c == '\'') {
                    const char quote = c;
                    ++i;
                    std::string value;
                    bool closed = false;
                    while (i < src.size()) {
                        char ch = src[i++];
                        if (ch == quote) { closed = true; break; }
                        if (ch == '\\' && i < src.size()) {
                            char esc = src[i++];
                            switch (esc) {
                                case 'n': value += '\n'; break;
                                case 't': value += '\t'; break;
                                default: value += esc; break;
                            }
                            continue;
                        }
                        value += ch;
                    }
                    if (!closed) throw SyntaxError{"Unterminated string", start};
                    push(Tok::String, start, std::move(value));
                    continue;
                }

                if (c == '$' && peek(1) == '{') {
                    auto close = src.find('}', i);
                    if (close == std::string_view::npos) throw SyntaxError{"Unterminated ${parameter}", start};
                    i = close + 1;
                    push(Tok::Param, start, std::string(src.substr(start, i - start)));
                    continue;
                }

                auto two = [&](char a, char b) { return c == a && peek(1) == b; };
                if (two('&', '&')) { push(Tok::And, start); i += 2; continue; }
                if (two('|', '|')) { push(Tok::Or, start); i += 2; continue; }
                if (two('=', '=')) { push(Tok::Eq, start); i += 2; continue; }
                if (two('!', '=')) { push(Tok::Ne, start); i += 2; continue; }
                if (two('<', '=')) { push(Tok::Le, start); i += 2; continue; }
                if (two('>', '=')) { push(Tok::Ge, start); i += 2; continue; }

                switch (c) {
                    case '(': push(Tok::LParen, start); break;
                    case ')': push(Tok::RParen, start); break;
                    case '[': push(Tok::LBracket, start); break;
                    case ']': push(Tok::RBracket, start); break;
                    case ',': push(Tok::Comma, start); break;
                    case '=': push(Tok::Assign, start); break;
                    case '@': push(Tok::At, start); break;
                    case '!': push(Tok::Not, start); break;
                    case '<': push(Tok::Lt, start); break;
                    case '>': push(Tok::Gt, start); break;
                    default:
                        throw SyntaxError{std::string("Unexpected character '") + c + "'", start};
                }
                ++i;
            }
            tokens.push_back(Token{Tok::End, {}, src.size()});
            return tokens;
        }

        // ── Parser ───────────────────────────────────────────────────────────

        class Parser {
        public:
            explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

            json ParseAll() {
                json result = ParseOr();
                if (Peek().type != Tok::End) throw SyntaxError{"Unexpected input", Peek().pos};
                return result;
            }

        private:
            const Token& Peek() const { return tokens_[index_]; }
            const Token& Next() { return tokens_[index_++]; }
            bool Accept(Tok t) {
                if (Peek().type != t) return false;
                ++index_;
                return true;
            }
            const Token& Expect(Tok t, const char* what) {
                if (Peek().type != t) throw SyntaxError{std::string("Expected ") + what, Peek().pos};
                return Next();
            }

            static json Combine(const char* op, std::vector<json> children) {
                if (children.size() == 1) return std::move(children.front());
                return json{{"type", "Logical"}, {"operator", op}, {"children", std::move(children)}};
            }

            json ParseOr() {
                std::vector<json> children{ParseAnd()};
                while (Accept(Tok::Or)) children.push_back(ParseAnd());
                return Combine("Or", std::move(children));
            }

            json ParseAnd() {
                std::vector<json> children{ParseNot()};
                while (Accept(Tok::And)) children.push_back(ParseNot());
                return Combine("And", std::move(children));
            }

            json ParseNot() {
                if (Accept(Tok::Not)) {
                    return json{{"type", "Logical"}, {"operator", "Not"}, {"children", json::array({ParseNot()})}};
                }
                return ParsePrimary();
            }

            json ParsePrimary() {
                const Token& tok = Peek();
                if (Accept(Tok::LParen)) {
                    json inner = ParseOr();
                    Expect(Tok::RParen, "')'");
                    return inner;
                }
                if (Accept(Tok::At)) {
                    const Token& name = Next();
                    if (name.type != Tok::Ident && name.type != Tok::String) {
                        throw SyntaxError{"Expected a name after '@'", name.pos};
                    }
                    return json{{"type", "Rule"}, {"ruleName", name.text}};
                }
                if (tok.type == Tok::Ident) {
                    if (IEquals(tok.text, "true") || IEquals(tok.text, "false")) {
                        Next();
                        return json{{"type", "Constant"}, {"value", IEquals(tok.text, "true")}};
                    }
                    return ParseCall();
                }
                throw SyntaxError{"Expected a condition", tok.pos};
            }

            json ParseCall() {
                const Token& nameTok = Next();
                const TypeInfo* info = Registry::Get().FindType(nameTok.text);
                if (!info) {
                    throw SyntaxError{"Unknown condition type '" + nameTok.text +
                                          "' (use @" + nameTok.text + " for a named condition)",
                                      nameTok.pos};
                }

                json result{{"type", info->name}};

                if (Accept(Tok::LParen)) {
                    std::size_t positional = 0;
                    if (!Accept(Tok::RParen)) {
                        do {
                            // field=value
                            if (Peek().type == Tok::Ident && tokens_[index_ + 1].type == Tok::Assign) {
                                std::string field = Next().text;
                                Next();
                                result[field] = ParseValue();
                                continue;
                            }
                            const std::size_t argPos = Peek().pos;
                            if (positional >= info->positional.size()) {
                                throw SyntaxError{info->name + " takes at most " +
                                                      std::to_string(info->positional.size()) +
                                                      " positional argument(s)",
                                                  argPos};
                            }
                            result[info->positional[positional++]] = ParseValue();
                        } while (Accept(Tok::Comma));
                        Expect(Tok::RParen, "')' or ','");
                    }
                }

                // Optional trailing comparison
                static const std::unordered_map<Tok, const char*> kOps = {
                    {Tok::Eq, "=="}, {Tok::Ne, "!="}, {Tok::Lt, "<"}, {Tok::Le, "<="}, {Tok::Gt, ">"}, {Tok::Ge, ">="},
                };
                auto opIt = kOps.find(Peek().type);
                if (opIt != kOps.end()) {
                    const std::size_t opPos = Next().pos;
                    if (info->compareField.empty()) {
                        throw SyntaxError{info->name + " does not support comparisons", opPos};
                    }
                    result[info->compareField] = ParseValue();
                    result.erase("operation");
                    result["operator"] = opIt->second;
                }
                return result;
            }

            json ParseValue() {
                const Token& tok = Next();
                switch (tok.type) {
                    case Tok::String:
                    case Tok::Param:
                        return tok.text;
                    case Tok::Ident:
                        if (IEquals(tok.text, "true")) return true;
                        if (IEquals(tok.text, "false")) return false;
                        return tok.text;
                    case Tok::Number: {
                        const std::string& t = tok.text;
                        try {
                            if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
                                return static_cast<std::int64_t>(std::stoll(t, nullptr, 16));
                            }
                            if (t.find('.') != std::string::npos) return std::stod(t);
                            return static_cast<std::int64_t>(std::stoll(t));
                        } catch (...) {
                            throw SyntaxError{"Invalid number '" + t + "'", tok.pos};
                        }
                    }
                    case Tok::LBracket: {
                        json list = json::array();
                        if (!Accept(Tok::RBracket)) {
                            do { list.push_back(ParseValue()); } while (Accept(Tok::Comma));
                            Expect(Tok::RBracket, "']' or ','");
                        }
                        return list;
                    }
                    default:
                        throw SyntaxError{"Expected a value", tok.pos};
                }
            }

            std::vector<Token> tokens_;
            std::size_t index_ = 0;
        };

        // ── Printer ──────────────────────────────────────────────────────────

        bool IsBareWord(const std::string& s) {
            if (s.empty() || !IsIdentStart(s[0])) return false;
            if (!std::all_of(s.begin(), s.end(), IsIdentChar)) return false;
            for (const char* kw : {"true", "false", "and", "or", "not"}) {
                if (IEquals(s, kw)) return false;
            }
            return true;
        }

        std::string Quote(const std::string& s) {
            std::string out = "\"";
            for (char c : s) {
                if (c == '"' || c == '\\') out += '\\';
                if (c == '\n') { out += "\\n"; continue; }
                out += c;
            }
            return out + '"';
        }

        std::string PrintValue(const json& v) {
            if (v.is_string()) {
                const auto& s = v.get_ref<const std::string&>();
                if (s.starts_with("${") && s.ends_with("}") && s.find('}') == s.size() - 1) return s;
                return IsBareWord(s) ? s : Quote(s);
            }
            if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
            if (v.is_number()) return v.dump();
            if (v.is_array()) {
                std::string out = "[";
                bool first = true;
                for (const auto& item : v) {
                    if (!first) out += ", ";
                    out += PrintValue(item);
                    first = false;
                }
                return out + "]";
            }
            if (v.is_null()) return "\"\"";
            return Quote(v.dump());
        }

        std::string LogicalOpOf(const json& j) {
            for (const char* key : {"operator", "operation"}) {
                if (!j.contains(key)) continue;
                const auto& v = j[key];
                if (v.is_string()) {
                    auto s = v.get<std::string>();
                    if (IEquals(s, "or") || s == "||") return "Or";
                    if (IEquals(s, "not") || s == "!") return "Not";
                    return "And";
                }
                if (v.is_number()) {
                    switch (v.get<int>()) {
                        case 1: return "Or";
                        case 2: return "Not";
                        default: return "And";
                    }
                }
            }
            return "And";
        }

        bool IsLogical(const json& j) {
            if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) return false;
            const auto* info = Registry::Get().FindType(j["type"].get<std::string>());
            return info && info->name == "Logical";
        }

        std::string Print(const json& j);

        std::string PrintChild(const json& child, const std::string& parentOp) {
            std::string text = Print(child);
            if (IsLogical(child)) {
                const auto childOp = LogicalOpOf(child);
                const bool needsParens = parentOp == "Not" ? childOp != "Not"
                                                           : (childOp != "Not" && childOp != parentOp);
                if (needsParens && child.value("children", json::array()).size() > 1) return "(" + text + ")";
            }
            return text;
        }

        std::string Print(const json& j) {
            if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) return "false";

            const std::string rawType = j["type"].get<std::string>();
            const TypeInfo* info = Registry::Get().FindType(rawType);
            const std::string type = info ? info->name : rawType;

            if (type == "Logical") {
                const auto op = LogicalOpOf(j);
                const auto& children = j.contains("children") && j["children"].is_array() ? j["children"] : json::array();
                if (op == "Not") {
                    return children.empty() ? "!true" : "!" + PrintChild(children.front(), op);
                }
                if (children.empty()) return op == "And" ? "true" : "false";
                std::string out;
                for (const auto& child : children) {
                    if (!out.empty()) out += op == "And" ? " && " : " || ";
                    out += PrintChild(child, op);
                }
                return out;
            }

            if (type == "Constant") return j.value("value", true) ? "true" : "false";

            if (type == "Rule") {
                const std::string name = j.value("ruleName", "");
                const std::string uuid = j.value("ruleUUID", "");
                std::string out;
                if (uuid.empty() && IsBareWord(name)) {
                    out = "@" + name;
                } else {
                    out = "Rule(" + Quote(name);
                    if (!uuid.empty()) out += ", ruleUUID=" + Quote(uuid);
                    out += ")";
                }
                return j.value("inverted", false) ? "!" + out : out;
            }

            // Generic call: positional args, named args, trailing comparison
            json fields = j;
            fields.erase("type");

            std::string comparison;
            if (info && !info->compareField.empty() && fields.contains(info->compareField)) {
                const char* opKey = fields.contains("operator") ? "operator" : (fields.contains("operation") ? "operation" : nullptr);
                if (opKey) {
                    auto op = ParseComparisonOperator(fields, opKey);
                    comparison = " " + ComparisonOpToString(op) + " " + PrintValue(fields[info->compareField]);
                    fields.erase(info->compareField);
                    fields.erase(opKey);
                }
            }

            if (info) {
                for (const auto& field : info->derived) fields.erase(field);
            }
            // An Equal operator is the default when absent, so it is noise without a comparison
            for (const char* opKey : {"operator", "operation"}) {
                if (comparison.empty() && fields.contains(opKey) &&
                    ParseComparisonOperator(fields, opKey) == ComparisonOperator::Equal) {
                    fields.erase(opKey);
                }
            }

            std::vector<std::string> args;
            if (info) {
                for (const auto& field : info->positional) {
                    if (!fields.contains(field)) break;
                    args.push_back(PrintValue(fields[field]));
                    fields.erase(field);
                }
            }
            for (const auto& [key, value] : fields.items()) {
                args.push_back(key + "=" + PrintValue(value));
            }

            std::string out = type;
            if (!args.empty()) {
                out += "(";
                for (std::size_t i = 0; i < args.size(); ++i) {
                    if (i > 0) out += ", ";
                    out += args[i];
                }
                out += ")";
            }
            return out + comparison;
        }
    }

    std::optional<nlohmann::json> ParseToJson(std::string_view source, ParseError* error) {
        try {
            Parser parser(Lex(source));
            return parser.ParseAll();
        } catch (const SyntaxError& e) {
            if (error) *error = ParseError{e.message, e.pos};
            return std::nullopt;
        }
    }

    std::unique_ptr<Condition> Compile(std::string_view source, ParseError* error) {
        auto tree = ParseToJson(source, error);
        if (!tree) return nullptr;
        auto condition = Condition::FromJson(*tree);
        if (!condition && error) *error = ParseError{"Condition could not be built from the expression", 0};
        return condition;
    }

    std::unique_ptr<Condition> CompileAny(std::string_view source, std::string* error) {
        auto first = source.find_first_not_of(" \t\r\n");
        if (first != std::string_view::npos && source[first] == '{') {
            try {
                auto condition = Condition::FromJson(nlohmann::json::parse(source));
                if (!condition && error) *error = "JSON does not describe a known condition type";
                return condition;
            } catch (const std::exception& e) {
                if (error) *error = std::string("Invalid JSON: ") + e.what();
                return nullptr;
            }
        }

        ParseError parseError;
        auto condition = Compile(source, &parseError);
        if (!condition && error) {
            *error = parseError.message + " (at position " + std::to_string(parseError.position) + ")";
        }
        return condition;
    }

    std::string FromJson(const nlohmann::json& json) {
        return Print(json);
    }

    std::string ToExpression(const Condition& condition) {
        return Print(condition.ToJsonComplete());
    }

} // namespace ConditionEngine::Expression
