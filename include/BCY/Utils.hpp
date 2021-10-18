#define _TURN_OFF_PLATFORM_STRING
#ifndef BCY_UTILS_HPP
#define BCY_UTILS_HPP
#include <cpprest/json.h>
#include <sstream>
#include <string>
#include <codecvt>

namespace BCY {
std::wstring bcy_string_to_hex(const std::wstring &input);//Thank you OpenSSL for polluting my namespace
std::wstring generateRandomString(std::wstring alphabet, size_t length);
std::wstring expand_user(std::wstring path);
std::wstring ensure_string(web::json::value foo);
template <typename InputIt>
std::wstring join(InputIt begin, InputIt end,
                 const std::wstring &separator = ", ",
                 const std::wstring &concluder = "")
{
  std::wostringstream ss;

  if (begin != end) {
    ss << *begin++;
  }

  while (begin != end)
  {
    ss << separator;
    ss << *begin++;
  }

  ss << concluder;
  return ss.str();
}
} // namespace BCY
#endif
