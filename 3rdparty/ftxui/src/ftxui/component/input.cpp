// Copyright 2022 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <algorithm>   // for max, min
#include <cstddef>     // for size_t
#include <cstdint>     // for uint32_t
#include <functional>  // for function
#include <sstream>     // for basic_istream, stringstream
#include <string>      // for string, basic_string, operator==, getline
#include <utility>     // for move
#include <vector>      // for vector

#include "ftxui/component/app.hpp"                // for Component
#include "ftxui/component/component.hpp"          // for Make, Input
#include "ftxui/component/component_base.hpp"     // for ComponentBase
#include "ftxui/component/component_options.hpp"  // for InputOption
#include "ftxui/component/event.hpp"  // for Event, Event::ArrowDown, Event::ArrowLeft, Event::ArrowLeftCtrl, Event::ArrowRight, Event::ArrowRightCtrl, Event::ArrowUp, Event::Backspace, Event::Delete, Event::End, Event::Home, Event::Return
#include "ftxui/component/mouse.hpp"  // for Mouse, Mouse::Left, Mouse::Pressed
#include "ftxui/dom/elements.hpp"  // for operator|, reflect, text, Element, xflex, hbox, Elements, frame, operator|=, vbox, focus, focusCursorBarBlinking, select
#include "ftxui/screen/box.hpp"    // for Box
#include "ftxui/screen/string.hpp"           // for string_width
#include "ftxui/screen/string_internal.hpp"  // for GlyphNext, GlyphPrevious, WordBreakProperty, EatCodePoint, CodepointToWordBreakProperty, IsFullWidth, WordBreakProperty::ALetter, WordBreakProperty::CR, WordBreakProperty::Double_Quote, WordBreakProperty::Extend, WordBreakProperty::ExtendNumLet, WordBreakProperty::Format, WordBreakProperty::Hebrew_Letter, WordBreakProperty::Katakana, WordBreakProperty::LF, WordBreakProperty::MidLetter, WordBreakProperty::MidNum, WordBreakProperty::MidNumLet, WordBreakProperty::Newline, WordBreakProperty::Numeric, WordBreakProperty::Regional_Indicator, WordBreakProperty::Single_Quote, WordBreakProperty::WSegSpace, WordBreakProperty::ZWJ
#include "ftxui/screen/util.hpp"             // for clamp
#include "ftxui/util/ref.hpp"                // for StringRef, Ref

namespace ftxui {

namespace {

std::vector<std::string> SplitLines(std::string_view input) {
  std::vector<std::string> output;
  size_t start = 0;
  size_t end = input.find('\n');
  while (end != std::string_view::npos) {
    output.push_back(std::string(input.substr(start, end - start)));
    start = end + 1;
    end = input.find('\n', start);
  }
  output.push_back(std::string(input.substr(start)));
  return output;
}

size_t GlyphWidth(std::string_view input, size_t iter) {
  uint32_t ucs = 0;
  if (!EatCodePoint(input, iter, &iter, &ucs)) {
    return 0;
  }
  if (IsFullWidth(ucs)) {
    return 2;
  }
  return 1;
}

bool IsWordCodePoint(uint32_t codepoint) {
  switch (CodepointToWordBreakProperty(codepoint)) {
    case WordBreakProperty::ALetter:
    case WordBreakProperty::Hebrew_Letter:
    case WordBreakProperty::Katakana:
    case WordBreakProperty::Numeric:
      return true;

    case WordBreakProperty::CR:
    case WordBreakProperty::Double_Quote:
    case WordBreakProperty::LF:
    case WordBreakProperty::MidLetter:
    case WordBreakProperty::MidNum:
    case WordBreakProperty::MidNumLet:
    case WordBreakProperty::Newline:
    case WordBreakProperty::Single_Quote:
    case WordBreakProperty::WSegSpace:
    // Unexpected/Unsure
    case WordBreakProperty::Extend:
    case WordBreakProperty::ExtendNumLet:
    case WordBreakProperty::Format:
    case WordBreakProperty::Regional_Indicator:
    case WordBreakProperty::ZWJ:
      return false;
  }
  return false;  // NOT_REACHED();
}

bool IsWordCharacter(std::string_view input, size_t iter) {
  uint32_t ucs = 0;
  if (!EatCodePoint(input, iter, &iter, &ucs)) {
    return false;
  }

  return IsWordCodePoint(ucs);
}

// An input box. The user can type text into it.
class InputBase : public ComponentBase, public InputOption {
 public:
  // NOLINTNEXTLINE
  InputBase(InputOption option) : InputOption(std::move(option)) {}

