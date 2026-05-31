#include "compiler/Macros.h"
#include <stdexcept>
#include <unordered_map>

namespace {

static std::string locOf(const Token& t) {
    if (!t.location.file || t.location.file->empty()) return "";
    return *t.location.file + ":" + std::to_string(t.location.line) + ":" + std::to_string(t.location.column) + ": ";
}

static std::string locAt(const std::vector<Token>& toks, size_t idx) {
    if (toks.empty()) return "";
    if (idx >= toks.size()) idx = toks.size() - 1;
    return locOf(toks[idx]);
}

struct TT {
    // A token tree is represented as a flat token list with balanced delimiters.
    std::vector<Token> toks;
};

enum class FragKind { TT, IDENT, EXPR, TYPE };

struct Matcher {
    enum class Kind { LIT, META, REPEAT };
    Kind kind;

    // LIT: token to match
    Token lit;

    // META: $name:frag
    std::string name;
    FragKind frag = FragKind::TT;

    // REPEAT: $( ... ) sep? quant
    std::vector<Matcher> children;
    Token sep;              // UNKNOWN => no separator
    char quant = '*';       // '*' or '+'
};

struct Rule {
    std::vector<Matcher> pattern;    // parsed matchers
    std::vector<Token> replacement;  // inside { ... }
};

static bool isOpenDelim(TokenType t) {
    return t == TokenType::LPAREN || t == TokenType::LBRACE;
}
static bool isCloseDelim(TokenType t) {
    return t == TokenType::RPAREN || t == TokenType::RBRACE;
}
static TokenType matchingClose(TokenType open) {
    if (open == TokenType::LPAREN) return TokenType::RPAREN;
    if (open == TokenType::LBRACE) return TokenType::RBRACE;
    return TokenType::UNKNOWN;
}

static std::vector<Token> readBalanced(const std::vector<Token>& tokens, size_t& i, TokenType open) {
    // i currently points at the open token.
    TokenType close = matchingClose(open);
    if (tokens[i].type != open) throw std::runtime_error(locOf(tokens[i]) + "expected opening delimiter");
    i++; // consume open
    int depth = 1;
    std::vector<Token> out;
    while (i < tokens.size()) {
        TokenType t = tokens[i].type;
        if (t == open) depth++;
        if (t == close) {
            depth--;
            if (depth == 0) {
                i++; // consume close
                return out;
            }
        }
        out.push_back(tokens[i]);
        i++;
    }
    throw std::runtime_error("unterminated delimiter group");
}

static FragKind parseFrag(const std::string& s) {
    if (s == "tt") return FragKind::TT;
    if (s == "ident") return FragKind::IDENT;
    if (s == "expr") return FragKind::EXPR;
    if (s == "type") return FragKind::TYPE;
    throw std::runtime_error("unknown macro fragment: " + s);
}

static bool isExprStarter(const Token& t) {
    return t.type == TokenType::NUMBER ||
           t.type == TokenType::IDENTIFIER ||
           t.type == TokenType::LPAREN ||
           t.type == TokenType::AMPERSAND ||
           t.type == TokenType::MULTIPLY;
}

static std::vector<Token> takeExprLike(const std::vector<Token>& input, size_t& in) {
    // Heuristic: consume tokens until we hit ',' at depth 0 or end.
    // Balanced parentheses/braces are preserved.
    std::vector<Token> out;
    int paren = 0, brace = 0;
    while (in < input.size()) {
        TokenType t = input[in].type;
        if (t == TokenType::COMMA && paren == 0 && brace == 0) break;
        if (t == TokenType::LPAREN) paren++;
        if (t == TokenType::RPAREN) paren--;
        if (t == TokenType::LBRACE) brace++;
        if (t == TokenType::RBRACE) brace--;
        out.push_back(input[in]);
        in++;
    }
    return out;
}

static std::vector<Token> takeTypeLike(const std::vector<Token>& input, size_t& in) {
    // Very small: keyword type or identifier, then optional '*'s.
    std::vector<Token> out;
    if (in >= input.size()) return out;
    if (input[in].type == TokenType::CHAR_TYPE ||
        input[in].type == TokenType::SHORT_TYPE ||
        input[in].type == TokenType::INT_TYPE ||
        input[in].type == TokenType::LONG_TYPE ||
        input[in].type == TokenType::VOID_TYPE ||
        input[in].type == TokenType::IDENTIFIER) {
        out.push_back(input[in++]);
        while (in < input.size() && input[in].type == TokenType::MULTIPLY) out.push_back(input[in++]);
    }
    return out;
}

static bool matchOneMeta(FragKind frag, const std::vector<Token>& input, size_t& in, std::vector<Token>& bound) {
    if (in >= input.size()) return false;
    if (frag == FragKind::IDENT) {
        if (input[in].type != TokenType::IDENTIFIER) return false;
        bound.push_back(input[in++]);
        return true;
    }
    if (frag == FragKind::TT) {
        if (isOpenDelim(input[in].type)) {
            TokenType open = input[in].type;
            TokenType close = matchingClose(open);
            bound.push_back(input[in++]);
            int depth = 1;
            while (in < input.size()) {
                TokenType t = input[in].type;
                bound.push_back(input[in]);
                if (t == open) depth++;
                if (t == close) {
                    depth--;
                    in++;
                    if (depth == 0) return true;
                    continue;
                }
                in++;
            }
            return false;
        }
        bound.push_back(input[in++]);
        return true;
    }
    if (frag == FragKind::EXPR) {
        if (!isExprStarter(input[in])) return false;
        bound = takeExprLike(input, in);
        return !bound.empty();
    }
    if (frag == FragKind::TYPE) {
        bound = takeTypeLike(input, in);
        return !bound.empty();
    }
    return false;
}

static bool matchMatchers(const std::vector<Matcher>& ms, const std::vector<Token>& input, size_t& in,
                          std::unordered_map<std::string, std::vector<std::vector<Token>>>& reps,
                          std::unordered_map<std::string, std::vector<Token>>& singles) {
    for (const auto& m : ms) {
        if (m.kind == Matcher::Kind::LIT) {
            if (in >= input.size()) return false;
            if (m.lit.type != input[in].type) return false;
            if (m.lit.type == TokenType::IDENTIFIER && m.lit.value != input[in].value) return false;
            if (m.lit.type == TokenType::NUMBER && m.lit.value != input[in].value) return false;
            in++;
            continue;
        }
        if (m.kind == Matcher::Kind::META) {
            std::vector<Token> bound;
            if (!matchOneMeta(m.frag, input, in, bound)) return false;
            singles[m.name] = bound;
            continue;
        }
        if (m.kind == Matcher::Kind::REPEAT) {
            int count = 0;
            while (true) {
                size_t save = in;
                std::unordered_map<std::string, std::vector<Token>> stepSingles;
                std::unordered_map<std::string, std::vector<std::vector<Token>>> stepReps;
                if (!matchMatchers(m.children, input, in, stepReps, stepSingles)) {
                    in = save;
                    break;
                }
                // record
                for (auto& kv : stepSingles) {
                    reps[kv.first].push_back(kv.second);
                }
                count++;
                if (m.sep.type != TokenType::UNKNOWN) {
                    if (in < input.size() && input[in].type == m.sep.type) {
                        in++; // consume sep
                    } else {
                        break;
                    }
                }
            }
            if (m.quant == '+' && count == 0) return false;
            continue;
        }
    }
    return true;
}

static bool matchPattern(const std::vector<Matcher>& pattern, const std::vector<Token>& input,
                         std::unordered_map<std::string, std::vector<std::vector<Token>>>& reps,
                         std::unordered_map<std::string, std::vector<Token>>& singles) {
    size_t in = 0;
    if (!matchMatchers(pattern, input, in, reps, singles)) return false;
    return in == input.size();
}

static std::vector<Token> substitute(const std::vector<Token>& repl,
                                     const std::unordered_map<std::string, std::vector<Token>>& singles,
                                     const std::unordered_map<std::string, std::vector<std::vector<Token>>>& reps,
                                     int gensymId) {
    std::vector<Token> out;
    for (size_t i = 0; i < repl.size(); ++i) {
        if (repl[i].type == TokenType::DOLLAR) {
            if (i + 1 < repl.size() && repl[i + 1].type == TokenType::IDENTIFIER) {
                auto itS = singles.find(repl[i + 1].value);
                if (itS != singles.end()) {
                    out.insert(out.end(), itS->second.begin(), itS->second.end());
                    i++;
                    continue;
                }
                auto itR = reps.find(repl[i + 1].value);
                if (itR != reps.end()) {
                    // Splice all captures, separated by commas.
                    bool first = true;
                    for (const auto& cap : itR->second) {
                        if (!first) out.push_back({TokenType::COMMA, ","});
                        first = false;
                        out.insert(out.end(), cap.begin(), cap.end());
                    }
                    i++;
                    continue;
                }
            } else if (i + 2 < repl.size() && repl[i+1].type == TokenType::BANG && repl[i+2].type == TokenType::IDENTIFIER) {
                // $!stringify($name)
                if (repl[i+2].value == "stringify") {
                    i += 3; // consume $ ! stringify
                    if (i < repl.size() && repl[i].type == TokenType::LPAREN) {
                        size_t start = i;
                        auto inner = readBalanced(repl, i, TokenType::LPAREN);
                        std::string res;
                        for (const auto& t : inner) {
                           if (t.type == TokenType::DOLLAR && i + 1 < inner.size()) {
                               // recursive lookup? no, just simple for now.
                           }
                           res += t.value;
                        }
                        out.push_back({TokenType::STRING_LITERAL, res});
                        continue;
                    }
                }
            }
        }
        // Hygiene: identifiers starting with "__" get a unique suffix per expansion.
        if (repl[i].type == TokenType::IDENTIFIER) {
            const std::string& v = repl[i].value;
            if (v.rfind("__", 0) == 0) {
                out.push_back({TokenType::IDENTIFIER, v + "_" + std::to_string(gensymId)});
                continue;
            }
        }
        out.push_back(repl[i]);
    }
    return out;
}

static std::vector<Matcher> parseMatchers(const std::vector<Token>& toks, size_t& i) {
    std::vector<Matcher> ms;
    while (i < toks.size()) {
        if (toks[i].type == TokenType::DOLLAR && i + 1 < toks.size() && toks[i + 1].type == TokenType::LPAREN) {
            // repetition
            i += 1; // to '('
            auto inner = readBalanced(toks, i, TokenType::LPAREN); // consumes '(' ... ')'
            size_t ii = 0;
            auto children = parseMatchers(inner, ii);

            Token sep{TokenType::UNKNOWN, ""};
            char quant = '*';
            if (i < toks.size() && (toks[i].type == TokenType::COMMA || toks[i].type == TokenType::SEMICOLON)) {
                // separator before quant, e.g. $(... ),*
                sep = toks[i];
                i++;
            }
            if (i < toks.size() && toks[i].type == TokenType::MULTIPLY) { quant = '*'; i++; }
            else if (i < toks.size() && toks[i].type == TokenType::PLUS) { quant = '+'; i++; }
            else {
                throw std::runtime_error(locOf(toks[i]) + "expected '*' or '+' after macro repetition");
            }

            Matcher m;
            m.kind = Matcher::Kind::REPEAT;
            m.children = std::move(children);
            m.sep = sep;
            m.quant = quant;
            ms.push_back(std::move(m));
            continue;
        }
        if (toks[i].type == TokenType::DOLLAR) {
            if (i + 1 >= toks.size() || toks[i + 1].type != TokenType::IDENTIFIER) {
                throw std::runtime_error(locOf(toks[i]) + "expected identifier after '$' in macro pattern");
            }
            std::string name = toks[i + 1].value;
            i += 2;
            FragKind frag = FragKind::TT;
            if (i + 1 < toks.size() && toks[i].type == TokenType::COLON && toks[i + 1].type == TokenType::IDENTIFIER) {
                frag = parseFrag(toks[i + 1].value);
                i += 2;
            }
            Matcher m;
            m.kind = Matcher::Kind::META;
            m.name = name;
            m.frag = frag;
            ms.push_back(std::move(m));
            continue;
        }
        Matcher lit;
        lit.kind = Matcher::Kind::LIT;
        lit.lit = toks[i];
        ms.push_back(std::move(lit));
        i++;
    }
    return ms;
}

} // namespace

