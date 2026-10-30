// Minimal Arduino shim so the UI code builds and runs on a PC.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif
uint32_t millis(void);
#ifdef __cplusplus
}

#include <algorithm>
#include <cctype>
#include <string>

inline void delay(uint32_t) {}
inline long random(long howbig) { return howbig > 0 ? rand() % howbig : 0; }
inline long random(long howsmall, long howbig) { return howbig > howsmall ? howsmall + rand() % (howbig - howsmall) : howsmall; }

class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  String(char ch) : s_(1, ch) {}
  explicit String(int v) : s_(std::to_string(v)) {}
  explicit String(unsigned v) : s_(std::to_string(v)) {}
  explicit String(long v) : s_(std::to_string(v)) {}
  explicit String(unsigned long v) : s_(std::to_string(v)) {}
  explicit String(long long v) : s_(std::to_string(v)) {}

  const char *c_str() const { return s_.c_str(); }
  unsigned length() const { return s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  void reserve(unsigned n) { s_.reserve(n); }
  char operator[](unsigned i) const { return i < s_.size() ? s_[i] : 0; }

  String substring(unsigned from) const { return from >= s_.size() ? String() : String(s_.substr(from)); }
  String substring(unsigned from, unsigned to) const {
    if (from > to) std::swap(from, to);
    if (from >= s_.size()) return String();
    return String(s_.substr(from, std::min<unsigned>(to, s_.size()) - from));
  }
  int indexOf(char ch, unsigned from = 0) const {
    const size_t p = s_.find(ch, from);
    return p == std::string::npos ? -1 : (int)p;
  }
  long toInt() const { return strtol(s_.c_str(), nullptr, 10); }
  bool startsWith(const String &p) const { return s_.compare(0, p.s_.size(), p.s_) == 0; }
  bool endsWith(const String &p) const {
    return s_.size() >= p.s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
  }
  void toLowerCase() {
    for (char &ch : s_) ch = (char)tolower((unsigned char)ch);
  }
  void toUpperCase() {
    for (char &ch : s_) ch = toupper((unsigned char)ch);
  }
  void trim() {
    const size_t a = s_.find_first_not_of(" \t\r\n");
    const size_t b = s_.find_last_not_of(" \t\r\n");
    s_ = a == std::string::npos ? "" : s_.substr(a, b - a + 1);
  }

  String &operator+=(const String &o) { s_ += o.s_; return *this; }
  String &operator+=(const char *o) { s_ += o; return *this; }
  String &operator+=(char o) { s_ += o; return *this; }
  friend String operator+(const String &a, const String &b) { return String(a.s_ + b.s_); }
  friend String operator+(const String &a, const char *b) { return String(a.s_ + b); }
  friend String operator+(const char *a, const String &b) { return String(a + b.s_); }
  bool operator==(const String &o) const { return s_ == o.s_; }
  bool operator==(const char *o) const { return s_ == o; }
  bool operator!=(const String &o) const { return s_ != o.s_; }
  bool operator<(const String &o) const { return s_ < o.s_; }

 private:
  std::string s_;
};

template <typename T>
inline T max(T a, T b) { return a > b ? a : b; }
#endif  // __cplusplus
