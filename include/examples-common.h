#pragma once

#include <chrono>
#include <concepts>
#include <thread>
#include <format>
#include <iostream>
#include <filesystem>
#include <type_traits>


namespace examples
{
    
    template<typename T>
    concept is_filesystem_path_or_string = std::is_same_v<std::decay_t<T>, std::filesystem::path> ||
                        std::is_same_v<std::decay_t<T>, std::string> ||
                        std::is_same_v<std::decay_t<T>, std::string_view>;
    


    template<is_filesystem_path_or_string T>
    bool resource_exists(const T& resource_path, const std::string_view runtime_name)
    {
        namespace fs = std::filesystem;
        if (not fs::exists(resource_path))
        {
            std::cerr << "Resource path '" << resource_path << "' does not exist.\nExiting " << runtime_name << ".";
            return false;
        }
        return true;
    }

    template<is_filesystem_path_or_string source>
    auto& remove_token(source& str, const std::string_view token) {

        if constexpr(std::is_same_v<std::decay_t<source>, std::filesystem::path>) {
            auto s = str.string();
            typename source::string_type::size_type pos = s.find(token);
            if (pos != source::string_type::npos) {
                s.erase(pos, token.length());
            }
            str = std::filesystem::path(s);
        } else {
            typename source::size_type pos = str.find(token);
            if (pos != source::npos) {
                str.erase(pos, token.length());
            }
        }
        return str;
    }

}