std::vector<Token> expandMacros(const std::vector<Token>& tokens) {
    std::unordered_map<std::string, std::vector<Rule>> macros;
    std::vector<Token> withoutDefs;

    // 1) Collect macro_rules! definitions and remove them from the stream.
    for (size_t i = 0; i < tokens.size();) {
        if (tokens[i].type == TokenType::MACRO_RULES) {
            i++;
            if (i >= tokens.size() || tokens[i].type != TokenType::BANG) throw std::runtime_error(locAt(tokens, i - 1) + "expected '!' after macro_rules");
            i++;
            if (i >= tokens.size() || tokens[i].type != TokenType::IDENTIFIER) throw std::runtime_error(locAt(tokens, i) + "expected macro name");
            std::string name = tokens[i].value;
            i++;
            if (i >= tokens.size() || tokens[i].type != TokenType::LBRACE) throw std::runtime_error(locAt(tokens, i) + "expected '{' after macro name");

            // Parse rules inside { ... }
            auto body = readBalanced(tokens, i, TokenType::LBRACE);
            std::vector<Rule> rules;

            for (size_t b = 0; b < body.size();) {
                if (b < body.size() && body[b].type == TokenType::SEMICOLON) { b++; continue; }
                if (b >= body.size()) break;

                if (body[b].type != TokenType::LPAREN) throw std::runtime_error(locOf(body[b]) + "expected '(pattern)' in macro_rules");
                size_t bi = b;
                auto pat = readBalanced(body, bi, TokenType::LPAREN);
                b = bi;

                if (b >= body.size() || body[b].type != TokenType::FAT_ARROW) throw std::runtime_error(locAt(body, b) + "expected '=>' in macro_rules");
                b++;
                if (b >= body.size() || body[b].type != TokenType::LBRACE) throw std::runtime_error(locAt(body, b) + "expected '{replacement}' in macro_rules");
                size_t bj = b;
                auto rep = readBalanced(body, bj, TokenType::LBRACE);
                b = bj;

                if (b < body.size() && body[b].type == TokenType::SEMICOLON) b++;
                size_t pi = 0;
                auto parsed = parseMatchers(pat, pi);
                rules.push_back({std::move(parsed), std::move(rep)});
            }

            macros[name] = std::move(rules);
            continue;
        }

        withoutDefs.push_back(tokens[i]);
        i++;
    }

    // 2) Expand invocations. We iterate until no changes or limit reached.
    std::vector<Token> cur = std::move(withoutDefs);
    for (int pass = 0; pass < 32; ++pass) {
        bool changed = false;
        std::vector<Token> out;
        for (size_t i = 0; i < cur.size();) {
            if (cur[i].type == TokenType::IDENTIFIER &&
                i + 2 < cur.size() &&
                cur[i + 1].type == TokenType::BANG &&
                cur[i + 2].type == TokenType::LPAREN) {
                std::string name = cur[i].value;
                auto it = macros.find(name);
                if (it != macros.end()) {
                    size_t j = i + 2;
                    // Capture invocation tokens inside ( ... )
                    auto inner = readBalanced(cur, j, TokenType::LPAREN);
                    bool matched = false;
                    for (const Rule& r : it->second) {
                        std::unordered_map<std::string, std::vector<Token>> singles;
                        std::unordered_map<std::string, std::vector<std::vector<Token>>> reps;
                        if (matchPattern(r.pattern, inner, reps, singles)) {
                            int gensymId = pass * 100000 + (int)i;
                            auto expanded = substitute(r.replacement, singles, reps, gensymId);
                            out.insert(out.end(), expanded.begin(), expanded.end());
                            matched = true;
                            changed = true;
                            break;
                        }
                    }
                    if (!matched) {
                        throw std::runtime_error(locOf(cur[i]) + "no matching macro_rules pattern for invocation: " + name);
                    }
                    i = j;
                    continue;
                }
            }

            out.push_back(cur[i]);
            i++;
        }
        cur = std::move(out);
        if (!changed) break;
    }

    return cur;
}