 private:
  // Component implementation:
  Element OnRender() override {
    const bool is_focused = Focused();
    // CLFCode patch（2026-09-08）：Blinking → 稳态形状。原 BlockBlinking 让
    // App::Draw 每帧输出 "\033[?25h\033[5 q"（DECSCUSR 闪烁块），conhost 在
    // 每次 ?25h 时重置光标 blink 相位——闪烁节奏被帧率绑架（打字每字符一帧、
    // 定时器每秒 PostEvent），光标呈异常快闪；帧率低于 blink 周期时又几乎
    // 不闪（JediTerm 类终端支持差异更大）。稳态块（\033[2 q）无相位可重置，
    // 光标表现与终端帧率解耦，IME 组合窗口定位逻辑（?25h 与光标移动）不变。
    const auto focused = (!is_focused && !hovered_) ? focus
                         : insert()                 ? focusCursorBar
                                                    : focusCursorBlock;

    auto transform_func =
        transform ? transform : InputOption::Default().transform;

    // placeholder.
    if (content->empty()) {
      // CLFCode patch（2026-09-08）：focused 原装饰整个 placeholder 文本——
      // 焦点 box 落在 placeholder 起点（"❯" 字符处），真实光标与 IME 组合
      // 窗口（输入法激活、文本尚未上屏时输入框仍空）都被定位在 "❯" 上，
      // 与每帧重绘的 placeholder 冲突产生残影抖动。焦点改放末尾空格：
      // 光标/组合窗口定位到 placeholder 之后 = 用户输入首字符的预期位置。
      auto element = hbox({
                         text(placeholder()),
                         text(" ") | focused,
                     }) |
                     xflex | frame;

      return transform_func({
                 std::move(element), hovered_, is_focused,
                 true  // placeholder
             }) |
             reflect(box_);
    }

    Elements elements;
    const std::vector<std::string> lines = SplitLines(*content);

    cursor_position() = util::clamp(cursor_position(), 0, (int)content->size());

    // Find the line and index of the cursor.
    int cursor_line = 0;
    int cursor_char_index = cursor_position();
    for (const auto& line : lines) {
      if (cursor_char_index <= (int)line.size()) {
        break;
      }

      cursor_char_index -= static_cast<int>(line.size() + 1);
      cursor_line++;
    }

    if (lines.empty()) {
      elements.push_back(text("") | focused);
    }

    elements.reserve(lines.size());
    // CLFCode patch（2026-09-23）：行渲染走通用拆段（选区反色 + 光标装饰
    // 叠加）——原三分支（非光标行 / 行尾光标 / 行内光标）语义逐一保真
    int line_offset = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
      const std::string& line = lines[i];

      if (int(i) != cursor_line) {
        AddLineWithDecorations(elements, line, line_offset, 0, 0,
                               false, is_focused, focused);
      } else if (cursor_char_index >= (int)line.size()) {
        // 光标在行尾（原语义：聚焦时空格 cell 承载光标框）
        AddLineWithDecorations(elements, line, line_offset,
                               static_cast<int>(line.size()),
                               static_cast<int>(line.size()),
                               true, is_focused, focused);
      } else {
        // 光标在行内（原语义：光标 glyph 承载光标框）
        const int glyph_start = cursor_char_index;
        const int glyph_end = static_cast<int>(GlyphNext(line, glyph_start));
        AddLineWithDecorations(elements, line, line_offset,
                               glyph_start, glyph_end, true, is_focused,
                               focused);
      }
      line_offset += static_cast<int>(line.size()) + 1;
    }

    auto element = vbox(std::move(elements)) | frame;
    return transform_func({
               std::move(element), hovered_, is_focused,
               false  // placeholder
           }) |
           xflex | reflect(box_);
  }

