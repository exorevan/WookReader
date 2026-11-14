#ifndef WOOK_READER_BOOK_PROGRESS_KEY_HPP
#define WOOK_READER_BOOK_PROGRESS_KEY_HPP

#include <string>

// Builds a valid libconfig key while retaining existing valid book keys.
//
// Args:
//   book_name: Existing sanitized book name, also used for the notes filename.
// Returns:
//   The original valid name, or a losslessly encoded name in a separate namespace.
inline std::string BookProgressKey(const std::string& book_name) {
  const auto is_letter = [](unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
  };
  bool valid = !book_name.empty() && is_letter(book_name.front());
  for (unsigned char c : book_name) {
    if (!is_letter(c) && !(c >= '0' && c <= '9') && c != '-' && c != '_') {
      valid = false;
      break;
    }
  }
  if (valid) return book_name;

  // Legacy sanitization removes '*', so encoded keys cannot alias legacy keys.
  static constexpr char kEncodedPrefix[] = "*";
  static constexpr char kHexDigits[] = "0123456789abcdef";
  std::string key = kEncodedPrefix;
  for (unsigned char c : book_name) {
    key += kHexDigits[c >> 4];
    key += kHexDigits[c & 0x0f];
  }
  return key;
}

#endif
