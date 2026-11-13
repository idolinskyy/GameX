#pragma once

#include <cstddef>

namespace gamex {

struct HelpSection {
  const char *title;
  const char *html;
};

const HelpSection *help_sections();
std::size_t help_section_count();

} // namespace gamex
