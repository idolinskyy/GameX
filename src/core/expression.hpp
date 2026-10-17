#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace gamex {

using ExprVars = std::unordered_map<std::string, double>;

bool looks_like_expression(std::string_view text);

// Цілочисельний результат (побітові операції — на int64).
std::optional<std::int64_t> eval_i64(std::string_view text, const ExprVars& vars, std::string* error);

// Дробовий результат; для чисто цілих виразів теж працює.
std::optional<double> eval_f64(std::string_view text, const ExprVars& vars, std::string* error);

// Логічний вираз для відсіву по знімках: (M1<>M2)and(M1=M3). 0 = false, інакше true.
std::optional<bool> eval_bool(std::string_view text, const ExprVars& vars, std::string* error);

}  // namespace gamex
