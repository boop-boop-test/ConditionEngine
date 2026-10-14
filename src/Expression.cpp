#include "ConditionEngine/Expression.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Values/ValueNodes.h"

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
            Plus, Minus, Star, Slash,
            Var,                   // $name (text = name)
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

                // Signs are separate tokens (binary/unary minus); ParseValue folds them into literals
                if (std::isdigit(static_cast<unsigned char>(c)) ||
                    (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1))))) {
                    ++i;
                    if (c == '0' && (peek(0) == 'x' || peek(0) == 'X')) {
                        ++i;
                        while (i < src.size() && std::isxdigit(static_cast<unsigned char>(src[i]))) ++i;
                    } else {
                        while (i < src.size() && (std::isdigit(static_cast<unsigned char>(src[i])) || src[i] == '.')) ++i;
                        // Exponent (the printer writes 1e-07 for tiny numbers)
                        if (i < src.size() && (src[i] == 'e' || src[i] == 'E')) {
                            std::size_t j = i + 1;
                            if (j < src.size() && (src[j] == '+' || src[j] == '-')) ++j;
                            if (j < src.size() && std::isdigit(static_cast<unsigned char>(src[j]))) {
                                i = j;
                                while (i < src.size() && std::isdigit(static_cast<unsigned char>(src[i]))) ++i;
                            }
                        }
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

                if (c == '$') {
                    if (!IsIdentStart(peek(1))) throw SyntaxError{"Expected a value name after '$'", start};
                    i += 1;
                    const std::size_t nameStart = i;
                    while (i < src.size() && IsIdentChar(src[i])) ++i;
                    push(Tok::Var, start, std::string(src.substr(nameStart, i - nameStart)));
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
                    case '+': push(Tok::Plus, start); break;
                    case '-': push(Tok::Minus, start); break;
                    case '*': push(Tok::Star, start); break;
                    case '/': push(Tok::Slash, start); break;
                    default:
                        throw SyntaxError{std::string("Unexpected character '") + c + "'", start};
                }
                ++i;
            }
            tokens.push_back(Token{Tok::End, {}, src.size()});
            return tokens;
        }

        // ── Parser ───────────────────────────────────────────────────────────

        // A parsed operand. Calls stay undecided until their use: a numeric type read as a
        // number (value) or as its own condition.
        enum class Kind { Condition, Value, Call };

        struct Node {
            json j;
            Kind kind = Kind::Condition;
            const TypeInfo* info = nullptr;  // Call
        };

        constexpr const char* kFunctions[] = {"min", "max", "clamp", "abs", "lerp"};

        bool IsFunctionName(std::string_view name) {
            return std::any_of(std::begin(kFunctions), std::end(kFunctions), [&](const char* f) { return IEquals(name, f); });
        }

        json MakeCompare(json left, const char* op, json right) {
            return json{{"type", "Compare"}, {"left", std::move(left)}, {"operator", op}, {"right", std::move(right)}};
        }

        class Parser {
        public:
            explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

            json ParseAll(bool valueMode) {
                Node result = ParseOr();
                if (Peek().type != Tok::End) throw SyntaxError{"Unexpected input", Peek().pos};
                return valueMode ? AsValue(std::move(result)) : AsCondition(std::move(result));
            }

        private:
            const Token& Peek(std::size_t ahead = 0) const {
                return tokens_[std::min(index_ + ahead, tokens_.size() - 1)];
            }
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

            // A value used as a condition is true when non-zero
            static json AsCondition(Node node) {
                if (node.kind != Kind::Value) return std::move(node.j);
                return MakeCompare(std::move(node.j), "!=", 0);
            }

            // A condition used as a value reads 0 / 1; numeric calls read their number
            static json AsValue(Node node) {
                if (node.kind == Kind::Value) return std::move(node.j);
                if (node.kind == Kind::Call && node.info && node.info->value) return std::move(node.j);
                return json{{"type", "Truth"}, {"condition", std::move(node.j)}};
            }

            static Node ConditionNode(json j) { return Node{std::move(j), Kind::Condition, nullptr}; }
            static Node ValueNode(json j) { return Node{std::move(j), Kind::Value, nullptr}; }

            static json Combine(const char* op, std::vector<json> children) {
                if (children.size() == 1) return std::move(children.front());
                return json{{"type", "Logical"}, {"operator", op}, {"children", std::move(children)}};
            }

            Node ParseOr() {
                Node first = ParseAnd();
                if (Peek().type != Tok::Or) return first;
                std::vector<json> children{AsCondition(std::move(first))};
                while (Accept(Tok::Or)) children.push_back(AsCondition(ParseAnd()));
                return ConditionNode(Combine("Or", std::move(children)));
            }

            Node ParseAnd() {
                Node first = ParseNot();
                if (Peek().type != Tok::And) return first;
                std::vector<json> children{AsCondition(std::move(first))};
                while (Accept(Tok::And)) children.push_back(AsCondition(ParseNot()));
                return ConditionNode(Combine("And", std::move(children)));
            }

            Node ParseNot() {
                if (Accept(Tok::Not)) {
                    return ConditionNode(
                        json{{"type", "Logical"}, {"operator", "Not"}, {"children", json::array({AsCondition(ParseNot())})}});
                }
                return ParseComparison();
            }

            static const char* ComparisonSymbol(Tok t) {
                switch (t) {
                    case Tok::Eq: return "==";
                    case Tok::Ne: return "!=";
                    case Tok::Lt: return "<";
                    case Tok::Le: return "<=";
                    case Tok::Gt: return ">";
                    case Tok::Ge: return ">=";
                    default: return nullptr;
                }
            }

            static bool IsArithmetic(Tok t) {
                return t == Tok::Plus || t == Tok::Minus || t == Tok::Star || t == Tok::Slash;
            }

            // True when the tokens after a comparison are one plain literal, so the
            // classic `Type(...) op literal` form applies
            bool LiteralAhead() const {
                std::size_t after = 1;
                const Token& tok = Peek();
                switch (tok.type) {
                    case Tok::Number:
                    case Tok::String:
                    case Tok::Param:
                        break;
                    case Tok::LBracket:
                        return true;
                    case Tok::Minus:
                    case Tok::Plus:
                        if (Peek(1).type != Tok::Number) return false;
                        after = 2;
                        break;
                    case Tok::Ident:
                        if (IEquals(tok.text, "true") || IEquals(tok.text, "false")) break;
                        // A type or function is a value expression; any other word is a literal
                        if (Peek(1).type == Tok::LParen || Registry::Get().FindType(tok.text)) return false;
                        break;
                    default:
                        return false;
                }
                return !IsArithmetic(Peek(after).type);
            }

            Node ParseComparison() {
                Node left = ParseAdditive();
                const char* op = ComparisonSymbol(Peek().type);
                if (!op) return left;
                const std::size_t opPos = Next().pos;

                if (left.kind == Kind::Call) {
                    const TypeInfo* info = left.info;
                    if (!info->compareField.empty() && LiteralAhead()) {
                        json result = std::move(left.j);
                        result[info->compareField] = ParseValue();
                        result.erase("operation");
                        result["operator"] = op;
                        return ConditionNode(std::move(result));
                    }
                    if (!info->value) {
                        throw SyntaxError{info->name + (info->compareField.empty()
                                                            ? " does not support comparisons"
                                                            : " can only be compared with a literal"),
                                          opPos};
                    }
                }
                Node right = ParseAdditive();
                return ConditionNode(MakeCompare(AsValue(std::move(left)), op, AsValue(std::move(right))));
            }

            static Node Math(const char* op, std::vector<json> args) {
                return ValueNode(json{{"type", "Math"}, {"operator", op}, {"args", std::move(args)}});
            }

            Node ParseAdditive() {
                Node left = ParseTerm();
                while (Peek().type == Tok::Plus || Peek().type == Tok::Minus) {
                    const char* op = Next().type == Tok::Plus ? "+" : "-";
                    Node right = ParseTerm();
                    left = Math(op, {AsValue(std::move(left)), AsValue(std::move(right))});
                }
                return left;
            }

            Node ParseTerm() {
                Node left = ParseUnary();
                while (Peek().type == Tok::Star || Peek().type == Tok::Slash) {
                    const char* op = Next().type == Tok::Star ? "*" : "/";
                    Node right = ParseUnary();
                    left = Math(op, {AsValue(std::move(left)), AsValue(std::move(right))});
                }
                return left;
            }

            Node ParseUnary() {
                if (Accept(Tok::Plus)) return ParseUnary();
                if (Accept(Tok::Minus)) {
                    Node operand = ParseUnary();
                    json value = AsValue(std::move(operand));
                    if (value.is_number_integer()) return ValueNode(-value.get<std::int64_t>());
                    if (value.is_number()) return ValueNode(-value.get<double>());
                    return Math("neg", {std::move(value)});
                }
                return ParseAtom();
            }

            Node ParseAtom() {
                const Token& tok = Peek();
                switch (tok.type) {
                    case Tok::LParen: {
                        Next();
                        Node inner = ParseOr();
                        Expect(Tok::RParen, "')'");
                        return inner;
                    }
                    case Tok::Number:
                    case Tok::Param:  // ${param} becomes a number once substituted
                        return ValueNode(ParseValue());
                    case Tok::Var:
                        Next();
                        return ValueNode(json{{"type", "NamedValue"}, {"name", tok.text}});
                    case Tok::At: {
                        Next();
                        const Token& name = Next();
                        if (name.type != Tok::Ident && name.type != Tok::String) {
                            throw SyntaxError{"Expected a name after '@'", name.pos};
                        }
                        return ConditionNode(json{{"type", "Rule"}, {"ruleName", name.text}});
                    }
                    case Tok::Ident: {
                        if (IEquals(tok.text, "true") || IEquals(tok.text, "false")) {
                            Next();
                            return ConditionNode(json{{"type", "Constant"}, {"value", IEquals(tok.text, "true")}});
                        }
                        if (Peek(1).type == Tok::LParen && IsFunctionName(tok.text) && !Registry::Get().FindType(tok.text)) {
                            return ParseFunction();
                        }
                        return ParseCall();
                    }
                    default:
                        throw SyntaxError{"Expected a condition or value", tok.pos};
                }
            }

            Node ParseFunction() {
                const Token& nameTok = Next();
                std::string name = nameTok.text;
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                Expect(Tok::LParen, "'('");
                std::vector<json> args;
                if (!Accept(Tok::RParen)) {
                    do { args.push_back(AsValue(ParseOr())); } while (Accept(Tok::Comma));
                    Expect(Tok::RParen, "')' or ','");
                }
                const auto op = MathValue::ParseOp(name);
                const auto arity = op ? MathValue::Arity(*op) : std::optional<std::size_t>{};
                if (arity ? args.size() != *arity : args.size() < 2) {
                    throw SyntaxError{name + " takes " + (arity ? std::to_string(*arity) : std::string("two or more")) +
                                          " argument(s)",
                                      nameTok.pos};
                }
                return Math(MathValue::OpName(*op), std::move(args));
            }

            Node ParseCall() {
                const Token& nameTok = Next();
                const TypeInfo* info = Registry::Get().FindType(nameTok.text);
                if (!info) {
                    throw SyntaxError{"Unknown condition type '" + nameTok.text + "' (use @" + nameTok.text +
                                          " for a named condition or $" + nameTok.text + " for a named value)",
                                      nameTok.pos};
                }

                json result{{"type", info->name}};

                if (Accept(Tok::LParen)) {
                    std::size_t positional = 0;
                    if (!Accept(Tok::RParen)) {
                        do {
                            // field=value
                            if (Peek().type == Tok::Ident && Peek(1).type == Tok::Assign) {
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
                return Node{std::move(result), Kind::Call, info};
            }

            // A literal: number (optionally signed), string, word, list or ${param}
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
                    case Tok::Minus:
                    case Tok::Plus: {
                        if (Peek().type != Tok::Number) throw SyntaxError{"Expected a number", Peek().pos};
                        json number = ParseValue();
                        if (tok.type == Tok::Plus) return number;
                        return number.is_number_integer() ? json(-number.get<std::int64_t>()) : json(-number.get<double>());
                    }
                    case Tok::Number: {
                        const std::string& t = tok.text;
                        try {
                            if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
                                return static_cast<std::int64_t>(std::stoll(t, nullptr, 16));
                            }
                            if (t.find_first_of(".eE") != std::string::npos) return std::stod(t);
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
        std::string PrintValueNode(const json& v, int precedence);

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

            if (type == "Compare") {
                const auto op = ParseComparisonOperator(j, "operator");
                return PrintValueNode(j.value("left", json()), 1) + " " + ComparisonOpToString(op) + " " +
                       PrintValueNode(j.value("right", json()), 1);
            }

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

    namespace {
        // True when the condition prints as one call with no operators, so it can sit
        // inside arithmetic without parentheses
        bool PrintsAtomically(const json& j) {
            if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) return true;
            const auto* info = Registry::Get().FindType(j["type"].get<std::string>());
            const std::string type = info ? info->name : j["type"].get<std::string>();
            if (type == "Logical" || type == "Compare") return false;
            if (type == "Rule") return !j.value("inverted", false);
            if (info && !info->compareField.empty() && j.contains(info->compareField) &&
                (j.contains("operator") || j.contains("operation"))) {
                return false;
            }
            return true;
        }

        // precedence: 0 = anywhere, 1 = additive / comparison operand, 2 = multiplicative,
        // 3 = unary operand
        std::string PrintValueNode(const json& v, int precedence) {
            if (v.is_number() || v.is_boolean()) return PrintValue(v);
            if (v.is_string()) return PrintValue(v);
            if (!v.is_object() || !v.contains("type") || !v["type"].is_string()) return "0";

            const std::string type = v["type"].get<std::string>();
            if (IEquals(type, "NamedValue")) return "$" + v.value("name", std::string());

            if (IEquals(type, "Truth")) {
                const json condition = v.value("condition", json());
                std::string text = Print(condition);
                return precedence >= 1 && !PrintsAtomically(condition) ? "(" + text + ")" : text;
            }

            if (IEquals(type, "Math")) {
                const std::string op = v.value("operator", "");
                const json args = v.value("args", json::array());
                auto arg = [&](std::size_t i, int p) { return i < args.size() ? PrintValueNode(args[i], p) : "0"; };
                int own = 0;
                std::string text;
                if (op == "+" || op == "-" || op == "*" || op == "/") {
                    own = (op == "+" || op == "-") ? 1 : 2;
                    // Left-associative: an equal-precedence right operand keeps its parentheses
                    text = arg(0, own) + " " + op + " " + arg(1, own + 1);
                } else if (IEquals(op, "neg")) {
                    own = 3;
                    text = "-" + arg(0, 3);
                } else {
                    text = op + "(";
                    for (std::size_t i = 0; i < args.size(); ++i) {
                        if (i > 0) text += ", ";
                        text += PrintValueNode(args[i], 0);
                    }
                    return text + ")";
                }
                return own < precedence ? "(" + text + ")" : text;
            }

            // A numeric type read as its number
            std::string text = Print(v);
            return precedence >= 1 && !PrintsAtomically(v) ? "(" + text + ")" : text;
        }
    }

    std::optional<nlohmann::json> ParseToJson(std::string_view source, ParseError* error) {
        try {
            Parser parser(Lex(source));
            return parser.ParseAll(false);
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

    // ── Values ──────────────────────────────────────────────────────────────

    std::optional<nlohmann::json> ParseValueToJson(std::string_view source, ParseError* error) {
        try {
            Parser parser(Lex(source));
            return parser.ParseAll(true);
        } catch (const SyntaxError& e) {
            if (error) *error = ParseError{e.message, e.pos};
            return std::nullopt;
        }
    }

    std::unique_ptr<Value> CompileValue(std::string_view source, ParseError* error) {
        auto tree = ParseValueToJson(source, error);
        if (!tree) return nullptr;
        std::string buildError;
        auto value = Value::FromJson(*tree, &buildError);
        if (!value && error) *error = ParseError{buildError.empty() ? "Value could not be built from the expression" : buildError, 0};
        return value;
    }

    std::unique_ptr<Value> CompileValueAny(std::string_view source, std::string* error) {
        const auto first = source.find_first_not_of(" \t\r\n");
        const auto last = source.find_last_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            if (error) *error = "Empty value expression";
            return nullptr;
        }
        const auto trimmed = source.substr(first, last - first + 1);

        if (trimmed.front() == '{') {
            try {
                return Value::FromJson(nlohmann::json::parse(trimmed), error);
            } catch (const std::exception& e) {
                if (error) *error = std::string("Invalid JSON: ") + e.what();
                return nullptr;
            }
        }

        // A bare name that is not a type is a named value, defined now or later
        const std::string word(trimmed);
        if (NamedValueDef::IsValidName(word) && !Registry::Get().FindType(word) && !IEquals(word, "true") &&
            !IEquals(word, "false")) {
            return std::make_unique<NamedValueRef>(word);
        }

        ParseError parseError;
        auto value = CompileValue(trimmed, &parseError);
        if (!value && error) {
            *error = parseError.message + " (at position " + std::to_string(parseError.position) + ")";
        }
        return value;
    }

    std::string ValueFromJson(const nlohmann::json& json) {
        return PrintValueNode(json, 0);
    }

    std::string ToExpression(const Value& value) {
        return PrintValueNode(value.ToJson(), 0);
    }

} // namespace ConditionEngine::Expression
