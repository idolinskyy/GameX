#include "core/expression.hpp"

#include <cctype>
#include <cmath>
#include <limits>

namespace gamex {
namespace {

struct Parser {
    std::string_view text;
    std::size_t pos = 0;
    const ExprVars* vars = nullptr;
    std::string* error = nullptr;
    bool ok = true;

    void fail(std::string_view message) {
        ok = false;
        if (error && error->empty()) {
            *error = std::string(message);
        }
    }

    void skip() {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
    }

    bool match_token(std::string_view token) {
        skip();
        if (text.substr(pos, token.size()) != token) {
            return false;
        }
        pos += token.size();
        return true;
    }

    bool match_word(std::string_view word) {
        skip();
        if (pos + word.size() > text.size()) {
            return false;
        }
        for (std::size_t i = 0; i < word.size(); ++i) {
            const unsigned char a = static_cast<unsigned char>(text[pos + i]);
            const unsigned char b = static_cast<unsigned char>(word[i]);
            if (std::toupper(a) != std::toupper(b)) {
                return false;
            }
        }
        const std::size_t next = pos + word.size();
        if (next < text.size()) {
            const unsigned char ch = static_cast<unsigned char>(text[next]);
            if (std::isalnum(ch) || ch == '_') {
                return false;
            }
        }
        pos = next;
        return true;
    }

    bool match_char(char ch) {
        skip();
        if (pos < text.size() && text[pos] == ch) {
            ++pos;
            return true;
        }
        return false;
    }

