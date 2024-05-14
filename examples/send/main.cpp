/*! \file main.cpp
 *  \brief incppect basics
 *  \author Georgi Gerganov
 */

#include "incppect/incppect.h"
#include "examples-common.h"

using incppect = incpp::Incppect<false>;
namespace fs = std::filesystem;
using namespace examples;

int main(int argc, char ** argv) {
	printf("Usage: %s [port] [httpRoot]\n", argv[0]);

    int port = argc > 1 ? atoi(argv[1]) : 3000;
    std::string httpRoot = argc > 2 ? argv[2] : "./_deps/incppect-src/examples";

    // defined the project directory
    if (not fs::exists(httpRoot)) {
        httpRoot = fs::absolute(".").string();
        httpRoot = std::format("{}/_deps/incppect-src/examples",remove_token(httpRoot, "/bin/."));
        httpRoot = in_place_replace(httpRoot, "/./", "/build/");
    }

    auto resource_path = std::format("{}/send/index.html", httpRoot);
    if (not resource_exists(resource_path, "send")) std::exit(1);
    std::cout << "\nurl: localhost:" << port << std::endl;

    incppect::Parameters parameters{
        .portListen = port,
        .maxPayloadLength_bytes = 256 * 1024,
        .httpRoot = httpRoot + "/send",
        .resources = {"", "index.html"}
    };
    // handle input from the clients
    incppect::getInstance().handler([&](int clientId, incppect::EventType etype, std::string_view data) {
        switch (etype) {
            case incppect::Connect:
                {
                    printf("Client %d connected\n", clientId);
                }
                break;
            case incppect::Disconnect:
                {
                    printf("Client %d disconnected\n", clientId);
                }
                break;
            case incppect::Custom:
                {
                    printf("Client %d: '%s'\n", clientId, std::string(data.data(), data.size()).c_str());
                }
                break;
        };
    });

    incppect::getInstance().runAsync(parameters).detach();

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}
