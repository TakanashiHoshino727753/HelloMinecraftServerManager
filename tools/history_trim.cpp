// 一次性工具：流式精简/拆分 CodeBuddy 会话导出 (history.json)
// 不构建全量 DOM，只抽取：会话元信息 + 每条消息的正文文本 + 工具调用名/目标文件
// 用法: history_trim.exe <history.json> <输出目录> [每条文本块最大字符数]
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <functional>
#include <filesystem>

namespace fs = std::filesystem;

static size_t gMaxText = 6000;
static size_t gMaxFile = 300000; // 每片字符数

struct Parser {
    const std::string& s;
    size_t i = 0;
    explicit Parser(const std::string& str) : s(str) {}

    [[noreturn]] void err(const char* msg) const {
        char buf[256];
        snprintf(buf, sizeof(buf), "JSON parse error: %s at offset %llu (ctx: %.40s)",
                 msg, (unsigned long long)i, s.c_str() + (i < s.size() ? i : s.size()));
        throw std::runtime_error(buf);
    }
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
    }
    bool match(char ch) {
        ws();
        if (i < s.size() && s[i] == ch) { ++i; return true; }
        return false;
    }
    void expect(char ch) {
        ws();
        if (i >= s.size() || s[i] != ch) err("unexpected char");
        ++i;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
        else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    }
    std::string parseString() {
        ws();
        if (i >= s.size() || s[i] != '"') err("string expected");
        ++i;
        std::string out;
        while (i < s.size()) {
            char ch = s[i++];
            if (ch == '"') return out;
            if (ch != '\\') { out += ch; continue; }
            if (i >= s.size()) break;
            char e = s[i++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case '/': out += '/'; break;
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case 'u': {
                unsigned cp = 0;
                for (int k = 0; k < 4 && i < s.size(); ++k, ++i) {
                    char h = s[i];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= unsigned(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= unsigned(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= unsigned(h - 'A' + 10);
                }
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    i += 2;
                    unsigned lo = 0;
                    for (int k = 0; k < 4 && i < s.size(); ++k, ++i) {
                        char h = s[i];
                        lo <<= 4;
                        if (h >= '0' && h <= '9') lo |= unsigned(h - '0');
                        else if (h >= 'a' && h <= 'f') lo |= unsigned(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') lo |= unsigned(h - 'A' + 10);
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                appendUtf8(out, cp);
                break;
            }
            default: out += e; break;
            }
        }
        err("unterminated string");
    }
    void skipValue() {
        ws();
        if (i >= s.size()) return;
        char ch = s[i];
        if (ch == '"') { parseString(); return; }
        if (ch == '{' || ch == '[') {
            int depth = 0;
            while (i < s.size()) {
                char d = s[i];
                if (d == '"') { parseString(); continue; }
                if (d == '{' || d == '[') ++depth;
                else if (d == '}' || d == ']') --depth;
                ++i;
                if (depth == 0) return;
            }
            return;
        }
        while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') ++i;
    }
    void parseArray(const std::function<void()>& elem) {
        expect('[');
        if (match(']')) return;
        while (true) {
            elem();
            if (match(',')) continue;
            expect(']');
            return;
        }
    }
    void parseObject(const std::function<void(const std::string&)>& onKey) {
        expect('{');
        if (match('}')) return;
        while (true) {
            ws();
            std::string k = parseString();
            expect(':');
            onKey(k);
            if (match(',')) continue;
            expect('}');
            return;
        }
    }
};

struct Block {
    std::string type, text, toolName, filePath;
};

static void parseContentArray(Parser& p, std::vector<Block>& out) {
    p.parseArray([&]() {
        Block b;
        p.parseObject([&](const std::string& k) {
            if (k == "type") b.type = p.parseString();
            else if (k == "text") {
                b.text = p.parseString();
                if (b.text.size() > gMaxText) b.text = b.text.substr(0, gMaxText) + "\n...[截断]";
            }
            else if (k == "toolName") b.toolName = p.parseString();
            else if (k == "filePath") b.filePath = p.parseString();
            else if (k == "path") b.filePath = p.parseString();
            else if (k == "args") {
                if (!p.match('{')) { p.skipValue(); return; }
                // args 对象：只取 filePath
                if (p.match('}')) return;
                while (true) {
                    p.ws();
                    std::string ak = p.parseString();
                    p.expect(':');
                    if (ak == "filePath" || ak == "path" || ak == "target_file") {
                        if (b.filePath.empty()) b.filePath = p.parseString();
                        else p.skipValue();
                    } else p.skipValue();
                    if (p.match(',')) continue;
                    p.expect('}');
                    break;
                }
            }
            else p.skipValue();
        });
        out.push_back(std::move(b));
    });
}

static std::string sanitize(const std::string& name) {
    // 只保留 ASCII 安全字符，避免中文文件名在部分工具(PowerShell 枚举)下出问题
    std::string r;
    for (unsigned char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')
            r += char(c);
        else if (c >= 0x80) { /* 丢弃非 ASCII */ }
        else r += '_';
    }
    while (!r.empty() && (r.back() == ' ' || r.back() == '.')) r.pop_back();
    if (r.empty()) r = "conversation";
    // 限制长度（按字节粗略限制）
    if (r.size() > 60) r = r.substr(0, 60);
    return r;
}

struct Msg {
    std::string role, createdAt;
    std::vector<Block> blocks;
};

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <history.json> <outdir> [maxTextChars]\n", argv[0]);
        return 1;
    }
    const char* in = argv[1];
    const std::string outDir = argv[2];
    if (argc >= 4) gMaxText = strtoull(argv[3], nullptr, 10);
    if (argc >= 5) gMaxFile = strtoull(argv[4], nullptr, 10);

    std::string data;
    {
        std::ifstream f(in, std::ios::binary);
        if (!f) { fprintf(stderr, "cannot open %s\n", in); return 1; }
        f.seekg(0, std::ios::end);
        auto sz = (size_t)f.tellg();
        f.seekg(0, std::ios::beg);
        data.resize(sz);
        f.read(&data[0], (std::streamsize)sz);
        if (!data.empty() && (unsigned char)data[0] == 0xEF) data.erase(0, 3); // BOM
    }
    fprintf(stderr, "loaded %llu bytes\n", (unsigned long long)data.size());

    fs::create_directories(outDir);

    struct Conv {
        std::string id, name, createdAt, lastMessageAt, chatMode;
        std::vector<Msg> msgs;
        size_t requests = 0;
    };
    std::vector<Conv> convs;

    try {
        Parser p(data);
        p.parseObject([&](const std::string& topKey) {
            if (topKey != "data") { p.skipValue(); return; }
            p.parseObject([&](const std::string& k) {
                if (k != "conversations") { p.skipValue(); return; }
                p.parseArray([&]() {
                    Conv c;
                    p.parseObject([&](const std::string& ck) {
                        if (ck == "id") c.id = p.parseString();
                        else if (ck == "name") c.name = p.parseString();
                        else if (ck == "createdAt") c.createdAt = p.parseString();
                        else if (ck == "lastMessageAt") c.lastMessageAt = p.parseString();
                        else if (ck == "chatMode") c.chatMode = p.parseString();
                        else if (ck == "requests") {
                            p.parseArray([&]() {
                                ++c.requests;
                                p.parseObject([&](const std::string& rk) {
                                    if (rk != "messages") { p.skipValue(); return; }
                                    p.parseArray([&]() {
                                        Msg m;
                                        p.parseObject([&](const std::string& mk) {
                                            if (mk == "role") m.role = p.parseString();
                                            else if (mk == "createdAt") m.createdAt = p.parseString();
                                            else if (mk == "message") {
                                                std::string inner = p.parseString();
                                                try {
                                                    Parser ip(inner);
                                                    ip.parseObject([&](const std::string& k) {
                                                        if (k == "content" || k == "sourceContentBlocks") parseContentArray(ip, m.blocks);
                                                        else ip.skipValue();
                                                    });
                                                } catch (...) {}
                                            }
                                            else if (mk == "extra") {
                                                std::string inner = p.parseString();
                                                try {
                                                    Parser ip(inner);
                                                    ip.parseObject([&](const std::string& k) {
                                                        if (k == "sourceContentBlocks") parseContentArray(ip, m.blocks);
                                                        else ip.skipValue();
                                                    });
                                                } catch (...) {}
                                            }
                                            else p.skipValue();
                                        });
                                        if (!m.role.empty()) c.msgs.push_back(std::move(m));
                                    });
                                });
                            });
                        }
                        else p.skipValue();
                    });
                    convs.push_back(std::move(c));
                    fprintf(stderr, "parsed conversation %llu: %s (%llu msgs)\n",
                            (unsigned long long)convs.size(), c.name.c_str(), (unsigned long long)c.msgs.size());
                });
            });
        });
    } catch (const std::exception& e) {
        fprintf(stderr, "WARN: %s\n", e.what());
    }

    // 输出
    std::string index = "# history.json 精简索引\n\n";
    index += "- 会话数: " + std::to_string(convs.size()) + "\n\n";

    for (size_t ci = 0; ci < convs.size(); ++ci) {
        const Conv& c = convs[ci];
        char num[16];
        snprintf(num, sizeof(num), "%02llu", (unsigned long long)(ci + 1));
        std::string base = std::string(num) + "_" + sanitize(c.name);
        std::string header = "# " + c.name + "\n\n";
        header += "- 会话 ID: `" + c.id + "`\n";
        header += "- 创建: " + c.createdAt + "  最后活动: " + c.lastMessageAt + "\n";
        header += "- 请求数: " + std::to_string(c.requests) + "  消息数: " + std::to_string(c.msgs.size()) + "\n";
        if (!c.chatMode.empty()) header += "- chatMode: " + c.chatMode + "\n";
        header += "\n---\n\n";

        std::vector<std::string> parts;
        int partNo = 1;
        {
            std::string cur = header;
            for (const Msg& m : c.msgs) {
                std::string body;
                std::string label = (m.role == "user") ? "用户" : (m.role == "assistant" ? "助手" : m.role);
                body += "## [" + m.createdAt + "] " + label + "\n\n";
                for (const Block& b : m.blocks) {
                    if (b.type == "text" && !b.text.empty()) {
                        std::string t = b.text;
                        if (m.role == "user") {
                            auto p1 = t.find("<user_query>");
                            if (p1 != std::string::npos) {
                                p1 += std::string("<user_query>").size();
                                auto p2 = t.find("</user_query>", p1);
                                t = (p2 == std::string::npos) ? t.substr(p1) : t.substr(p1, p2 - p1);
                                while (!t.empty() && (t[0] == '\n' || t[0] == '\r')) t.erase(t.begin());
                            }
                        }
                        if (!t.empty()) body += t + "\n\n";
                    } else if (b.type == "tool-call") {
                        body += "`" + b.toolName + "`";
                        if (!b.filePath.empty()) body += " (" + b.filePath + ")";
                        body += "  ";
                    }
                }
                body += "\n";
                cur += body;
                if (cur.size() >= gMaxFile) {
                    char pn[16]; snprintf(pn, sizeof(pn), "_part%02d", partNo++);
                    parts.push_back(cur);
                    cur = header + "（接上一片）\n\n";
                }
            }
            if (parts.empty() || cur.size() > header.size() + 64) {
                char pn[16]; snprintf(pn, sizeof(pn), "_part%02d", partNo++);
                parts.push_back(cur);
            }
        }

        std::vector<std::string> written;
        for (size_t pi = 0; pi < parts.size(); ++pi) {
            char pn[16]; snprintf(pn, sizeof(pn), "_part%02d", int(pi + 1));
            std::string fn = base + pn + ".md";
            std::string path = (fs::path(outDir) / fn).string();
            std::ofstream f(path, std::ios::binary);
            f.write(parts[pi].data(), (std::streamsize)parts[pi].size());
            written.push_back(fn);
            fprintf(stderr, "wrote %s (%llu bytes)\n", fn.c_str(), (unsigned long long)parts[pi].size());
        }
        for (const std::string& fn : written)
            index += "- [" + fn + "](" + fn + ")\n";
        index += "  - " + c.name + "  \n  - " + c.createdAt + " → " + c.lastMessageAt +
                 "，消息 " + std::to_string(c.msgs.size()) + "\n";
    }

    {
        std::ofstream f((fs::path(outDir) / "index.md").string(), std::ios::binary);
        f.write(index.data(), (std::streamsize)index.size());
    }
    fprintf(stderr, "done.\n");
    return 0;
}
