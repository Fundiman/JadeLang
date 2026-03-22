#pragma once
#include <string>
#include <vector>

namespace jscc {

enum class TokenType {
    // literals
    INT_LIT, FLOAT_LIT, STRING_LIT, BOOL_LIT, NULL_LIT,

    // identifiers & keywords
    IDENT,
    KW_DEF, KW_VAL, KW_VAR, KW_RETURN, KW_WHEN, KW_IS,
    KW_CLASS, KW_DATA, KW_SEALED, KW_TRAIT, KW_IMPL,
    KW_IMPORT, KW_PACKAGE, KW_ASYNC, KW_AWAIT,
    KW_IF, KW_ELSE, KW_FOR, KW_WHILE, KW_IN,
    KW_TRUE, KW_FALSE, KW_NULL, KW_TYPE,

    // symbols
    LPAREN, RPAREN,     // ( )
    LBRACE, RBRACE,     // { }
    LBRACKET, RBRACKET, // [ ]
    LANGLE, RANGLE,     // < >
    COMMA, DOT, COLON, SEMICOLON,
    ARROW,              // ->
    FAT_ARROW,          // =>
    HASH,               // #
    AT,                 // @
    QUESTION,           // ?
    AMPERSAND,          // &

    // operators
    ASSIGN,             // =
    EQ, NEQ,            // == !=
    LT, GT, LTE, GTE,   // < > <= >=
    PLUS, MINUS, STAR, SLASH, PERCENT,
    AND, OR, NOT,       // && || !
    PLUS_ASSIGN, MINUS_ASSIGN, STAR_ASSIGN, SLASH_ASSIGN,

    // special
    NEWLINE,
    END_OF_FILE,
    UNKNOWN
};

struct Token {
    TokenType   type;
    std::string value;
    int         line;
    int         col;

    std::string to_string() const;
};

class Lexer {
public:
    explicit Lexer(const std::string& source, const std::string& filename = "<input>");

    std::vector<Token> tokenize();

private:
    std::string m_source;
    std::string m_filename;
    size_t      m_pos   = 0;
    int         m_line  = 1;
    int         m_col   = 1;

    char        peek(int offset = 0) const;
    char        advance();
    void        skip_whitespace_and_comments();

    Token       read_string();
    Token       read_number();
    Token       read_ident_or_keyword();
    Token       make_token(TokenType type, const std::string& value) const;

    static TokenType keyword_type(const std::string& word);
};

} // namespace jscc