  Element Text(const std::string& input) {
    if (!password()) {
      return text(input);
    }

    const size_t glyph_count = GlyphCount(input);
    std::string out;
    out.reserve(glyph_count * 3);
    for (size_t i = 0; i < glyph_count; ++i) {
      out += "•";
    }
    return text(out);
  }

  // CLFCode patch（2026-09-23）：带选区高亮与光标装饰的行渲染——按字节
  // 边界通用拆段：选区区间反色（inverted）、光标 cell 保持 focused +
  // reflect(cursor_box_)（IME 组合窗口定位依赖 cursor_box_）。无选区无
  // 光标的行走原样 Text(line) 快速路径。
  void AddLineWithDecorations(Elements& elements, const std::string& line,
                              int line_start, int cursor_begin_in_line,
                              int cursor_end_in_line, bool has_cursor,
                              bool is_focused, Decorator cursor_dec) {
    // 选区在本行的交集（字节区间；无选区 → 空）
    int sel_begin = -1, sel_end = -1;
    if (selection_begin_ >= 0 && selection_end_ > selection_begin_) {
      const int s = std::min(selection_begin_, selection_end_) - line_start;
      const int e = std::max(selection_begin_, selection_end_) - line_start;
      sel_begin = std::max(s, 0);
      sel_end   = std::min(e, static_cast<int>(line.size()));
      if (sel_begin >= sel_end) {
        sel_begin = -1;
        sel_end = -1;
      }
    }
    if (sel_begin < 0 && !has_cursor) {
      elements.push_back(Text(line));
      return;
    }
    // 边界集合拆段：选区反色与光标装饰可叠加
    struct Mark {
      int pos;
      bool sel_toggle;
      bool cur_toggle;
    };
    std::vector<Mark> marks{{0, false, false}};
    if (sel_begin >= 0) {
      marks.push_back({sel_begin, true, false});
      marks.push_back({sel_end, false, false});
    }
    if (has_cursor) {
      marks.push_back({cursor_begin_in_line, false, true});
      marks.push_back({cursor_end_in_line, false, false});
    }
    std::stable_sort(marks.begin(), marks.end(),
                     [](const Mark& a, const Mark& b) { return a.pos < b.pos; });
    Elements segments;
    bool sel_on = false, cur_on = false;
    for (size_t k = 0; k < marks.size(); ++k) {
      if (marks[k].sel_toggle) sel_on = !sel_on;
      if (marks[k].cur_toggle) cur_on = !cur_on;
      const int to = (k + 1 < marks.size())
                         ? marks[k + 1].pos
                         : static_cast<int>(line.size());
      if (to < marks[k].pos) continue;
      if (to == marks[k].pos) {
        // 空段 = 行尾光标空格 cell（仅聚焦时渲染；IME 定位依赖）
        if (cur_on && is_focused) {
          segments.push_back(text(" ") | cursor_dec | reflect(cursor_box_));
        }
        continue;
      }
      Element el = Text(line.substr(marks[k].pos, to - marks[k].pos));
      if (sel_on) el = el | inverted;
      if (cur_on) el = el | cursor_dec | reflect(cursor_box_);
      segments.push_back(std::move(el));
    }
    elements.push_back(hbox(std::move(segments)) | xflex);
  }

  bool HandleBackspace() {
    if (cursor_position() == 0) {
      return false;
    }
    const size_t start = GlyphPrevious(content(), cursor_position());
    const size_t end = cursor_position();
    content->erase(start, end - start);
    cursor_position() = static_cast<int>(start);
    App::PostEventOrExecute(on_change);
    return true;
  }

  bool DeleteImpl() {
    if (cursor_position() == (int)content->size()) {
      return false;
    }
    const size_t start = cursor_position();
    const size_t end = GlyphNext(content(), cursor_position());
    content->erase(start, end - start);
    return true;
  }

  bool HandleDelete() {
    if (DeleteImpl()) {
      App::PostEventOrExecute(on_change);
      return true;
    }
    return false;
  }

  bool HandleArrowLeft() {
    if (cursor_position() == 0) {
      return false;
    }

    cursor_position() =
        static_cast<int>(GlyphPrevious(content(), cursor_position()));
    return true;
  }

