#include "error.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>

namespace jscc {

const char* ErrorReporter::RESET  = "\033[0m";
const char* ErrorReporter::RED    = "\033[31m";
const char* ErrorReporter::YELLOW = "\033[33m";
const char* ErrorReporter::CYAN   = "\033[36m";
const char* ErrorReporter::GREEN  = "\033[32m";
const char* ErrorReporter::BOLD   = "\033[1m";

ErrorReporter::ErrorReporter(const std::string& source, const std::string& filename)
    : m_source(source), m_filename(filename) {
    init_lines();
}

void ErrorReporter::init_lines() {
    std::istringstream stream(m_source);
    std::string line;
    while (std::getline(stream, line))
        m_lines.push_back(line);
}

std::string ErrorReporter::get_source_line(int line) const {
    if (line < 1 || line > (int)m_lines.size()) return "";
    return m_lines[line - 1];
}

std::string ErrorReporter::make_underline(int col_start, int col_end) const {
    std::string underline(col_start - 1, ' ');
    int len = std::max(1, col_end - col_start);
    underline += std::string(len, '^');
    return underline;
}

std::string ErrorReporter::level_label(ErrorLevel level) const {
    switch (level) {
        case ErrorLevel::ERROR:   return "error";
        case ErrorLevel::WARNING: return "warning";
        case ErrorLevel::NOTE:    return "note";
        case ErrorLevel::HELP:    return "help";
    }
    return "error";
}

std::string ErrorReporter::level_color(ErrorLevel level) const {
    switch (level) {
        case ErrorLevel::ERROR:   return RED;
        case ErrorLevel::WARNING: return YELLOW;
        case ErrorLevel::NOTE:    return CYAN;
        case ErrorLevel::HELP:    return GREEN;
    }
    return RED;
}

void ErrorReporter::report(const Diagnostic& diag) {
    if (diag.level == ErrorLevel::ERROR) m_error_count++;

    auto color = level_color(diag.level);
    auto label = level_label(diag.level);

    // error[JD031]: type mismatch
    std::cerr << BOLD << color
              << label << "[" << diag.code << "]"
              << RESET << BOLD
              << ": " << diag.message
              << RESET << "\n";

    // --> src/main.jsc:4:12
    std::cerr << CYAN << "  --> " << RESET
              << diag.span.filename << ":"
              << diag.span.line << ":"
              << diag.span.col_start << "\n";

    // source line + underline
    auto src_line = get_source_line(diag.span.line);
    if (!src_line.empty()) {
        auto line_str = std::to_string(diag.span.line);
        auto pad      = std::string(line_str.size(), ' ');

        std::cerr << CYAN << " " << pad << " |" << RESET << "\n";
        std::cerr << CYAN << " " << line_str << " |" << RESET
                  << " " << src_line << "\n";

        auto underline = make_underline(diag.span.col_start, diag.span.col_end);
        std::cerr << CYAN << " " << pad << " |" << RESET
                  << " " << color << underline << RESET << "\n";
    }

    // help: ...
    if (diag.hint) {
        std::cerr << GREEN << BOLD << "help" << RESET
                  << ": " << *diag.hint << "\n";
        if (diag.hint_replacement) {
            auto line_str = std::to_string(diag.span.line);
            auto pad      = std::string(line_str.size(), ' ');
            std::cerr << CYAN << " " << line_str << " |" << RESET
                      << " " << *diag.hint_replacement << "\n";
        }
    }

    std::cerr << "\n";
}

void ErrorReporter::error(const std::string& code, const std::string& msg,
                          const Span& span, std::optional<std::string> hint) {
    report({ ErrorLevel::ERROR, code, msg, span, hint, std::nullopt });
}

void ErrorReporter::warning(const std::string& code, const std::string& msg,
                            const Span& span, std::optional<std::string> hint) {
    report({ ErrorLevel::WARNING, code, msg, span, hint, std::nullopt });
}

} // namespace jscc
