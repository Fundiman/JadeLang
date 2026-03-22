#pragma once
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <sstream>

namespace jade {

// ── JadeString ────────────────────────────────────────────────────────────────
// Jade's runtime string type — UTF-8, immutable, RC managed

struct JadeString {
    std::string data;

    explicit JadeString(std::string s) : data(std::move(s)) {}
    explicit JadeString(const char* s) : data(s) {}

    size_t      len()    const { return data.size(); }
    bool        empty()  const { return data.empty(); }
    const char* c_str()  const { return data.c_str(); }

    JadeString operator+(const JadeString& other) const {
        return JadeString(data + other.data);
    }

    bool operator==(const JadeString& o) const { return data == o.data; }
    bool operator!=(const JadeString& o) const { return data != o.data; }
};

// ── interpolation value types ─────────────────────────────────────────────────
// when the compiler sees "hello #{name}" it emits calls into this API
// each #{expr} is evaluated and converted to a string segment

enum class InterpKind { STR, INT, FLOAT, BOOL, NONE };

struct InterpValue {
    InterpKind kind;
    std::string str_val;
    int64_t     int_val   = 0;
    double      float_val = 0.0;
    bool        bool_val  = false;

    static InterpValue from_str(const char* s)  { return {InterpKind::STR,   s}; }
    static InterpValue from_str(std::string s)  { return {InterpKind::STR,   std::move(s)}; }
    static InterpValue from_int(int64_t v)      { return {InterpKind::INT,   "", v}; }
    static InterpValue from_float(double v)     { return {InterpKind::FLOAT, "", 0, v}; }
    static InterpValue from_bool(bool v)        { return {InterpKind::BOOL,  "", 0, 0.0, v}; }
    static InterpValue none()                   { return {InterpKind::NONE}; }

    std::string to_string() const {
        switch (kind) {
            case InterpKind::STR:   return str_val;
            case InterpKind::INT:   return std::to_string(int_val);
            case InterpKind::FLOAT: {
                std::ostringstream ss;
                ss << float_val;
                return ss.str();
            }
            case InterpKind::BOOL:  return bool_val ? "true" : "false";
            case InterpKind::NONE:  return "null";
        }
        return "";
    }
};

// ── interpolated string builder ───────────────────────────────────────────────
// the compiler turns "hello #{name}, you have #{count} messages"
// into:
//   InterpolatedString s;
//   s.add_literal("hello ");
//   s.add_value(InterpValue::from_str(name));
//   s.add_literal(", you have ");
//   s.add_value(InterpValue::from_int(count));
//   s.add_literal(" messages");
//   std::string result = s.build();

class InterpolatedString {
public:
    void add_literal(const char* lit) {
        m_parts.push_back({ true, lit, {} });
    }

    void add_value(InterpValue val) {
        m_parts.push_back({ false, "", std::move(val) });
    }

    std::string build() const {
        std::string result;
        result.reserve(64);
        for (auto& p : m_parts) {
            if (p.is_literal) result += p.literal;
            else              result += p.value.to_string();
        }
        return result;
    }

    // convenience: build directly from a template string + values map
    // template: "hello #{name}" + {"name": InterpValue::from_str("Jade")}
    static std::string resolve(const std::string& tmpl,
                               const std::unordered_map<std::string,
                                                         InterpValue>& vars) {
        std::string result;
        result.reserve(tmpl.size() * 2);
        size_t i = 0;
        while (i < tmpl.size()) {
            // look for #{
            if (i + 1 < tmpl.size() && tmpl[i] == '#' && tmpl[i+1] == '{') {
                size_t start = i + 2;
                size_t end   = tmpl.find('}', start);
                if (end == std::string::npos) {
                    // malformed — just emit the rest literally
                    result += tmpl.substr(i);
                    break;
                }
                std::string var_name = tmpl.substr(start, end - start);
                auto it = vars.find(var_name);
                if (it != vars.end())
                    result += it->second.to_string();
                else
                    result += "#{" + var_name + "}"; // leave unresolved
                i = end + 1;
            } else {
                result += tmpl[i++];
            }
        }
        return result;
    }

private:
    struct Part {
        bool        is_literal;
        std::string literal;
        InterpValue value;
    };
    std::vector<Part> m_parts;
};

// ── C ABI exported functions ──────────────────────────────────────────────────
// these are called by compiled Jade code at runtime
// the compiler emits calls to jade_interp_* for every interpolated string

extern "C" {
    // create a new interpolation buffer
    void* jade_interp_new();

    // add a literal segment
    void  jade_interp_literal(void* buf, const char* lit);

    // add a typed value
    void  jade_interp_str  (void* buf, const char* val);
    void  jade_interp_int  (void* buf, int64_t val);
    void  jade_interp_float(void* buf, double val);
    void  jade_interp_bool (void* buf, int val);

    // finalize — returns a malloc'd C string (caller must free)
    const char* jade_interp_build(void* buf);

    // free the buffer
    void  jade_interp_free(void* buf);
}

} // namespace jade
