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
   printf("Usage: %s [port] [http_root]\n", argv[0]);

   int port = argc > 1 ? atoi(argv[1]) : 3010;

   auto parameters = configure_incppect_example(argc, argv, "hello-browser", port);

   auto future = incppect::getInstance().run_async(parameters);

   int8_t var_int8 = 1;
   int16_t var_int16 = 2;
   int32_t var_int32 = 3;
   int32_t var_arr32[4] = {4, 5, 6, 7};
   float var_float = 8.0f;
   double var_double = 16.0f;

   const char* var_str = "hello browser";

   incppect::getInstance().var("var_int8", [&](auto) { return incppect::view(var_int8); });
   incppect::getInstance().var("var_int16", [&](auto) { return incppect::view(var_int16); });
   incppect::getInstance().var("var_int32", [&](auto) { return incppect::view(var_int32); });
   incppect::getInstance().var("var_int32_arr", [&](auto) { return incppect::view(var_arr32); });
   incppect::getInstance().var("var_int32_arr[%d]", [&](auto idxs) { return incppect::view(var_arr32[idxs[0]]); });
   incppect::getInstance().var("var_float", [&](auto) { return incppect::view(var_float); });
   incppect::getInstance().var("var_double", [&](auto) { return incppect::view(var_double); });
   incppect::getInstance().var("var_str", [&](auto) { return var_str; });

   while (true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }

   return 0;
}
