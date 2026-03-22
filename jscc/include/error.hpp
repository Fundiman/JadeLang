#pragma once
#include <string>
#include <vector>
#include <optional>

namespace jscc {

enum class ErrorLevel { ERROR, WARNING, NOTE, HELP };

struct Span {
    std::string filename;
    int         line;
    int         col_start;
    int         col_end;
};

struct Diagnostic {
    ErrorLevel               level;
    std::string              code;      // e.g. "JD031"
    std::string              message;
    Span                     span;
    std::optional<std::string> hint;   // the "help: ..." line
    std::optional<std::string> hint_replacement;
};

class ErrorReporter {
public:
    explicit ErrorReporter(const std::string& source, const std::string& filename);

    void report(const Diagnostic& diag);
    bool has_errors() const { return m_error_count > 0; }
    int  error_count() const { return m_error_count; }
    const std::string& filename() const { return m_filename; }

    // convenience helpers
    void error(const std::string& code, const std::string& msg, const Span& span,
               std::optional<std::string> hint = std::nullopt);
    void warning(const std::string& code, const std::string& msg, const Span& span,
                 std::optional<std::string> hint = std::nullopt);

private:
    std::string              m_source;
    std::string              m_filename;
    int                      m_error_count = 0;
    std::vector<std::string> m_lines;

    void        init_lines();
    std::string level_label(ErrorLevel level) const;
    std::string level_color(ErrorLevel level) const;
    std::string get_source_line(int line) const;
    std::string make_underline(int col_start, int col_end) const;

    static const char* RESET;
    static const char* RED;
    static const char* YELLOW;
    static const char* CYAN;
    static const char* GREEN;
    static const char* BOLD;
};

} // namespace jscc
