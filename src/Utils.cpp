#include <BCY/Utils.hpp>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <assert.h>

extern std::wstring_convert<std::codecvt_utf8<wchar_t>> toWstring;
using json = web::json::value;
using namespace std;
wstring BCY::bcy_string_to_hex(const wstring &input) {
  static const char *const lut = "0123456789abcdef";
  size_t len = input.length();

  std::wstring output;
  output.reserve(2 * len);
  for (size_t i = 0; i < len; ++i) {
    const unsigned char c = input[i];
    output.push_back(lut[c >> 4]);
    output.push_back(lut[c & 15]);
  }
  return output;
}
wstring BCY::ensure_string(web::json::value foo) {
  if (foo.is_string()) {
    return foo.as_string();
  } else if (foo.is_number()) {
    int64_t num = foo.as_number().to_int64();
    return to_wstring(num);
  } else if (foo.is_null()) {
    return L"";
  } else {
    throw std::invalid_argument(toWstring.to_bytes(foo.serialize()) +
                                " Can't Be Converted to String");
  }
}
wstring BCY::generateRandomString(wstring alphabet, size_t length) {
  wstringstream ss;
  random_device rd;                // obtain a random number from hardware
  default_random_engine eng(rd()); // seed the generator
  uniform_int_distribution<> distr(0,
                                   alphabet.length() - 1); // define the range
  for (size_t i = 0; i < length; i++) {
    ss << alphabet[distr(eng)];
  }
  return ss.str();
}
wstring BCY::expand_user(wstring path) {
  if (! path.empty() && path[0] == '~') {
    assert(path.size() == 1 || path[1] == '/'); // or other error handling
    char const *home = getenv("HOME");
    if (home || ((home = getenv("USERPROFILE")))) {
      path.replace(0, 1, toWstring.from_bytes(home));
    } else {
      char const *hdrive = getenv("HOMEDRIVE"), *hpath = getenv("HOMEPATH");
      assert(hdrive); // or other error handling
      assert(hpath);
      path.replace(0, 1, toWstring.from_bytes(std::string(hdrive) + hpath));
    }
  }
  return path;
}