  bool HandleArrowRight() {
    if (cursor_position() == (int)content->size()) {
      return false;
    }

    cursor_position() =
        static_cast<int>(GlyphNext(content(), cursor_position()));
    return true;
  }

  size_t CursorColumn() {
    size_t iter = cursor_position();
    int width = 0;
    while (true) {
      if (iter == 0) {
        break;
      }
      iter = GlyphPrevious(content(), iter);
      if (content()[iter] == '\n') {
        break;
      }
      if (password()) {
        width += 1;
      } else {
        width += static_cast<int>(GlyphWidth(content(), iter));
      }
    }
    return width;
  }

  // Move the cursor `columns` on the right, if possible.
  void MoveCursorColumn(int columns) {
    while (columns > 0) {
      if (cursor_position() == (int)content().size() ||
          content()[cursor_position()] == '\n') {
        return;
      }

      if (password()) {
        columns -= 1;
      } else {
        columns -= static_cast<int>(GlyphWidth(content(), cursor_position()));
      }
      cursor_position() =
          static_cast<int>(GlyphNext(content(), cursor_position()));
    }
  }

  bool HandleArrowUp() {
    if (cursor_position() == 0) {
      return false;
    }

    const size_t columns = CursorColumn();

    // Move cursor at the beginning of 2 lines above.
    while (true) {
      if (cursor_position() == 0) {
        return true;
      }
      const size_t previous = GlyphPrevious(content(), cursor_position());
      if (content()[previous] == '\n') {
        break;
      }
      cursor_position() = static_cast<int>(previous);
    }
    cursor_position() =
        static_cast<int>(GlyphPrevious(content(), cursor_position()));
    while (true) {
      if (cursor_position() == 0) {
        break;
      }
      const size_t previous = GlyphPrevious(content(), cursor_position());
      if (content()[previous] == '\n') {
        break;
      }
      cursor_position() = static_cast<int>(previous);
    }

    MoveCursorColumn(static_cast<int>(columns));
    return true;
  }

  bool HandleArrowDown() {
    if (cursor_position() == (int)content->size()) {
      return false;
    }

    const size_t columns = CursorColumn();

    // Move cursor at the beginning of the next line
    while (true) {
      if (content()[cursor_position()] == '\n') {
        break;
      }
      cursor_position() =
          static_cast<int>(GlyphNext(content(), cursor_position()));
      if (cursor_position() == (int)content().size()) {
        return true;
      }
    }
    cursor_position() =
        static_cast<int>(GlyphNext(content(), cursor_position()));

    MoveCursorColumn(static_cast<int>(columns));
    return true;
  }

  bool HandleHome() {
    cursor_position() = 0;
    return true;
  }

  bool HandleEnd() {
    cursor_position() = static_cast<int>(content->size());
    return true;
  }

  bool HandleReturn() {
    if (multiline()) {
      HandleCharacter("\n");
    }
    App::PostEventOrExecute(on_enter);
    return true;
  }

  bool HandleCharacter(const std::string& character) {
    if (!insert() && cursor_position() < (int)content->size() &&
        content()[cursor_position()] != '\n') {
      DeleteImpl();
    }
    content->insert(cursor_position(), character);
    cursor_position() += static_cast<int>(character.size());
    App::PostEventOrExecute(on_change);
    return true;
  }

  bool OnEvent(Event event) override {
    cursor_position() = util::clamp(cursor_position(), 0, (int)content->size());

    // CLFCode patch（2026-09-23）：非鼠标事件清除拖选选区（编辑动作/光标
    // 移动后选区失效——编辑器惯例；鼠标事件走 HandleMouse 自行管理）
    if (!event.is_mouse() && selection_begin_ >= 0) {
      ClearSelection();
    }

    if (event == Event::Return) {
      return HandleReturn();
    }
    if (event.is_character()) {
      return HandleCharacter(event.character());
    }
    if (event.is_mouse()) {
      return HandleMouse(event);
    }
    if (event == Event::Backspace) {
      return HandleBackspace();
    }
    if (event == Event::Delete) {
      return HandleDelete();
    }
    if (event == Event::ArrowLeft) {
      return HandleArrowLeft();
    }
    if (event == Event::ArrowRight) {
      return HandleArrowRight();
    }
    if (event == Event::ArrowUp) {
      return HandleArrowUp();
    }
    if (event == Event::ArrowDown) {
      return HandleArrowDown();
    }
    if (event == Event::Home) {
      return HandleHome();
    }
    if (event == Event::End) {
      return HandleEnd();
    }
    if (event == Event::ArrowLeftCtrl) {
      return HandleLeftCtrl();
    }
    if (event == Event::ArrowRightCtrl) {
      return HandleRightCtrl();
    }
    if (event == Event::Insert) {
      return HandleInsert();
    }
    return false;
  }

