// Built-in help pages (Menu -> Pomoc).
#pragma once
#include <vector>

namespace ui {

struct HelpTopic {
    const char* title;
    const char* text;  // paragraphs separated by '\n'; a paragraph starting with "– " is a list item
};

const std::vector<HelpTopic>& helpTopics();

}  // namespace ui
