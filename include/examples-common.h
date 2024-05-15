#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <ranges>
#include <thread>
#include <type_traits>
#include <vector>

#include "incppect/incppect.h"
namespace examples
{
#ifdef _WIN32
   inline constexpr char path_separator = '\\';
#else
   inline constexpr char path_separator = '/';
#endif

   template <typename StrT>
   concept is_string =
      std::is_same_v<std::decay_t<StrT>, std::string> || std::is_same_v<std::decay_t<StrT>, std::string_view> ||
      std::is_same_v<std::decay_t<StrT>, std::wstring> || std::is_same_v<std::decay_t<StrT>, std::wstring_view>;

   template <typename StrT_Or_PathT>
   concept is_filesystem_path_or_string =
      std::is_same_v<std::decay_t<StrT_Or_PathT>, std::filesystem::path> || is_string<StrT_Or_PathT>;

   template <is_filesystem_path_or_string StrT_Or_PathT>
   std::string truncate_directory_path_at_last_folder(const StrT_Or_PathT& path)
   {
      std::string str;
      if constexpr (std::is_same_v<std::decay_t<StrT_Or_PathT>, std::filesystem::path>) {
         str = path.generic_string();
      }
      else {
         str = path;
      }

      // On Windows folder separators may be either '/' or '\\' therefore:
      //
      size_t pos = str.find_last_of(path_separator);
      size_t alt_pos = str.find_last_of(path_separator == '\\' ? '/' : '\\');
      if (pos != std::string_view::npos && alt_pos != std::string_view::npos) {
         pos = std::max(pos, alt_pos);
      }
      else if (alt_pos != std::string_view::npos) {
         pos = alt_pos;
      }

      if (pos != std::string_view::npos) {
         return str.substr(0, pos);
      }
      else {
         return str;
      }
   }

   template <is_filesystem_path_or_string T>
   bool resource_exists(const T& resource_path, const std::string_view runtime_name)
   {
      namespace fs = std::filesystem;
      if (not fs::exists(resource_path)) {
         std::cerr << "Resource path '" << resource_path << "' does not exist.\nExiting " << runtime_name << "." << std::endl;
         return false;
      }
      return true;
   }

   inline incpp::Parameters configure_incppect_example(int argc, char* argv[], const std::string_view example_name,
                                                       int port, const std::string_view index_file_name = "index.html")
   {
      namespace fs = std::filesystem;

      auto httpRoot = std::filesystem::current_path().generic_string();

      // Sadly "build" is hard coded here for the project generation directory.
      // Long story short is to support various Visual Code IDE modes this is
      // being required (to simplify the code here). Otherwise Visual Code will 
      // hand off inconsistent paths from std::current_directory and absolute 
      // when running from the debugger vs terminal mode startup.
      //
      httpRoot = std::format("{}/build/_deps/incppect-src/examples", httpRoot);

      if (not fs::exists(httpRoot)) {
         httpRoot = argc > 2 ? argv[2] : truncate_directory_path_at_last_folder(std::filesystem::current_path());
         httpRoot = std::format("{}/_deps/incppect-src/examples", httpRoot);
      }

      auto resource_path = std::format("{}/{}/{}", httpRoot, example_name, index_file_name);
      if (not resource_exists(resource_path, example_name)) std::exit(1);

      std::cout << "\nurl: localhost:" << port << std::endl;

      return incpp::Parameters{.portListen = port,
                               .maxPayloadLength_bytes = 256 * 1024,
                               .httpRoot = httpRoot + "/" + std::string(example_name),
                               .resources = {"", index_file_name.data()}};
   }

}