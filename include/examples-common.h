#pragma once

#include <chrono>
#include <concepts>
#include <filesystem>
#include <format>
#include <iostream>
#include <thread>
#include <type_traits>

namespace examples
{

   template <typename T>
   concept is_filesystem_path_or_string =
      std::is_same_v<std::decay_t<T>, std::filesystem::path> || std::is_same_v<std::decay_t<T>, std::string> ||
      std::is_same_v<std::decay_t<T>, std::string_view>;

   template <typename T>
   concept is_string =
      std::is_same_v<std::decay_t<T>, std::string> || std::is_same_v<std::decay_t<T>, std::string_view>;

   template <is_string T>
   T& in_place_replace(T& str, const std::string_view substr_to_replace, const std::string_view new_str)
   {
      size_t pos = 0;
      while ((pos = str.find(substr_to_replace, pos)) != T::npos) {
         str.replace(pos, substr_to_replace.length(), new_str);
         pos += new_str.length();
      }
      return str;
   }

   template <is_filesystem_path_or_string T>
   bool resource_exists(const T& resource_path, const std::string_view runtime_name)
   {
      namespace fs = std::filesystem;
      if (not fs::exists(resource_path)) {
         std::cerr << "Resource path '" << resource_path << "' does not exist.\nExiting " << runtime_name << ".";
         return false;
      }
      return true;
   }

   template <is_filesystem_path_or_string source>
   auto& remove_token(source& str, const std::string_view token)
   {
      if constexpr (std::is_same_v<std::decay_t<source>, std::filesystem::path>) {
         auto s = str.string();
         typename source::string_type::size_type pos = s.find(token);
         if (pos != source::string_type::npos) {
            s.erase(pos, token.length());
         }
         str = std::filesystem::path(s);
      }
      else {
         typename source::size_type pos = str.find(token);
         if (pos != source::npos) {
            str.erase(pos, token.length());
         }
      }
      return str;
   }

}