#pragma once

#include <chrono>
#include <concepts>
#include <thread>
#include <format>
#include <iostream>
#include <filesystem>

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
}