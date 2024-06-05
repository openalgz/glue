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

   bool resource_exists(const std::string_view http_route)
   {
      namespace fs = std::filesystem;
      if (not fs::exists(http_route)) {
         std::cerr << "Resource path '" << http_route << "' does not exist.\nExiting program.\n" << std::endl;
         return false;
      }
      return true;
   }

   inline incpp::Parameters configure_incppect_example(int argc, char* argv[], std::string& http_route, int port,
                                                       const std::string_view index_file_name = "index.html")
   {
      namespace fs = std::filesystem;
      
      if (argc > 2) {
         http_route = argv[2];
      }

      auto resource_path = std::format("{}/{}", http_route, index_file_name);
      if (not resource_exists(http_route)) std::exit(1);

      std::cout << "\nurl: localhost:" << port << std::endl;

      return incpp::Parameters{.port = port,
                               .max_payload = 256 * 1024,
                               .http_root = http_route,
                               .resources = {"", index_file_name.data()}};
   }

}
