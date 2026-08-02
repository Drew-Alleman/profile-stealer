// detail/json.hpp
//
// Minimal, dependency-free JSON value type.
//
// This is a drop-in replacement for the small subset of the nlohmann/json API
// that this project uses:
//
//     njson::json j = njson::json::parse(text);
//     j.contains("id");
//     j.value("id", -1);                       // typed lookup with default
//     j.value("message", "unknown error");
//     j.value("result", njson::json::object());
//     j["error"].value("message", "...");
//     njson::json req = {{"id", id}, {"method", m}, {"params", p}};  // object build
//     req["sessionId"] = session_id;
//     std::string wire = req.dump();
//
// It is intentionally small and readable rather than maximally fast. It parses
// the full JSON grammar (objects, arrays, strings with \uXXXX + surrogate
// pairs, numbers, true/false/null) and serialises with correct escaping.
//
// C++17, header-only, standard library only.

#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace njson {

    class json {
    public:
        enum class Type { Null, Boolean, Integer, Double, String, Array, Object };

        using array_t = std::vector<json>;
        using object_t = std::map<std::string, json>;

        // ---- construction -------------------------------------------------
        json() = default;
        json(std::nullptr_t) {}
        json(bool b) : type_(Type::Boolean), bool_(b) {}
        json(int i) : type_(Type::Integer), int_(i) {}
        json(long long i) : type_(Type::Integer), int_(i) {}
        json(unsigned i) : type_(Type::Integer), int_(static_cast<std::int64_t>(i)) {}
        json(unsigned long long i) : type_(Type::Integer), int_(static_cast<std::int64_t>(i)) {}
        json(double d) : type_(Type::Double), dbl_(d) {}
        json(const char* s) : type_(Type::String), str_(s ? s : "") {}
        json(const std::string& s) : type_(Type::String), str_(s) {}
        json(std::string&& s) : type_(Type::String), str_(std::move(s)) {}

        // Braced construction, matching nlohmann's heuristic: a brace list is
        // treated as an object when every element is a two-element array whose
        // first element is a string; otherwise it is an array.
        json(std::initializer_list<json> init) {
            bool looks_like_object = init.size() > 0;
            for (const auto& e : init) {
                if (!(e.type_ == Type::Array && e.arr_.size() == 2 &&
                      e.arr_[0].type_ == Type::String)) {
                    looks_like_object = false;
                    break;
                }
            }
            if (looks_like_object) {
                type_ = Type::Object;
                for (const auto& e : init)
                    obj_[e.arr_[0].str_] = e.arr_[1];
            } else {
                type_ = Type::Array;
                arr_.assign(init.begin(), init.end());
            }
        }

        // Build an array from any std::vector whose element type converts to
        // json (e.g. std::vector<std::string>, std::vector<int>). Enables
        //   json params = { {"permissions", permissions} };
        template <typename T,
                  typename std::enable_if<std::is_constructible<json, const T&>::value, int>::type = 0>
        json(const std::vector<T>& v) : type_(Type::Array) {
            arr_.reserve(v.size());
            for (const auto& e : v) arr_.emplace_back(e);
        }

        // Build an object from a std::map<std::string, T>.
        template <typename T,
                  typename std::enable_if<std::is_constructible<json, const T&>::value, int>::type = 0>
        json(const std::map<std::string, T>& m) : type_(Type::Object) {
            for (const auto& kv : m) obj_.emplace(kv.first, json(kv.second));
        }

        // Explicit factories.
        static json object() { json j; j.type_ = Type::Object; return j; }
        static json array() { json j; j.type_ = Type::Array; return j; }
        // json::array({a, b, c}) — always builds an array, even when the
        // elements would otherwise look like object key/value pairs.
        static json array(std::initializer_list<json> init) {
            json j; j.type_ = Type::Array;
            j.arr_.assign(init.begin(), init.end());
            return j;
        }

        // ---- type queries -------------------------------------------------
        Type type() const noexcept { return type_; }
        bool is_null()    const noexcept { return type_ == Type::Null; }
        bool is_boolean() const noexcept { return type_ == Type::Boolean; }
        bool is_number()  const noexcept { return type_ == Type::Integer || type_ == Type::Double; }
        bool is_integer() const noexcept { return type_ == Type::Integer; }
        bool is_number_integer()  const noexcept { return type_ == Type::Integer; }
        bool is_number_unsigned() const noexcept { return type_ == Type::Integer && int_ >= 0; }
        bool is_number_float()    const noexcept { return type_ == Type::Double; }
        bool is_string()  const noexcept { return type_ == Type::String; }
        bool is_array()   const noexcept { return type_ == Type::Array; }
        bool is_object()  const noexcept { return type_ == Type::Object; }

        std::size_t size() const noexcept {
            if (type_ == Type::Array)  return arr_.size();
            if (type_ == Type::Object) return obj_.size();
            return 0;
        }

        bool empty() const noexcept {
            if (type_ == Type::Array)  return arr_.empty();
            if (type_ == Type::Object) return obj_.empty();
            return type_ == Type::Null;   // null is empty; scalars are not
        }

        // ---- object / array access ---------------------------------------
        bool contains(const std::string& key) const {
            return type_ == Type::Object && obj_.find(key) != obj_.end();
        }

        std::size_t count(const std::string& key) const {
            return contains(key) ? 1u : 0u;
        }

        // Checked access (throws like nlohmann's at()).
        json& at(const std::string& key) {
            if (type_ != Type::Object) throw std::out_of_range("json::at: not an object");
            auto it = obj_.find(key);
            if (it == obj_.end()) throw std::out_of_range("json::at: key not found: " + key);
            return it->second;
        }
        const json& at(const std::string& key) const {
            if (type_ != Type::Object) throw std::out_of_range("json::at: not an object");
            auto it = obj_.find(key);
            if (it == obj_.end()) throw std::out_of_range("json::at: key not found: " + key);
            return it->second;
        }
        json&       at(std::size_t i)       { require_array(); return arr_.at(i); }
        const json& at(std::size_t i) const { require_array(); return arr_.at(i); }

        // Non-const: auto-vivifies into an object (like nlohmann).
        json& operator[](const std::string& key) {
            if (type_ == Type::Null) type_ = Type::Object;
            return obj_[key];
        }
        const json& operator[](const std::string& key) const {
            static const json null_ref;
            if (type_ != Type::Object) return null_ref;
            auto it = obj_.find(key);
            return it == obj_.end() ? null_ref : it->second;
        }

        json& operator[](std::size_t i) { return arr_.at(i); }
        const json& operator[](std::size_t i) const { return arr_.at(i); }

        // C-string key overloads. These are TEMPLATES on purpose:
        //   * For j["literal"], const char[N] -> const char* is a *standard*
        //     conversion, so this beats both the std::string overload and the
        //     built-in pointer-subscript operator => no ambiguity even with the
        //     implicit conversion operator below.
        //   * For j[0], template argument deduction from int fails, so only the
        //     size_t overload applies => j[0] still indexes arrays cleanly.
        template <typename C, typename std::enable_if<std::is_same<C, char>::value, int>::type = 0>
        json& operator[](const C* key) {
            if (type_ == Type::Null) type_ = Type::Object;
            return obj_[std::string(key)];
        }
        template <typename C, typename std::enable_if<std::is_same<C, char>::value, int>::type = 0>
        const json& operator[](const C* key) const {
            static const json null_ref;
            if (type_ != Type::Object) return null_ref;
            auto it = obj_.find(key);
            return it == obj_.end() ? null_ref : it->second;
        }

        const array_t&  as_array()  const { return arr_; }
        const object_t& as_object() const { return obj_; }

        void push_back(json v) {
            if (type_ == Type::Null) type_ = Type::Array;
            arr_.push_back(std::move(v));
        }

        // ---- typed extraction --------------------------------------------
        bool        as_bool(bool def = false) const { return type_ == Type::Boolean ? bool_ : def; }
        std::int64_t as_int(std::int64_t def = 0) const {
            if (type_ == Type::Integer) return int_;
            if (type_ == Type::Double)  return static_cast<std::int64_t>(dbl_);
            return def;
        }
        double as_double(double def = 0.0) const {
            if (type_ == Type::Double)  return dbl_;
            if (type_ == Type::Integer) return static_cast<double>(int_);
            return def;
        }
        const std::string& as_string() const {
            static const std::string empty;
            return type_ == Type::String ? str_ : empty;
        }

        // ---- value(key, default) with type following the default ----------
        json value(const std::string& key, const json& def) const {
            if (type_ == Type::Object) {
                auto it = obj_.find(key);
                if (it != obj_.end()) return it->second;
            }
            return def;
        }
        int value(const std::string& key, int def) const {
            if (type_ == Type::Object) {
                auto it = obj_.find(key);
                if (it != obj_.end() && it->second.is_number())
                    return static_cast<int>(it->second.as_int());
            }
            return def;
        }
        std::int64_t value(const std::string& key, std::int64_t def) const {
            if (type_ == Type::Object) {
                auto it = obj_.find(key);
                if (it != obj_.end() && it->second.is_number())
                    return it->second.as_int();
            }
            return def;
        }
        double value(const std::string& key, double def) const {
            if (type_ == Type::Object) {
                auto it = obj_.find(key);
                if (it != obj_.end() && it->second.is_number())
                    return it->second.as_double();
            }
            return def;
        }
        bool value(const std::string& key, bool def) const {
            if (type_ == Type::Object) {
                auto it = obj_.find(key);
                if (it != obj_.end() && it->second.is_boolean())
                    return it->second.as_bool();
            }
            return def;
        }
        std::string value(const std::string& key, const std::string& def) const {
            if (type_ == Type::Object) {
                auto it = obj_.find(key);
                if (it != obj_.end() && it->second.is_string())
                    return it->second.as_string();
            }
            return def;
        }
        std::string value(const std::string& key, const char* def) const {
            return value(key, std::string(def ? def : ""));
        }

        // ---- typed get<T>() (throws on type mismatch, like nlohmann) -------
        template <typename T>
        T get() const {
            if constexpr (std::is_same_v<T, json>) {
                return *this;
            } else if constexpr (std::is_same_v<T, std::string>) {
                if (type_ != Type::String) throw std::runtime_error("json::get: not a string");
                return str_;
            } else if constexpr (std::is_same_v<T, bool>) {
                if (type_ != Type::Boolean) throw std::runtime_error("json::get: not a boolean");
                return bool_;
            } else if constexpr (std::is_integral_v<T>) {
                if (!is_number()) throw std::runtime_error("json::get: not a number");
                return static_cast<T>(as_int());
            } else if constexpr (std::is_floating_point_v<T>) {
                if (!is_number()) throw std::runtime_error("json::get: not a number");
                return static_cast<T>(as_double());
            } else {
                static_assert(sizeof(T) == 0, "json::get<T>: unsupported target type");
            }
        }

        template <typename T>
        void get_to(T& out) const { out = get<T>(); }

        // Implicit conversion to string / arithmetic types, so existing
        // nlohmann-style code such as `std::string s = j["value"];` and
        // `int n = j["id"];` compiles unchanged. Safe against the built-in
        // pointer-subscript ambiguity because the templated C-string
        // operator[] above matches j["literal"] by a standard conversion.
        // json itself is excluded so copy/move construction is unaffected.
        template <typename T,
                  typename std::enable_if<std::is_same<T, std::string>::value ||
                                          std::is_arithmetic<T>::value, int>::type = 0>
        operator T() const { return get<T>(); }

        // ---- iteration ----------------------------------------------------
        // Range-for over an array yields its elements; over an object yields
        // its values (matching nlohmann). Use items() for key/value pairs.
        template <bool Const>
        struct basic_iterator {
            using arr_it = std::conditional_t<Const, array_t::const_iterator, array_t::iterator>;
            using obj_it = std::conditional_t<Const, object_t::const_iterator, object_t::iterator>;
            using json_ref = std::conditional_t<Const, const json&, json&>;
            bool is_obj = false;
            arr_it ait{};
            obj_it oit{};
            json_ref operator*()  const { return is_obj ? oit->second : *ait; }
            auto     operator->() const { return is_obj ? &oit->second : &(*ait); }
            basic_iterator& operator++() { if (is_obj) ++oit; else ++ait; return *this; }
            bool operator!=(const basic_iterator& o) const {
                return is_obj ? oit != o.oit : ait != o.ait;
            }
            bool operator==(const basic_iterator& o) const { return !(*this != o); }
        };
        using iterator = basic_iterator<false>;
        using const_iterator = basic_iterator<true>;

        iterator begin() {
            iterator it; it.is_obj = (type_ == Type::Object);
            if (it.is_obj) it.oit = obj_.begin(); else it.ait = arr_.begin();
            return it;
        }
        iterator end() {
            iterator it; it.is_obj = (type_ == Type::Object);
            if (it.is_obj) it.oit = obj_.end(); else it.ait = arr_.end();
            return it;
        }
        const_iterator begin() const {
            const_iterator it; it.is_obj = (type_ == Type::Object);
            if (it.is_obj) it.oit = obj_.begin(); else it.ait = arr_.begin();
            return it;
        }
        const_iterator end() const {
            const_iterator it; it.is_obj = (type_ == Type::Object);
            if (it.is_obj) it.oit = obj_.end(); else it.ait = arr_.end();
            return it;
        }

        // items(): objects only, matching
        //   for (auto& el : j.items()) { el.key(); el.value(); }
        // The iterator itself is the element proxy (dereferences to *this).
        template <bool Const>
        struct item_proxy {
            using obj_it = std::conditional_t<Const, object_t::const_iterator, object_t::iterator>;
            using json_ref = std::conditional_t<Const, const json&, json&>;
            obj_it it;
            const std::string& key()   const { return it->first; }
            json_ref           value() const { return it->second; }
            item_proxy& operator*()  { return *this; }
            item_proxy& operator++() { ++it; return *this; }
            bool operator!=(const item_proxy& o) const { return it != o.it; }
        };
        template <bool Const>
        struct items_view {
            using obj_ptr = std::conditional_t<Const, const object_t*, object_t*>;
            obj_ptr obj;
            item_proxy<Const> begin() const { return { obj->begin() }; }
            item_proxy<Const> end()   const { return { obj->end() }; }
        };
        items_view<false> items()       { return { &obj_ }; }
        items_view<true>  items() const { return { &obj_ }; }

        // ---- serialisation ------------------------------------------------
        std::string dump() const {
            std::string out;
            dump_to(out);
            return out;
        }
        // Pretty-print with `indent` spaces per level (nlohmann-compatible).
        std::string dump(int indent) const {
            std::string out;
            dump_pretty(out, indent, 0);
            return out;
        }

        // ---- parsing ------------------------------------------------------
        static json parse(const std::string& text) {
            Parser p(text);
            json result = p.parse_value();
            p.skip_ws();
            if (!p.at_end())
                throw std::runtime_error("json parse: trailing characters");
            return result;
        }

    private:
        Type        type_ = Type::Null;
        bool        bool_ = false;
        std::int64_t int_ = 0;
        double      dbl_ = 0.0;
        std::string str_;
        array_t     arr_;
        object_t    obj_;

        void require_array() const {
            if (type_ != Type::Array) throw std::out_of_range("json::at: not an array");
        }

        // ---- serialisation helpers ---------------------------------------
        static void dump_string(const std::string& s, std::string& out) {
            out.push_back('"');
            for (unsigned char c : s) {
                switch (c) {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b";  break;
                    case '\f': out += "\\f";  break;
                    case '\n': out += "\\n";  break;
                    case '\r': out += "\\r";  break;
                    case '\t': out += "\\t";  break;
                    default:
                        if (c < 0x20) {
                            static const char* hex = "0123456789abcdef";
                            out += "\\u00";
                            out.push_back(hex[(c >> 4) & 0xF]);
                            out.push_back(hex[c & 0xF]);
                        } else {
                            out.push_back(static_cast<char>(c));  // pass UTF-8 through
                        }
                }
            }
            out.push_back('"');
        }

        static void dump_number_double(double d, std::string& out) {
            // Round-trip-safe formatting without <sstream> overhead.
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.17g", d);
            out += buf;
        }

        void dump_to(std::string& out) const {
            switch (type_) {
                case Type::Null:    out += "null"; break;
                case Type::Boolean: out += (bool_ ? "true" : "false"); break;
                case Type::Integer: out += std::to_string(int_); break;
                case Type::Double:  dump_number_double(dbl_, out); break;
                case Type::String:  dump_string(str_, out); break;
                case Type::Array: {
                    out.push_back('[');
                    bool first = true;
                    for (const auto& e : arr_) {
                        if (!first) out.push_back(',');
                        first = false;
                        e.dump_to(out);
                    }
                    out.push_back(']');
                    break;
                }
                case Type::Object: {
                    out.push_back('{');
                    bool first = true;
                    for (const auto& kv : obj_) {
                        if (!first) out.push_back(',');
                        first = false;
                        dump_string(kv.first, out);
                        out.push_back(':');
                        kv.second.dump_to(out);
                    }
                    out.push_back('}');
                    break;
                }
            }
        }

        void dump_pretty(std::string& out, int indent, int depth) const {
            const auto pad = [&](int d) { out.append(static_cast<std::size_t>(indent) * d, ' '); };
            switch (type_) {
                case Type::Array: {
                    if (arr_.empty()) { out += "[]"; break; }
                    out += "[\n";
                    bool first = true;
                    for (const auto& e : arr_) {
                        if (!first) out += ",\n";
                        first = false;
                        pad(depth + 1);
                        e.dump_pretty(out, indent, depth + 1);
                    }
                    out.push_back('\n'); pad(depth); out.push_back(']');
                    break;
                }
                case Type::Object: {
                    if (obj_.empty()) { out += "{}"; break; }
                    out += "{\n";
                    bool first = true;
                    for (const auto& kv : obj_) {
                        if (!first) out += ",\n";
                        first = false;
                        pad(depth + 1);
                        dump_string(kv.first, out);
                        out += ": ";
                        kv.second.dump_pretty(out, indent, depth + 1);
                    }
                    out.push_back('\n'); pad(depth); out.push_back('}');
                    break;
                }
                default:
                    dump_to(out);
            }
        }

        // ---- recursive-descent parser ------------------------------------
        struct Parser {
            const std::string& s;
            std::size_t i = 0;
            explicit Parser(const std::string& src) : s(src) {}

            bool at_end() const { return i >= s.size(); }
            char peek() const { return i < s.size() ? s[i] : '\0'; }

            void skip_ws() {
                while (i < s.size()) {
                    char c = s[i];
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i;
                    else break;
                }
            }

            [[noreturn]] void fail(const std::string& why) const {
                throw std::runtime_error("json parse: " + why +
                    " at offset " + std::to_string(i));
            }

            json parse_value() {
                skip_ws();
                if (at_end()) fail("unexpected end of input");
                char c = s[i];
                switch (c) {
                    case '{': return parse_object();
                    case '[': return parse_array();
                    case '"': { json j; j.type_ = Type::String; j.str_ = parse_string(); return j; }
                    case 't': case 'f': return parse_bool();
                    case 'n': return parse_null();
                    default:  return parse_number();
                }
            }

            void expect(char c) {
                if (at_end() || s[i] != c) fail(std::string("expected '") + c + "'");
                ++i;
            }

            json parse_object() {
                expect('{');
                json j; j.type_ = Type::Object;
                skip_ws();
                if (peek() == '}') { ++i; return j; }
                for (;;) {
                    skip_ws();
                    if (peek() != '"') fail("expected string key");
                    std::string key = parse_string();
                    skip_ws();
                    expect(':');
                    j.obj_[key] = parse_value();
                    skip_ws();
                    char c = peek();
                    if (c == ',') { ++i; continue; }
                    if (c == '}') { ++i; break; }
                    fail("expected ',' or '}'");
                }
                return j;
            }

            json parse_array() {
                expect('[');
                json j; j.type_ = Type::Array;
                skip_ws();
                if (peek() == ']') { ++i; return j; }
                for (;;) {
                    j.arr_.push_back(parse_value());
                    skip_ws();
                    char c = peek();
                    if (c == ',') { ++i; continue; }
                    if (c == ']') { ++i; break; }
                    fail("expected ',' or ']'");
                }
                return j;
            }

            json parse_bool() {
                if (s.compare(i, 4, "true") == 0)  { i += 4; return json(true); }
                if (s.compare(i, 5, "false") == 0) { i += 5; return json(false); }
                fail("invalid literal");
            }

            json parse_null() {
                if (s.compare(i, 4, "null") == 0) { i += 4; return json(); }
                fail("invalid literal");
            }

            json parse_number() {
                std::size_t start = i;
                bool is_double = false;
                if (peek() == '-') ++i;
                while (i < s.size()) {
                    char c = s[i];
                    if (c >= '0' && c <= '9') { ++i; }
                    else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                        is_double = true; ++i;
                    } else break;
                }
                if (i == start) fail("invalid number");
                std::string tok = s.substr(start, i - start);
                json j;
                if (is_double) {
                    j.type_ = Type::Double;
                    j.dbl_ = std::strtod(tok.c_str(), nullptr);
                } else {
                    // May overflow int64 for absurd inputs; fall back to double.
                    errno = 0;
                    char* end = nullptr;
                    long long v = std::strtoll(tok.c_str(), &end, 10);
                    if (errno == ERANGE) {
                        j.type_ = Type::Double;
                        j.dbl_ = std::strtod(tok.c_str(), nullptr);
                    } else {
                        j.type_ = Type::Integer;
                        j.int_ = v;
                    }
                }
                return j;
            }

            std::string parse_string() {
                expect('"');
                std::string out;
                while (i < s.size()) {
                    char c = s[i++];
                    if (c == '"') return out;
                    if (c == '\\') {
                        if (i >= s.size()) fail("unterminated escape");
                        char e = s[i++];
                        switch (e) {
                            case '"':  out.push_back('"');  break;
                            case '\\': out.push_back('\\'); break;
                            case '/':  out.push_back('/');  break;
                            case 'b':  out.push_back('\b'); break;
                            case 'f':  out.push_back('\f'); break;
                            case 'n':  out.push_back('\n'); break;
                            case 'r':  out.push_back('\r'); break;
                            case 't':  out.push_back('\t'); break;
                            case 'u':  parse_unicode(out); break;
                            default:   fail("invalid escape");
                        }
                    } else {
                        out.push_back(c);
                    }
                }
                fail("unterminated string");
            }

            unsigned parse_hex4() {
                if (i + 4 > s.size()) fail("truncated \\u escape");
                unsigned v = 0;
                for (int k = 0; k < 4; ++k) {
                    char c = s[i++];
                    v <<= 4;
                    if (c >= '0' && c <= '9') v |= (c - '0');
                    else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
                    else fail("invalid hex in \\u escape");
                }
                return v;
            }

            void append_utf8(unsigned cp, std::string& out) {
                if (cp <= 0x7F) {
                    out.push_back(static_cast<char>(cp));
                } else if (cp <= 0x7FF) {
                    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else if (cp <= 0xFFFF) {
                    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
            }

            void parse_unicode(std::string& out) {
                unsigned cp = parse_hex4();
                if (cp >= 0xD800 && cp <= 0xDBFF) {   // high surrogate
                    if (i + 2 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        unsigned lo = parse_hex4();
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            append_utf8(cp, out);   // unpaired; emit both
                            cp = lo;
                        }
                    }
                }
                append_utf8(cp, out);
            }
        };
    };

} // namespace njson
