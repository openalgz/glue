#pragma once

#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <future>
#include <glaze/exceptions/binary_exceptions.hpp>
#include <map>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "App.h" // uWebSockets
#include "common.h"

#include "glaze/glaze_exceptions.hpp"
#include "glaze/json/json_ptr.hpp"
#include "glaze/rpc/repe.hpp"

namespace incpp
{
   inline int64_t timestamp()
   {
      using namespace std::chrono;
      return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
   }

   struct Request
   {
      glz::repe::header header{};
      
      int64_t t_last_update_ms = -1;
      int64_t t_last_req_ms = -1;
      int64_t t_min_update_ms = 16;
      int64_t t_last_req_timeout_ms = 3000;

      std::string prev{};
      std::string diff{};
      std::string_view cur{};
   };

   struct ClientData
   {
      int64_t t_connected_ms = -1;

      std::array<uint8_t, 4> ip_address{};

      std::vector<std::string> last_requests{};
      std::unordered_map<std::string, Request> requests{};

      std::string buf{}; // buffer
      std::string prev{}; // previous buffer
      std::string diff{}; // difference buffer
   };

   struct Parameters
   {
      int32_t port = 3000;
      int32_t max_payload = 256 * 1024;
      int64_t t_last_req_timeout_ms = 3000;
      int32_t t_idle_timeout_s = 120;

      std::string http_root = ".";
      std::vector<std::string> resources{};

      std::string ssl_key = "key.pem";
      std::string ssl_cert = "cert.pem";

      // todo:
      // max clients
      // max buffered amount
      // etc.
   };

   template <bool SSL>
   struct Incppect
   {
      bool debug = false; // print out debug statements

      template <class... Args>
      inline void print(std::format_string<Args...> fmt, Args&&... args)
      {
         if (debug) {
            const auto str = std::format(fmt, std::forward<Args>(args)...);
            std::fwrite(str.data(), 1, str.size(), stdout);
         }
      }

      int32_t unique_client_id = 1;

      enum struct event : uint8_t {
         connect,
         disconnect,
         custom,
      };

      using handler_t = std::function<void(int32_t client_id, event etype, std::string_view)>;

      struct PerSocketData final
      {
         int32_t client_id{};
         uWS::Loop* main_loop{};
         uWS::WebSocket<SSL, true, PerSocketData>* ws{};
      };

      Parameters parameters{};

      double tx_bytes{};
      double rx_bytes{};

      uWS::Loop* main_loop{};
      us_listen_socket_t* listen_socket{};
      std::map<int32_t, PerSocketData*> socket_data{};
      size_t nclients{}; // intermediate memory
      std::map<int32_t, ClientData> client_data{};

      std::unordered_map<std::string, std::string> resources{};

      handler_t handler{};

      struct glaze 
      {
         using T = Incppect;
         static constexpr std::string_view name = "glue";
         static constexpr auto n_clients = [](auto& s) -> auto& {
            s.nclients = s.socket_data.size();
            return s.nclients;
         };
         static constexpr auto value = glz::object("nclients", n_clients, &T::tx_bytes, &T::rx_bytes, &T::client_data);
      };

      static constexpr glz::opts glz_opts{.format = glz::binary};
      glz::repe::registry<glz_opts> registry{};
      
      static constexpr std::string_view empty_path = "";

      template <const std::string_view& Root = empty_path>
      void on(auto& value) {
         registry.on<Root>(value);
      }

      Incppect()
      {
         on<glz::root<"/glue">>(*this);
      }

      // run the incppect service main loop in the current thread
      // blocking call
      void run(Parameters parameters)
      {
         this->parameters = parameters;
         run();
      }

      // terminate the server instance
      void stop()
      {
         if (main_loop != nullptr) {
            main_loop->defer([this]() {
               for (auto sd : socket_data) {
                  if (sd.second->main_loop != nullptr) {
                     sd.second->main_loop->defer([sd]() { sd.second->ws->close(); });
                  }
               }
               us_listen_socket_close(0, listen_socket);
            });
         }
      }

      // set a resource. useful for serving html/js files from within the application
      void set_resource(const std::string& url, const std::string& content) { resources[url] = content; }

