#pragma once

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>

inline std::string escape_json(const std::string & value)
{
  std::ostringstream escaped;
  for (const char character : value) {
    switch (character) {
      case '"':
        escaped << "\\\"";
        break;
      case '\\':
        escaped << "\\\\";
        break;
      case '\b':
        escaped << "\\b";
        break;
      case '\f':
        escaped << "\\f";
        break;
      case '\n':
        escaped << "\\n";
        break;
      case '\r':
        escaped << "\\r";
        break;
      case '\t':
        escaped << "\\t";
        break;
      default:
        escaped << character;
        break;
    }
  }
  return escaped.str();
}

inline void write_json_string(std::ostream & out, const std::string & value)
{
  out << '"' << escape_json(value) << '"';
}

inline std::string to_iso8601_utc(const std::chrono::system_clock::time_point & time_point)
{
  const auto timestamp = std::chrono::system_clock::to_time_t(time_point);
  std::tm calendar_time{};
#if defined(_WIN32)
  gmtime_s(&calendar_time, &timestamp);
#else
  calendar_time = *std::gmtime(&timestamp);
#endif

  std::ostringstream out;
  out << std::put_time(&calendar_time, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

inline std::ofstream create_output_file(const std::string & path)
{
  const std::filesystem::path output_path(path);
  if (output_path.has_parent_path()) {
    std::filesystem::create_directories(output_path.parent_path());
  }

  std::ofstream output(path);
  if (!output.is_open()) {
    throw std::runtime_error("failed to open output file: " + path);
  }
  return output;
}
