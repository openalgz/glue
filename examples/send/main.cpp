/*! \file main.cpp
 *  \brief incppect basics
 *  \author Georgi Gerganov
 */
#include "examples-common.h"
#include "localhost-root-path.hpp"

using incppect = incpp::Incppect<false>;
namespace fs = std::filesystem;
using namespace examples;

int main(int argc, char** argv)
{
   printf("Usage: %s [port] [http_root]\n", argv[0]);

   int port = argc > 1 ? atoi(argv[1]) : 3020;

   std::string http_route = localhost_root_path;
   auto parameters = configure_incppect_example(argc, argv, http_route, port);

   // handle input from the clients
   incppect::getInstance().handler = [&](int client_id, incppect::event etype, std::string_view data) {
      using enum incppect::event;
      switch (etype) {
      case connect: {
         std::printf("Client %d connected\n", client_id);
      } break;
      case disconnect: {
         std::printf("Client %d disconnected\n", client_id);
      } break;
      case custom: {
         std::printf("Client %d: '%s'\n", client_id, std::string(data.data(), data.size()).c_str());
      } break;
      };
   };

   auto future = incppect::getInstance().run_async(parameters);

   while (true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }

   return 0;
}