  bool HandleLeftCtrl() {
    if (cursor_position() == 0) {
      return false;
    }

    // Move left, as long as left it not a word.
    while (cursor_position()) {
      const size_t previous = GlyphPrevious(content(), cursor_position());
      if (IsWordCharacter(content(), previous)) {
        break;
      }
      cursor_position() = static_cast<int>(previous);
    }
    // Move left, as long as left is a word character:
    while (cursor_position()) {
      const size_t previous = GlyphPrevious(content(), cursor_position());
      if (!IsWordCharacter(content(), previous)) {
        break;
      }
      cursor_position() = static_cast<int>(previous);
    }
    return true;
  }

  bool HandleRightCtrl() {
    if (cursor_position() == (int)content().size()) {
      return false;
    }

    // Move right, until entering a word.
    while (cursor_position() < (int)content().size()) {
      cursor_position() =
          static_cast<int>(GlyphNext(content(), cursor_position()));
      if (IsWordCharacter(content(), cursor_position())) {
        break;
      }
    }
    // Move right, as long as right is a word character:
    while (cursor_position() < (int)content().size()) {
      const size_t next = GlyphNext(content(), cursor_position());
      if (!IsWordCharacter(content(), cursor_position())) {
        break;
      }
      cursor_position() = static_cast<int>(next);
    }

    return true;
  }

  // CLFCode patch（2026-09-23）：把组件内坐标 (mouse_x, mouse_y) 换算为光标
  // 字节偏移——自原 HandleMouse 点击定位逻辑原样抽取（列→字节与点击定位
  // 同源，拖选换算复用同一口径：多行折行/宽字符宽度天然一致）。
  int PositionToCursor(int mouse_x, int mouse_y) {
    if (content->empty()) {
      return 0;
    }

    // Find the line and index of the cursor.
    std::vector<std::string> lines = SplitLines(*content);
    int cursor_line = 0;
    int cursor_char_index = cursor_position();
    for (const auto& line : lines) {
      if (cursor_char_index <= (int)line.size()) {
        break;
      }

      cursor_char_index -= static_cast<int>(line.size() + 1);
      cursor_line++;
    }
    const int cursor_column =
        password()
            ? GlyphCount(lines[cursor_line].substr(0, cursor_char_index))
            : string_width(lines[cursor_line].substr(0, cursor_char_index));

    int new_cursor_column = cursor_column + mouse_x - cursor_box_.x_min;
    int new_cursor_line = cursor_line + mouse_y - cursor_box_.y_min;

    // Fix the new cursor position:
    new_cursor_line = std::max(std::min(new_cursor_line, (int)lines.size()), 0);

    const std::string empty_string;
    const std::string& line = new_cursor_line < (int)lines.size()
                                  ? lines[new_cursor_line]
                                  : empty_string;
    new_cursor_column =
        util::clamp(new_cursor_column, 0,
                    password() ? GlyphCount(line) : string_width(line));

    // Convert back the new_cursor_{line,column} toward cursor_position:
    int pos = 0;
    for (int i = 0; i < new_cursor_line; ++i) {
      pos += static_cast<int>(lines[i].size() + 1);
    }
    while (new_cursor_column > 0) {
      if (password()) {
        new_cursor_column -= 1;
      } else {
        new_cursor_column -= static_cast<int>(GlyphWidth(content(), pos));
      }
      pos = static_cast<int>(GlyphNext(content(), pos));
    }
    return pos;
  }

