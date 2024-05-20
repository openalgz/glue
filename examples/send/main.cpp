/*! \file main.cpp
 *  \brief incppect basics
 *  \author Georgi Gerganov
 */
#include "examples-common.h"

using incppect = incpp::Incppect<false>;
namespace fs = std::filesystem;
using namespace examples;

int main(int argc, char** argv)
{
   printf("Usage: %s [port] [httpRoot]\n", argv[0]);

   int port = argc > 1 ? atoi(argv[1]) : 3020;

   auto parameters = configure_incppect_example(argc, argv, "send", port);

   // handle input from the clients
   incppect::getInstance().handler = [&](int clientId, incppect::EventType etype, std::string_view data) {
      switch (etype) {
      case incppect::Connect: {
         printf("Client %d connected\n", clientId);
      } break;
      case incppect::Disconnect: {
         printf("Client %d disconnected\n", clientId);
      } break;
      case incppect::Custom: {
         printf("Client %d: '%s'\n", clientId, std::string(data.data(), data.size()).c_str());
      } break;
      };
   };

   auto future = incppect::getInstance().run_async(parameters);

   while (true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }

   return 0;
}
