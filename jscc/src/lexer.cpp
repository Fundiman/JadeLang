#include "lexer.hpp"
#include "error.hpp"
#include <stdexcept>
#include <unordered_map>
#include <sstream>

namespace jscc {

static const std::unordered_map<std::string, TokenType> KEYWORDS = {
    {"def",    TokenType::KW_DEF},
    {"val",    TokenType::KW_VAL},
    {"var",    TokenType::KW_VAR},
    {"return", TokenType::KW_RETURN},
    {"when",   TokenType::KW_WHEN},
    {"is",     TokenType::KW_IS},
    {"class",  TokenType::KW_CLASS},
    {"data",   TokenType::KW_DATA},
    {"sealed", TokenType::KW_SEALED},
    {"trait",  TokenType::KW_TRAIT},
    {"impl",   TokenType::KW_IMPL},
    {"import", TokenType::KW_IMPORT},
    {"package",TokenType::KW_PACKAGE},
    {"async",  TokenType::KW_ASYNC},
    {"await",  TokenType::KW_AWAIT},
    {"if",     TokenType::KW_IF},
    {"else",   TokenType::KW_ELSE},
    {"for",    TokenType::KW_FOR},
    {"while",  TokenType::KW_WHILE},
    {"in",     TokenType::KW_IN},
    {"true",   TokenType::KW_TRUE},
    {"false",  TokenType::KW_FALSE},
    {"null",   TokenType::KW_NULL},
    {"type",   TokenType::KW_TYPE},
};

Lexer::Lexer(const std::string& source, const std::string& filename)
    : m_source(source), m_filename(filename) {}

char Lexer::peek(int offset) const {
    size_t idx = m_pos + offset;
    if (idx >= m_source.size()) return '\0';
    return m_source[idx];
}

char Lexer::advance() {
    char c = m_source[m_pos++];
    if (c == '\n') { m_line++; m_col = 1; }
    else           { m_col++; }
    return c;
}

Token Lexer::make_token(TokenType type, const std::string& value) const {
    return { type, value, m_line, m_col };
}

void Lexer::skip_whitespace_and_comments() {
    while (m_pos < m_source.size()) {
        char c = peek();

        // skip spaces and tabs (not newlines — they may matter later)
        if (c == ' ' || c == '\t' || c == '\r') {
            advance();
            continue;
        }

        // single line comment: // ...
        if (c == '/' && peek(1) == '/') {
            while (m_pos < m_source.size() && peek() != '\n')
                advance();
            continue;
        }

        // block comment: /* ... */
        if (c == '/' && peek(1) == '*') {
            advance(); advance(); // consume /*
            while (m_pos < m_source.size()) {
                if (peek() == '*' && peek(1) == '/') {
                    advance(); advance();
                    break;
                }
                advance();
            }
            continue;
        }

        break;
    }
}

Token Lexer::read_string() {
    int start_line = m_line;
    int start_col  = m_col;
    advance(); // consume opening "

    std::string value;
    while (m_pos < m_source.size() && peek() != '"') {
        if (peek() == '\\') {
            advance();
            switch (advance()) {
                case 'n':  value += '\n'; break;
                case 't':  value += '\t'; break;
                case '"':  value += '"';  break;
                case '\\': value += '\\'; break;
                default:   value += '?';  break;
            }
        } else {
            value += advance();
        }
    }

    if (m_pos >= m_source.size())
        throw std::runtime_error("unterminated string at line " + std::to_string(start_line));

    advance(); // consume closing "
    return { TokenType::STRING_LIT, value, start_line, start_col };
}

Token Lexer::read_number() {
    int start_line = m_line;
    int start_col  = m_col;
    std::string value;
    bool is_float = false;

    while (m_pos < m_source.size() && (std::isdigit(peek()) || peek() == '.')) {
        if (peek() == '.') {
            if (is_float) break; // second dot — stop
            is_float = true;
        }
        value += advance();
    }

    return { is_float ? TokenType::FLOAT_LIT : TokenType::INT_LIT, value, start_line, start_col };
}

Token Lexer::read_ident_or_keyword() {
    int start_line = m_line;
    int start_col  = m_col;
    std::string value;

    while (m_pos < m_source.size() && (std::isalnum(peek()) || peek() == '_'))
        value += advance();

    auto it = KEYWORDS.find(value);
    TokenType type = (it != KEYWORDS.end()) ? it->second : TokenType::IDENT;
    return { type, value, start_line, start_col };
}

TokenType Lexer::keyword_type(const std::string& word) {
    auto it = KEYWORDS.find(word);
    return (it != KEYWORDS.end()) ? it->second : TokenType::IDENT;
}

std::string Token::to_string() const {
    return "Token(" + value + " @ " + std::to_string(line) + ":" + std::to_string(col) + ")";
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        skip_whitespace_and_comments();

        if (m_pos >= m_source.size()) {
            tokens.push_back(make_token(TokenType::END_OF_FILE, ""));
            break;
        }

        int tok_line = m_line;
        int tok_col  = m_col;
        char c = peek();

        // newlines
        if (c == '\n') {
            advance();
            tokens.push_back({ TokenType::NEWLINE, "\\n", tok_line, tok_col });
            continue;
        }

        // string literal
        if (c == '"') {
            tokens.push_back(read_string());
            continue;
        }

        // number literal
        if (std::isdigit(c)) {
            tokens.push_back(read_number());
            continue;
        }

        // identifier or keyword
        if (std::isalpha(c) || c == '_') {
            tokens.push_back(read_ident_or_keyword());
            continue;
        }

        // symbols and operators
        advance();
        switch (c) {
            case '(': tokens.push_back({TokenType::LPAREN,    "(", tok_line, tok_col}); break;
            case ')': tokens.push_back({TokenType::RPAREN,    ")", tok_line, tok_col}); break;
            case '{': tokens.push_back({TokenType::LBRACE,    "{", tok_line, tok_col}); break;
            case '}': tokens.push_back({TokenType::RBRACE,    "}", tok_line, tok_col}); break;
            case '[': tokens.push_back({TokenType::LBRACKET,  "[", tok_line, tok_col}); break;
            case ']': tokens.push_back({TokenType::RBRACKET,  "]", tok_line, tok_col}); break;
            case ',': tokens.push_back({TokenType::COMMA,     ",", tok_line, tok_col}); break;
            case '.': tokens.push_back({TokenType::DOT,       ".", tok_line, tok_col}); break;
            case ':': tokens.push_back({TokenType::COLON,     ":", tok_line, tok_col}); break;
            case ';': tokens.push_back({TokenType::SEMICOLON, ";", tok_line, tok_col}); break;
            case '#': tokens.push_back({TokenType::HASH,      "#", tok_line, tok_col}); break;
            case '@': tokens.push_back({TokenType::AT,        "@", tok_line, tok_col}); break;
            case '?': tokens.push_back({TokenType::QUESTION,  "?", tok_line, tok_col}); break;
            case '&':
                if (peek() == '&') { advance(); tokens.push_back({TokenType::AND, "&&", tok_line, tok_col}); }
                else tokens.push_back({TokenType::AMPERSAND, "&", tok_line, tok_col});
                break;
            case '|':
                if (peek() == '|') { advance(); tokens.push_back({TokenType::OR, "||", tok_line, tok_col}); }
                else tokens.push_back({TokenType::UNKNOWN, "|", tok_line, tok_col});
                break;
            case '!':
                if (peek() == '=') { advance(); tokens.push_back({TokenType::NEQ,  "!=", tok_line, tok_col}); }
                else tokens.push_back({TokenType::NOT, "!", tok_line, tok_col});
                break;
            case '=':
                if (peek() == '=')      { advance(); tokens.push_back({TokenType::EQ,       "==", tok_line, tok_col}); }
                else if (peek() == '>') { advance(); tokens.push_back({TokenType::FAT_ARROW, "=>", tok_line, tok_col}); }
                else tokens.push_back({TokenType::ASSIGN, "=", tok_line, tok_col});
                break;
            case '<':
                if (peek() == '=') { advance(); tokens.push_back({TokenType::LTE,    "<=", tok_line, tok_col}); }
                else tokens.push_back({TokenType::LANGLE, "<", tok_line, tok_col});
                break;
            case '>':
                if (peek() == '=') { advance(); tokens.push_back({TokenType::GTE,    ">=", tok_line, tok_col}); }
                else tokens.push_back({TokenType::RANGLE, ">", tok_line, tok_col});
                break;
            case '+':
                if (peek() == '=') { advance(); tokens.push_back({TokenType::PLUS_ASSIGN,  "+=", tok_line, tok_col}); }
                else tokens.push_back({TokenType::PLUS, "+", tok_line, tok_col});
                break;
            case '-':
                if (peek() == '>') { advance(); tokens.push_back({TokenType::ARROW,         "->", tok_line, tok_col}); }
                else if (peek() == '=') { advance(); tokens.push_back({TokenType::MINUS_ASSIGN, "-=", tok_line, tok_col}); }
                else tokens.push_back({TokenType::MINUS, "-", tok_line, tok_col});
                break;
            case '*':
                if (peek() == '=') { advance(); tokens.push_back({TokenType::STAR_ASSIGN,  "*=", tok_line, tok_col}); }
                else tokens.push_back({TokenType::STAR, "*", tok_line, tok_col});
                break;
            case '/':
                if (peek() == '=') { advance(); tokens.push_back({TokenType::SLASH_ASSIGN, "/=", tok_line, tok_col}); }
                else tokens.push_back({TokenType::SLASH, "/", tok_line, tok_col});
                break;
            case '%': tokens.push_back({TokenType::PERCENT, "%", tok_line, tok_col}); break;
            default:  tokens.push_back({TokenType::UNKNOWN, std::string(1, c), tok_line, tok_col}); break;
        }
    }

    return tokens;
}

} // namespace jscc