      // number of connected clients
      int32_t n_connected() const { return socket_data.size(); }

      // run the main loop in dedicated thread
      // non-blocking call, returns the created std::future<void>
      template <class Params>
         requires std::same_as<std::decay_t<Params>, Parameters>
      std::future<void> run_async(Params&& params)
      {
         return std::async([this, p = std::forward<Params>(params)]() { run(p); });
      }

      // get global instance
      static Incppect& getInstance()
      {
         static Incppect instance;
         return instance;
      }

      void run()
      {
         main_loop = uWS::Loop::get();

         constexpr std::string_view protocol = SSL ? "HTTPS" : "HTTP";
         print("[incppect] running instance. serving {} from '{}'\n", protocol, parameters.http_root);

         typename uWS::TemplatedApp<SSL>::template WebSocketBehavior<PerSocketData> wsBehaviour;
         wsBehaviour.compression = uWS::SHARED_COMPRESSOR;
         wsBehaviour.maxPayloadLength = parameters.max_payload;
         wsBehaviour.idleTimeout = parameters.t_idle_timeout_s;
         wsBehaviour.open = [&](auto* ws) {
            ++unique_client_id;

            auto& cd = client_data[unique_client_id];
            cd.t_connected_ms = timestamp();

            auto addressBytes = ws->getRemoteAddress();
            cd.ip_address[0] = addressBytes[12];
            cd.ip_address[1] = addressBytes[13];
            cd.ip_address[2] = addressBytes[14];
            cd.ip_address[3] = addressBytes[15];

            PerSocketData* sd = ws->getUserData();
            sd->client_id = unique_client_id;
            sd->ws = ws;
            sd->main_loop = uWS::Loop::get();

            socket_data.emplace(unique_client_id, sd);

            print("[incppect] client with id = {} connected\n", sd->client_id);

            if (handler) {
               handler(sd->client_id, event::connect, {(const char*)cd.ip_address.data(), cd.ip_address.size()});
            }
         };
         
         wsBehaviour.message = [this](auto* ws, std::string_view message, uWS::OpCode /*opCode*/) {
            rx_bytes += message.size();
            
            PerSocketData* sd = ws->getUserData();
            auto& cd = client_data[sd->client_id];
            
            constexpr glz::opts opts{.format = glz::binary, .partial_read = true};
            std::tuple<glz::repe::header, glz::raw_json> repe_message;
            auto ec = glz::read<opts>(repe_message, message);
            if (ec) {
               print("Error reading header");
               return;
            }
            
            auto&[header, body] = repe_message;
            
            if (header.method == "/requests") {
               glz::ex::read_binary(cd.requests, body);
            }
            else (header.method == "/update") {
               sd->main_loop->defer([this] { this->update(); });
            }
         };
         wsBehaviour.drain = [this](auto* ws) {
            /* Check getBufferedAmount here */
            if (ws->getBufferedAmount() > 0) {
               // use this-> to hide wrong warnings from Clang
               this->print("[incppect] drain: buffered amount = {}\n", ws->getBufferedAmount());
            }
         };
         wsBehaviour.ping = [](auto* /*ws*/, std::string_view) {};
         wsBehaviour.pong = [](auto* /*ws*/, std::string_view) {};
         wsBehaviour.close = [this](auto* ws, int /*code*/, std::string_view /*message*/) {
            PerSocketData* sd = ws->getUserData();
            print("[incppect] client with id = {} disconnected\n", sd->client_id);

            client_data.erase(sd->client_id);
            socket_data.erase(sd->client_id);

            if (handler) {
               handler(sd->client_id, event::disconnect, {});
            }
         };

         std::unique_ptr<uWS::TemplatedApp<SSL>> app{};

         if constexpr (SSL) {
            uWS::SocketContextOptions ssl_options{};

            ssl_options.key_file_name = parameters.ssl_key.data();
            ssl_options.cert_file_name = parameters.ssl_cert.data();

            app.reset(new uWS::TemplatedApp<SSL>(ssl_options));
         }
         else {
            app.reset(new uWS::TemplatedApp<SSL>());
         }

         if (app->constructorFailed()) {
            print("[incppect] failed to construct uWS server!\n");
            if (SSL) {
               print("[incppect] verify that you have valid certificate files:\n");
               print("[incppect] key  file : '{}'\n", parameters.ssl_key);
               print("[incppect] cert file : '{}'\n", parameters.ssl_cert);
            }
            return;
         }

         (*app)
            .template ws<PerSocketData>("/incppect", std::move(wsBehaviour))
            .get("/incppect.js", [](auto* res, auto* /*req*/) { res->end(kIncppect_js); });
         for (const auto& resource : parameters.resources) {
            (*app).get("/" + resource, [this](auto* res, auto* req) {
               std::string url{req->getUrl()};
               print("url = '{}'\n", url);

               if (url.empty()) {
                  res->end("Resource not found");
                  return;
               }

               if (url.back() == '/') {
                  url += "index.html";
               }

               if (resources.contains(url)) {
                  res->end(resources[url]);
                  return;
               }

               print("resource = '{}'\n", (parameters.http_root + url));
               std::ifstream file(parameters.http_root + url);

               if (file.is_open() == false || file.good() == false) {
                  res->end("Resource not found");
                  return;
               }

               const std::string str{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

               if (str.empty()) {
                  res->end("Resource not found");
                  return;
               }

               if (req->getUrl().ends_with(".js")) {
                  res->writeHeader("Content-Type", "text/javascript");
               }

               res->end(str);
            });
         }
         (*app).get("/*", [this](auto* res, auto* req) {
            const auto url{req->getUrl()};
            this->print("url = '{}'\n", url); // this-> hides incorrect warnings
            res->end("Resource not found");
            return;
         });
         (*app)
            .listen(parameters.port,
                    [this](auto* token) {
                       this->listen_socket = token;
                       if (token) {
                          print("[incppect] listening on port {}\n", parameters.port);
                          constexpr std::string_view protocol = SSL ? "https" : "http";
                          print("[incppect] {}://localhost:{}/\n", protocol, parameters.port);
                       }
                    })
            .run();
      }

      void update()
      {
         for (auto& [client_id, cd] : client_data) {
            // Waiting for the buffer to clear
            if (socket_data[client_id]->ws->getBufferedAmount()) {
               print(
                  "[incppect] warning: buffered amount = {}, not sending updates to client {}. waiting for buffer to "
                  "drain\n",
                  socket_data[client_id]->ws->getBufferedAmount(), client_id);
               continue;
            }

            auto& buf = cd.buf;
            auto& prev = cd.prev;
            auto& diff = cd.diff;

            buf.clear();

            uint32_t type_all = 0;
            buf.append((char*)(&type_all), sizeof(type_all));

            for (auto& [req_id, req] : cd.requests) {
               std::string_view path = req.path;
               path.remove_prefix(1);
               auto& getter = access[req.path.substr(0, path.find('/'))];
               const auto t = timestamp();
               if (((req.t_last_req_timeout_ms < 0 && req.t_last_req_ms > 0) ||
                    (t - req.t_last_req_ms < req.t_last_req_timeout_ms)) &&
                   t - req.t_last_update_ms > req.t_min_update_ms) {
                  if (req.t_last_req_timeout_ms < 0) {
                     req.t_last_req_ms = 0;
                  }

                  path.remove_prefix(1);
                  req.cur = getter(path);
                  req.t_last_update_ms = t;

                  constexpr int kPadding = 4;

                  int data_size = req.cur.size();
                  int padding_bytes = 0;
                  {
                     int r = data_size % kPadding;
                     while (r > 0 && r < kPadding) {
                        ++data_size;
                        ++padding_bytes;
                        ++r;
                     }
                  }

                  int32_t type = 0; // full update
                  if (req.prev.size() == req.cur.size() + padding_bytes && req.cur.size() > 256) {
                     type = 1; // run-length encoding of diff
                  }

                  buf.append((char*)(&req_id), sizeof(req_id));
                  buf.append((char*)(&type), sizeof(type));

                  if (type == 0) {
                     buf.append((char*)(&data_size), sizeof(data_size));
                     buf.append(req.cur);
                     {
                        char v = 0;
                        for (int i = 0; i < padding_bytes; ++i) {
                           buf.append((char*)(&v), sizeof(v));
                        }
                     }
                  }
                  else if (type == 1) {
                     uint32_t a = 0;
                     uint32_t b = 0;
                     uint32_t c = 0;
                     uint32_t n = 0;
                     req.diff.clear();

                     for (int i = 0; i < (int)req.cur.size(); i += 4) {
                        std::memcpy(&a, req.prev.data() + i, sizeof(uint32_t));
                        std::memcpy(&b, req.cur.data() + i, sizeof(uint32_t));
                        a = a ^ b;
                        if (a == c) {
                           ++n;
                        }
                        else {
                           if (n > 0) {
                              req.diff.append((char*)(&n), sizeof(uint32_t));
                              req.diff.append((char*)(&c), sizeof(uint32_t));
                           }
                           n = 1;
                           c = a;
                        }
                     }

                     if (req.cur.size() % 4 != 0) {
                        a = 0;
                        b = 0;
                        uint32_t i = (req.cur.size() / 4) * 4;
                        uint32_t k = req.cur.size() - i;
                        std::memcpy(&a, req.prev.data() + i, k);
                        std::memcpy(&b, req.cur.data() + i, k);
                        a = a ^ b;
                        if (a == c) {
                           ++n;
                        }
                        else {
                           req.diff.append((char*)(&n), sizeof(uint32_t));
                           req.diff.append((char*)(&c), sizeof(uint32_t));
                           n = 1;
                           c = a;
                        }
                     }

                     req.diff.append((char*)(&n), sizeof(uint32_t));
                     req.diff.append((char*)(&c), sizeof(uint32_t));

                     data_size = req.diff.size();
                     buf.append((char*)(&data_size), sizeof(data_size));
                     buf.append(req.diff);
                  }

                  req.prev = req.cur;
               }
            }

            if (buf.size() > 4) {
               if (buf.size() == prev.size() && buf.size() > 256) {
                  uint32_t a = 0;
                  uint32_t b = 0;
                  uint32_t c = 0;
                  uint32_t n = 0;
                  diff.clear();

                  uint32_t type_all = 1;
                  diff.append((char*)(&type_all), sizeof(type_all));

                  for (int i = 4; i < (int)buf.size(); i += 4) {
                     std::memcpy(&a, prev.data() + i, sizeof(uint32_t));
                     std::memcpy(&b, buf.data() + i, sizeof(uint32_t));
                     a = a ^ b;
                     if (a == c) {
                        ++n;
                     }
                     else {
                        if (n > 0) {
                           diff.append((char*)(&n), sizeof(uint32_t));
                           diff.append((char*)(&c), sizeof(uint32_t));
                        }
                        n = 1;
                        c = a;
                     }
                  }

                  diff.append((char*)(&n), sizeof(uint32_t));
                  diff.append((char*)(&c), sizeof(uint32_t));

                  if ((int32_t)diff.size() > parameters.max_payload) {
                     print("[incppect] warning: buffer size ({}) exceeds maxPayloadLength ({})\n", diff.size(),
                           parameters.max_payload);
                  }

                  // compress only for message larger than 64 bytes
                  bool doCompress = diff.size() > 64;

                  if (socket_data[client_id]->ws->send({diff.data(), diff.size()}, uWS::OpCode::BINARY, doCompress) ==
                      false) {
                     print("[incpeect] warning: backpressure for client {} increased \n", client_id);
                  }
               }
               else {
                  if (int32_t(buf.size()) > parameters.max_payload) {
                     print("[incppect] warning: buffer size ({}) exceeds maxPayloadLength ({})\n", (int)buf.size(),
                           parameters.max_payload);
                  }

                  // compress only for message larger than 64 bytes
                  const bool compress = buf.size() > 64;
                  if (socket_data[client_id]->ws->send({buf.data(), buf.size()}, uWS::OpCode::BINARY, compress) ==
                      false) {
                     print("[incpeect] warning: backpressure for client {} increased \n", client_id);
                  }
               }

               tx_bytes += buf.size();

               prev = buf;
            }
         }
      }
   };
}