    double parse_primary() {
        skip();
        if (!ok) {
            return 0;
        }
        if (match_char('(')) {
            const double value = parse_or();
            if (!match_char(')')) {
                fail("Очікувалась ')'");
            }
            return value;
        }
        if (match_word("NOT") || match_char('~')) {
            return static_cast<double>(~static_cast<std::int64_t>(parse_primary()));
        }
        if (match_char('+')) {
            return parse_primary();
        }
        if (match_char('-')) {
            return -parse_primary();
        }
        if (pos < text.size() && (std::isalpha(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) {
            const std::size_t begin = pos;
            while (pos < text.size()) {
                const unsigned char ch = static_cast<unsigned char>(text[pos]);
                if (!std::isalnum(ch) && ch != '_') {
                    break;
                }
                ++pos;
            }
            const std::string name(text.substr(begin, pos - begin));
            if (vars != nullptr) {
                const auto it = vars->find(name);
                if (it != vars->end()) {
                    return it->second;
                }
            }
            fail("Невідома змінна: " + name);
            return 0;
        }
        if (pos >= text.size() ||
            !(std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '.')) {
            fail("Очікувалось число");
            return 0;
        }
        const std::size_t begin = pos;
        if (text.substr(pos, 2) == "0x" || text.substr(pos, 2) == "0X") {
            pos += 2;
            while (pos < text.size() && std::isxdigit(static_cast<unsigned char>(text[pos]))) {
                ++pos;
            }
            std::uint64_t value = 0;
            const auto part = text.substr(begin + 2, pos - begin - 2);
            for (char ch : part) {
                value <<= 4;
                if (ch >= '0' && ch <= '9') {
                    value += static_cast<std::uint64_t>(ch - '0');
                } else if (ch >= 'a' && ch <= 'f') {
                    value += static_cast<std::uint64_t>(ch - 'a' + 10);
                } else if (ch >= 'A' && ch <= 'F') {
                    value += static_cast<std::uint64_t>(ch - 'A' + 10);
                }
            }
            return static_cast<double>(value);
        }
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        if (pos < text.size() && (text[pos] == '.' || text[pos] == ',')) {
            ++pos;
            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
                ++pos;
            }
        }
        if (pos < text.size() && (text[pos] == 'h' || text[pos] == 'H')) {
            const auto digits = text.substr(begin, pos - begin);
            ++pos;
            std::uint64_t value = 0;
            for (char ch : digits) {
                if (!std::isxdigit(static_cast<unsigned char>(ch))) {
                    fail("Некоректне hex-число");
                    return 0;
                }
                value <<= 4;
                if (ch >= '0' && ch <= '9') {
                    value += static_cast<std::uint64_t>(ch - '0');
                } else if (ch >= 'a' && ch <= 'f') {
                    value += static_cast<std::uint64_t>(ch - 'a' + 10);
                } else if (ch >= 'A' && ch <= 'F') {
                    value += static_cast<std::uint64_t>(ch - 'A' + 10);
                }
            }
            return static_cast<double>(value);
        }
        std::string number(text.substr(begin, pos - begin));
        for (char& ch : number) {
            if (ch == ',') {
                ch = '.';
            }
        }
        try {
            return std::stod(number);
        } catch (...) {
            fail("Некоректне число");
            return 0;
        }
    }

    double parse_power() {
        double left = parse_primary();
        // без **; MUL/DIV наступний рівень
        return left;
    }

    double parse_mul() {
        double left = parse_power();
        for (;;) {
            skip();
            if (match_char('*')) {
                left *= parse_power();
            } else if (match_char('/')) {
                const double right = parse_power();
                if (right == 0) {
                    fail("Ділення на нуль");
                    return 0;
                }
                left /= right;
            } else if (match_word("MOD") || match_char('%')) {
                const double right = parse_power();
                if (right == 0) {
                    fail("MOD на нуль");
                    return 0;
                }
                left = static_cast<double>(static_cast<std::int64_t>(left) %
                                           static_cast<std::int64_t>(right));
            } else {
                break;
            }
        }
        return left;
    }

    double parse_add() {
        double left = parse_mul();
        for (;;) {
            skip();
            if (match_char('+')) {
                left += parse_mul();
            } else if (match_char('-')) {
                left -= parse_mul();
            } else {
                break;
            }
        }
        return left;
    }

    double parse_shift() {
        double left = parse_add();
        for (;;) {
            if (match_word("SHL") || match_token("<<")) {
                left = static_cast<double>(static_cast<std::int64_t>(left)
                                           << static_cast<std::int64_t>(parse_add()));
            } else if (match_word("SHR") || match_token(">>")) {
                left = static_cast<double>(static_cast<std::uint64_t>(left) >>
                                           static_cast<std::uint64_t>(parse_add()));
            } else {
                break;
            }
        }
        return left;
    }

    double parse_compare() {
        double left = parse_shift();
        for (;;) {
            skip();
            if (match_token("<>") || match_token("!=")) {
                const double right = parse_shift();
                left = left != right ? 1.0 : 0.0;
            } else if (match_token("<=")) {
                const double right = parse_shift();
                left = left <= right ? 1.0 : 0.0;
            } else if (match_token(">=")) {
                const double right = parse_shift();
                left = left >= right ? 1.0 : 0.0;
            } else if (match_token("==") || match_char('=')) {
                const double right = parse_shift();
                left = left == right ? 1.0 : 0.0;
            } else if (match_char('<')) {
                const double right = parse_shift();
                left = left < right ? 1.0 : 0.0;
            } else if (match_char('>')) {
                const double right = parse_shift();
                left = left > right ? 1.0 : 0.0;
            } else {
                break;
            }
        }
        return left;
    }

    double parse_and() {
        double left = parse_compare();
        for (;;) {
            if (match_word("AND") || match_char('&')) {
                if (match_char('&')) {
                    // &&
                }
                const double right = parse_compare();
                left = static_cast<double>(static_cast<std::int64_t>(left) &
                                           static_cast<std::int64_t>(right));
            } else {
                break;
            }
        }
        return left;
    }

    double parse_xor() {
        double left = parse_and();
        for (;;) {
            if (match_word("XOR") || match_char('^')) {
                const double right = parse_and();
                left = static_cast<double>(static_cast<std::int64_t>(left) ^
                                           static_cast<std::int64_t>(right));
            } else {
                break;
            }
        }
        return left;
    }

    double parse_or() {
        double left = parse_xor();
        for (;;) {
            if (match_word("OR") || match_char('|')) {
                if (match_char('|')) {
                    // ||
                }
                const double right = parse_xor();
                left = static_cast<double>(static_cast<std::int64_t>(left) |
                                           static_cast<std::int64_t>(right));
            } else {
                break;
            }
        }
        return left;
    }

    double parse_all() {
        const double value = parse_or();
        skip();
        if (ok && pos != text.size()) {
            fail("Зайві символи у виразі");
        }
        return value;
    }
};

bool has_operator(std::string_view text) {
    bool in_ident = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
            in_ident = true;
            continue;
        }
        if (in_ident && (std::isdigit(static_cast<unsigned char>(ch)))) {
            continue;
        }
        in_ident = false;
        if (ch == '+' || ch == '*' || ch == '/' || ch == '%' || ch == '(' || ch == ')' ||
            ch == '&' || ch == '|' || ch == '^' || ch == '~' || ch == '<' || ch == '>' ||
            ch == '=') {
            return true;
        }
        if (ch == '-' && i > 0) {
            std::size_t j = i;
            while (j > 0 && std::isspace(static_cast<unsigned char>(text[j - 1]))) {
                --j;
            }
            if (j > 0) {
                const char prev = text[j - 1];
                if (std::isdigit(static_cast<unsigned char>(prev)) || prev == ')' || prev == 'h' ||
                    prev == 'H' || std::isalpha(static_cast<unsigned char>(prev))) {
                    return true;
                }
            }
        }
    }
    // слова-оператори
    const std::string upper(text);
    auto contains = [&](std::string_view word) {
        std::string haystack;
        haystack.reserve(upper.size());
        for (char ch : upper) {
            haystack.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        }
        return haystack.find(word) != std::string::npos;
    };
    return contains("AND") || contains("OR") || contains("XOR") || contains("NOT") ||
           contains("SHL") || contains("SHR") || contains("MOD");
}

}  // namespace

bool looks_like_expression(std::string_view text) {
    return has_operator(text);
}

std::optional<double> eval_f64(std::string_view text, const ExprVars& vars, std::string* error) {
    if (error) {
        error->clear();
    }
    Parser parser;
    parser.text = text;
    parser.vars = &vars;
    parser.error = error;
    const double value = parser.parse_all();
    if (!parser.ok) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::int64_t> eval_i64(std::string_view text, const ExprVars& vars, std::string* error) {
    const auto value = eval_f64(text, vars, error);
    if (!value) {
        return std::nullopt;
    }
    if (!std::isfinite(*value)) {
        if (error) {
            *error = "Результат не є скінченним числом";
        }
        return std::nullopt;
    }
    return static_cast<std::int64_t>(std::llround(*value));
}

std::optional<bool> eval_bool(std::string_view text, const ExprVars& vars, std::string* error) {
    const auto value = eval_f64(text, vars, error);
    if (!value) {
        return std::nullopt;
    }
    return *value != 0.0;
}

}  // namespace gamex
