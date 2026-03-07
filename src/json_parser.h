#pragma once
#include <string>
#include <vector>
#include <map>
#include <stdexcept>

enum class JsonType { Null, Bool, Number, String, Array, Object };

struct JsonValue {
    JsonType type = JsonType::Null;
    std::string str;
    double num = 0;
    bool b = false;
    std::vector<JsonValue> arr;
    std::map<std::string, JsonValue> obj;

    bool isNull()   const { return type == JsonType::Null;   }
    bool isBool()   const { return type == JsonType::Bool;   }
    bool isNumber() const { return type == JsonType::Number; }
    bool isString() const { return type == JsonType::String; }
    bool isArray()  const { return type == JsonType::Array;  }
    bool isObject() const { return type == JsonType::Object; }

    const JsonValue& operator[](const std::string& key) const {
        static const JsonValue null_v;
        auto it = obj.find(key);
        return it != obj.end() ? it->second : null_v;
    }
    const JsonValue& operator[](size_t i) const {
        static const JsonValue null_v;
        return i < arr.size() ? arr[i] : null_v;
    }
    bool has(const std::string& key) const {
        return obj.find(key) != obj.end();
    }
    std::string asString(const std::string& def = "") const {
        return isString() ? str : def;
    }
    double asNumber(double def = 0) const {
        return isNumber() ? num : def;
    }
    long long asInt(long long def = 0) const {
        return isNumber() ? static_cast<long long>(num) : def;
    }
    bool asBool(bool def = false) const {
        return isBool() ? b : def;
    }
};

class JsonParser {
    std::string src;
    size_t pos = 0;

    void skipWS() {
        while (pos < src.size() && (src[pos]==' '||src[pos]=='\t'||src[pos]=='\n'||src[pos]=='\r'))
            ++pos;
    }
    char peek() { skipWS(); return pos < src.size() ? src[pos] : 0; }
    char consume() { return src[pos++]; }
    bool match(char c) { skipWS(); if(pos<src.size()&&src[pos]==c){++pos;return true;} return false; }
    void expect(char c) { skipWS(); if(!match(c)) throw std::runtime_error(std::string("Expected '")+c+"' at pos "+std::to_string(pos)); }

    std::string parseString() {
        expect('"');
        std::string s;
        while (pos < src.size()) {
            char c = consume();
            if (c == '"') return s;
            if (c == '\\') {
                char esc = consume();
                switch(esc) {
                    case '"': s+='"'; break;
                    case '\\': s+='\\'; break;
                    case '/': s+='/'; break;
                    case 'n': s+='\n'; break;
                    case 'r': s+='\r'; break;
                    case 't': s+='\t'; break;
                    case 'u': {
                        std::string hex = src.substr(pos, 4);
                        pos += 4;
                        unsigned cp = std::stoul(hex, nullptr, 16);
                        if (cp < 0x80) s += (char)cp;
                        else if (cp < 0x800) { s += (char)(0xC0|(cp>>6)); s += (char)(0x80|(cp&0x3F)); }
                        else { s += (char)(0xE0|(cp>>12)); s += (char)(0x80|((cp>>6)&0x3F)); s += (char)(0x80|(cp&0x3F)); }
                        break;
                    }
                    default: s += esc;
                }
            } else {
                s += c;
            }
        }
        throw std::runtime_error("Unterminated string");
    }

    JsonValue parseValue() {
        char c = peek();
        if (c == '"') {
            JsonValue v; v.type = JsonType::String; v.str = parseString(); return v;
        }
        if (c == '{') {
            JsonValue v; v.type = JsonType::Object;
            expect('{');
            if (peek() != '}') {
                do {
                    std::string key = parseString();
                    expect(':');
                    v.obj[key] = parseValue();
                } while (match(','));
            }
            expect('}');
            return v;
        }
        if (c == '[') {
            JsonValue v; v.type = JsonType::Array;
            expect('[');
            if (peek() != ']') {
                do { v.arr.push_back(parseValue()); } while (match(','));
            }
            expect(']');
            return v;
        }
        if (c == 't') { pos+=4; JsonValue v; v.type=JsonType::Bool; v.b=true; return v; }
        if (c == 'f') { pos+=5; JsonValue v; v.type=JsonType::Bool; v.b=false; return v; }
        if (c == 'n') { pos+=4; return JsonValue{}; }
        size_t start = pos; skipWS();
        bool neg = (pos < src.size() && src[pos] == '-');
        if (neg) ++pos;
        while (pos < src.size() && (isdigit(src[pos])||src[pos]=='.'||src[pos]=='e'||src[pos]=='E'||src[pos]=='+'||src[pos]=='-'))
            ++pos;
        JsonValue v; v.type = JsonType::Number;
        v.num = std::stod(src.substr(start, pos - start));
        return v;
    }

public:
    explicit JsonParser(const std::string& s) : src(s) {}
    JsonValue parse() { return parseValue(); }
};

inline JsonValue parseJson(const std::string& s) {
    JsonParser p(s); return p.parse();
}