  // CLFCode patch（2026-09-23）：拖选复制（copy-on-select——与显示区同机制
  // 同手感）。原 HandleMouse 仅 Pressed 定位光标、Moved/Released 忽略；
  // 改造：Pressed 定位 + 选区开始；Moved 扩展选区（消费）；Released 触发
  // on_select 回调并清除。on_select 为空时选区机制不激活（原行为）。
  bool HandleMouse(Event event) {
    hovered_ = box_.Contain(event.mouse().x,  //
                            event.mouse().y) &&
               CaptureMouse(event);
    if (!hovered_) {
      return false;
    }

    if (event.mouse().button != Mouse::Left) {
      return false;
    }

    const auto motion = event.mouse().motion;
    if (motion == Mouse::Pressed) {
      TakeFocus();
      const int pos = PositionToCursor(event.mouse().x, event.mouse().y);
      cursor_position() = pos;
      if (on_select) {   // 拖选复制启用 → 选区开始（锚点 = 新光标）
        selection_begin_ = pos;
        selection_end_ = pos;
      }
      App::PostEventOrExecute(on_change);
      return true;
    }

    if (motion == Mouse::Moved) {
      if (selection_begin_ < 0) {
        return false;   // 未在拖选（on_select 未启用/无选区）→ 原行为
      }
      selection_end_ = PositionToCursor(event.mouse().x, event.mouse().y);
      return true;
    }

    if (motion == Mouse::Released) {
      if (selection_begin_ < 0) {
        return false;
      }
      const int begin = std::min(selection_begin_, selection_end_);
      const int end   = std::max(selection_begin_, selection_end_);
      if (begin < end && on_select) {
        on_select(content->substr(begin, end - begin));
      }
      ClearSelection();
      return true;
    }
    return false;
  }

  void ClearSelection() {
    selection_begin_ = -1;
    selection_end_ = -1;
  }

  bool HandleInsert() {
    insert() = !insert();
    return true;
  }

  bool Focusable() const final { return true; }

  bool hovered_ = false;

  // CLFCode patch（2026-09-23）：拖选选区（content 字节区间，-1 = 无选区）
  int selection_begin_ = -1;
  int selection_end_ = -1;

  Box box_;
  Box cursor_box_;
};

}  // namespace

/// @brief An input box for editing text.
/// @param option Additional optional parameters.
/// @ingroup component
/// @see InputBase
///
/// ### Example
///
/// ```cpp
/// auto screen = App::FitComponent();
/// std::string content= "";
/// std::string placeholder = "placeholder";
/// Component input = Input({
///   .content = &content,
///   .placeholder = &placeholder,
/// })
/// screen.Loop(input);
/// ```
///
/// ### Output
///
/// ```bash
/// placeholder
/// ```
Component Input(InputOption option) {
  return Make<InputBase>(std::move(option));
}

/// @brief An input box for editing text.
/// @param content The editable content.
/// @param option Additional optional parameters.
/// @ingroup component
/// @see InputBase
///
/// ### Example
///
/// ```cpp
/// auto screen = App::FitComponent();
/// std::string content= "";
/// std::string placeholder = "placeholder";
/// Component input = Input(content, {
///   .placeholder = &placeholder,
///   .password = true,
/// })
/// screen.Loop(input);
/// ```
///
/// ### Output
///
/// ```bash
/// placeholder
/// ```
Component Input(StringRef content, InputOption option) {
  option.content = std::move(content);
  return Make<InputBase>(std::move(option));
}

/// @brief An input box for editing text.
/// @param content The editable content.
/// @param placeholder The placeholder text.
/// @param option Additional optional parameters.
/// @ingroup component
/// @see InputBase
///
/// ### Example
///
/// ```cpp
/// auto screen = App::FitComponent();
/// std::string content= "";
/// std::string placeholder = "placeholder";
/// Component input = Input(content, placeholder);
/// screen.Loop(input);
/// ```
///
/// ### Output
///
/// ```bash
/// placeholder
/// ```
Component Input(StringRef content, StringRef placeholder, InputOption option) {
  option.content = std::move(content);
  option.placeholder = std::move(placeholder);
  return Make<InputBase>(std::move(option));
}

}  // namespace ftxui
