#include <sstream>
#include <iostream>

#include <cstring>

#define __FILENAME__                                                           \
  (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#define make_str(a, ...) _make_str(__FILENAME__, __LINE__, a, ##__VA_ARGS__)

// Helpers to avoid -Wformat-security
#define ALOGE(fmt, ...)                                                        \
  { std::cerr << "Error: " << make_str(fmt, ##__VA_ARGS__) << std::endl; }

#define ALOGW(fmt, ...)                                                        \
  { std::cerr << "Warning: " << make_str(fmt, ##__VA_ARGS__) << std::endl; }

#define ALOGI(fmt, ...)                                                        \
  { std::cout << "Info: " << make_str(fmt, ##__VA_ARGS__) << std::endl; }

#define ALOGD(fmt, ...)                                                        \
  { std::cout << "Debug: " << make_str(fmt, ##__VA_ARGS__) << std::endl; }

// Base case for recursive variadic template
inline void process_format(std::stringstream &ss, const std::string_view &fmt) {
  for (const auto &c : fmt) {
    if (c == '%') {
      throw std::invalid_argument(
          "Too few arguments provided for format specifiers.");
    }
    ss << c;
  }
}

// Recursive variadic template function
template <typename T, typename... Args>
void process_format(std::stringstream &ss, const std::string_view &fmt,
                    T &&value, Args &&...args) {
  bool found_placeholder = false;
  for (size_t i = 0; i < fmt.size(); ++i) {
    if (fmt[i] == '%') {
      // When we find the first placeholder, replace it with the argument value
      found_placeholder = true;
      ss << std::forward<T>(value);
      // Process the rest of the string with the remaining arguments
      process_format(ss, fmt.substr(i + 2), std::forward<Args>(args)...);
      return;
    } else {
      ss << fmt[i];
    }
  }

  // If we reach here and still have arguments left, that means we have too many
  // arguments.
  if (!found_placeholder) {
    throw std::invalid_argument(
        "Too many arguments provided for format specifiers.");
  }
}

// Main formatting function
template <typename... Args>
std::string _make_str(const std::string &filename, int line,
                      const std::string_view fmt, Args &&...args) {
  std::stringstream ss;
  ss << "[" << filename << ":" << line << "] ";

  // Call recursive formatting function
  process_format(ss, fmt, std::forward<Args>(args)...);

  return ss.str();
}
