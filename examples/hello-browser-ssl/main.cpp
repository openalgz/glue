/*! \file main.cpp
 *  \brief simple SSL example
 *  \author Georgi Gerganov
 */

#include "incppect/incppect.h"
#include "examples-common.h"

namespace fs = std::filesystem;
using namespace examples;

// using SSL
using incppect = Incppect<true>;

int main(int argc, char ** argv) {
	printf("Usage: %s [port] [httpRoot]\n", argv[0]);

    int port = argc > 1 ? atoi(argv[1]) : 3000;
    std::string httpRoot = argc > 2 ? argv[2] : "./_deps/incppect-src/examples";

    // defined the project directory
    if (not fs::exists(httpRoot)) {
        httpRoot = fs::absolute(".").string();
        httpRoot = std::format("{}/_deps/incppect-src/examples",remove_token(httpRoot, "/bin/."));
    }

    auto resource_path = std::format("{}/hello-browser-ssl/index.html", httpRoot);
    if (not resource_exists(resource_path, "hello-browser-ssl")) std::exit(1);
    std::cout << "\nurl: localhost:" << port << std::endl;

    incppect::Parameters parameters{
        .portListen = port,
        .maxPayloadLength_bytes = 256 * 1024,
        .httpRoot = httpRoot + "/hello-browser-ssl",
        .resources = {"", "index.html"}
    };

    // make sure you have generated those and they are valid
    parameters.sslKey = "key.pem";
    parameters.sslCert = "cert.pem";

    incppect::getInstance().runAsync(parameters).detach();

    int8_t  var_int8  = 1;
    int16_t var_int16 = 2;
    int32_t var_int32 = 3;
    int32_t var_arr32[4] = { 4, 5, 6, 7 };
    float   var_float = 8.0f;
    double  var_double = 16.0f;

    const char * var_str = "hello browser over SSL";

    incppect::getInstance().var("var_int8", [&](auto) { return incppect::view(var_int8); });
    incppect::getInstance().var("var_int16", [&](auto) { return incppect::view(var_int16); });
    incppect::getInstance().var("var_int32", [&](auto) { return incppect::view(var_int32); });
    incppect::getInstance().var("var_int32_arr", [&](auto) { return incppect::view(var_arr32); });
    incppect::getInstance().var("var_int32_arr[%d]", [&](auto idxs) { return incppect::view(var_arr32[idxs[0]]); });
    incppect::getInstance().var("var_float", [&](auto ) { return incppect::view(var_float); });
    incppect::getInstance().var("var_double", [&](auto ) { return incppect::view(var_double); });
    incppect::getInstance().var("var_str", [&](auto ) { return var_str; });

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}
