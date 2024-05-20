#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "App.h" // uWebSockets
#include "common.h"

namespace incpp
{
   inline int64_t timestamp()
   {
      using namespace std::chrono;
      return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
   }

   struct Request
   {
      int64_t tLastUpdated_ms = -1;
      int64_t tLastRequested_ms = -1;
      int64_t tMinUpdate_ms = 16;
      int64_t t_last_request_timeout_ms = 3000;

      std::vector<int> idxs{};
      int32_t getterId = -1;

      std::string prevData{};
      std::string diffData{};
      std::string_view curData{};
   };

   struct ClientData
   {
      int64_t t_connected_ms = -1;

      std::array<uint8_t, 4> ip_address{};

      std::vector<int32_t> last_requests;
      std::map<int32_t, Request> requests;

      std::string buf{}; // buffer
      std::string prev{}; // previous buffer
      std::string diff{}; // difference buffer
   };

   struct Parameters
   {
      int32_t port_listen = 3000;
      int32_t max_payload = 256 * 1024;
      int64_t t_last_request_timeout_ms = 3000;
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

   // shorthand for string_view from var
   template <class T>
   inline std::string_view view(T& v)
   {
      if constexpr (std::same_as<std::decay_t<T>, std::string>) {
         return std::string_view{v.data(), v.size()};
      }
      return std::string_view{(char*)(&v), sizeof(v)};
   }

   template <class T>
   inline std::string_view view(T&& v)
   {
      static T t;
      t = std::move(v);
      return std::string_view{(char*)(&t), sizeof(t)};
   }

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

      enum struct event : uint8_t {
         connect,
         disconnect,
         custom,
      };

      using TResourceContent = std::string;
      using TGetter = std::function<std::string_view(const std::vector<int>& idxs)>;
      using THandler = std::function<void(int clientId, event etype, std::string_view)>;

      struct PerSocketData final
      {
         int32_t clientId{};
         uWS::Loop* mainLoop{};
         uWS::WebSocket<SSL, true, PerSocketData>* ws{};
      };

      Parameters parameters{};

      double txTotal_bytes{};
      double rxTotal_bytes{};

      std::map<std::string, int> pathToGetter{};
      std::vector<TGetter> getters{};

      uWS::Loop* mainLoop{};
      us_listen_socket_t* listenSocket{};
      std::map<int32_t, PerSocketData*> socketData{};
      std::map<int32_t, ClientData> clientData{};

      std::map<std::string, TResourceContent> resources{};

      THandler handler{};

      // service parameters

      Incppect()
      {
         var("incppect.nclients", [this](const std::vector<int>&) { return view(socketData.size()); });
         var("incppect.tx_total", [this](const std::vector<int>&) { return view(txTotal_bytes); });
         var("incppect.rx_total", [this](const std::vector<int>&) { return view(rxTotal_bytes); });
         var("incppect.ip_address[%d]", [this](const std::vector<int>& idxs) {
            auto it = clientData.cbegin();
            std::advance(it, idxs[0]);
            return view(it->second.ip_address);
         });
      }
      ~Incppect() {}

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
         if (mainLoop != nullptr) {
            mainLoop->defer([this]() {
               for (auto sd : socketData) {
                  if (sd.second->mainLoop != nullptr) {
                     sd.second->mainLoop->defer([sd]() { sd.second->ws->close(); });
                  }
               }
               us_listen_socket_close(0, listenSocket);
            });
         }
      }

      // set a resource. useful for serving html/js files from within the application
      void setResource(const std::string& url, const TResourceContent& content) { resources[url] = content; }

      // number of connected clients
      int32_t nConnected() const { return socketData.size(); }

      // run the main loop in dedicated thread
      // non-blocking call, returns the created std::future<void>
      template <class Params>
         requires std::same_as<std::decay_t<Params>, Parameters>
      std::future<void> run_async(Params&& params)
      {
         return std::async([this, p = std::forward<Params>(params)]() { run(p); });
      }

      // define variable/memory to inspect
      //
      // examples:
      //
      //   var("path0", [](auto ) { ... });
      //   var("path1[%d]", [](auto idxs) { ... idxs[0] ... });
      //   var("path2[%d].foo[%d]", [](auto idxs) { ... idxs[0], idxs[1] ... });
      //
      bool var(const std::string& path, TGetter&& getter)
      {
         pathToGetter[path] = getters.size();
         getters.emplace_back(std::move(getter));

         return true;
      }

      // get global instance
      static Incppect& getInstance()
      {
         static Incppect instance;
         return instance;
      }

      inline bool hasExt(std::string_view file, std::string_view ext)
      {
         if (ext.size() > file.size()) {
            return false;
         }
         return std::equal(ext.rbegin(), ext.rend(), file.rbegin());
      }

      void run()
      {
         mainLoop = uWS::Loop::get();

         constexpr std::string_view protocol = SSL ? "HTTPS" : "HTTP";
         print("[incppect] running instance. serving {} from '{}'\n", protocol, parameters.http_root);

         typename uWS::TemplatedApp<SSL>::template WebSocketBehavior<PerSocketData> wsBehaviour;
         wsBehaviour.compression = uWS::SHARED_COMPRESSOR;
         wsBehaviour.maxPayloadLength = parameters.max_payload;
         wsBehaviour.idleTimeout = parameters.t_idle_timeout_s;
         wsBehaviour.open = [&](auto* ws) {
            static int32_t uniqueId = 1;
            ++uniqueId;

            auto& cd = clientData[uniqueId];
            cd.t_connected_ms = timestamp();

            auto addressBytes = ws->getRemoteAddress();
            cd.ip_address[0] = addressBytes[12];
            cd.ip_address[1] = addressBytes[13];
            cd.ip_address[2] = addressBytes[14];
            cd.ip_address[3] = addressBytes[15];

            PerSocketData* sd = ws->getUserData();
            sd->clientId = uniqueId;
            sd->ws = ws;
            sd->mainLoop = uWS::Loop::get();

            socketData.insert({uniqueId, sd});

            print("[incppect] client with id = {} connected\n", sd->clientId);

            if (handler) {
               handler(sd->clientId, event::connect, {(const char*)cd.ip_address.data(), 4});
            }
         };
         wsBehaviour.message = [this](auto* ws, std::string_view message, uWS::OpCode /*opCode*/) {
            rxTotal_bytes += message.size();
            if (message.size() < sizeof(int)) {
               return;
            }

            int32_t type = -1;
            std::memcpy((char*)(&type), message.data(), sizeof(type));

            bool doUpdate = true;

            PerSocketData* sd = ws->getUserData();
            auto& cd = clientData[sd->clientId];

            switch (type) {
            case 1: {
               std::stringstream ss(message.data() + 4);
               while (true) {
                  Request request;

                  std::string path;
                  ss >> path;
                  if (ss.eof()) break;
                  int requestId = 0;
                  ss >> requestId;
                  int nidxs = 0;
                  ss >> nidxs;
                  for (int i = 0; i < nidxs; ++i) {
                     int idx = 0;
                     ss >> idx;
                     if (idx == -1) idx = sd->clientId;
                     request.idxs.push_back(idx);
                  }

                  if (pathToGetter.find(path) != pathToGetter.end()) {
                     print("[incppect] requestId = {}, path = '{}', nidxs = {}\n", requestId, path, nidxs);
                     request.getterId = pathToGetter[path];

                     cd.requests[requestId] = std::move(request);
                  }
                  else {
                     print("[incppect] missing path '{}'\n", path);
                  }
               }
            } break;
            case 2: {
               int nRequests = (message.size() - sizeof(int32_t)) / sizeof(int32_t);
               if (nRequests * sizeof(int32_t) + sizeof(int32_t) != message.size()) {
                  print("[incppect] error : invalid message data!\n");
                  return;
               }
               print("[incppect] received requests: {}\n", nRequests);

               cd.last_requests.clear();
               for (int i = 0; i < nRequests; ++i) {
                  int32_t curRequest = -1;
                  std::memcpy((char*)(&curRequest), message.data() + 4 * (i + 1), sizeof(curRequest));
                  if (cd.requests.find(curRequest) != cd.requests.end()) {
                     cd.last_requests.push_back(curRequest);
                     cd.requests[curRequest].tLastRequested_ms = timestamp();
                     cd.requests[curRequest].t_last_request_timeout_ms = parameters.t_last_request_timeout_ms;
                  }
               }
            } break;
            case 3: {
               for (auto curRequest : cd.last_requests) {
                  if (cd.requests.find(curRequest) != cd.requests.end()) {
                     cd.requests[curRequest].tLastRequested_ms = timestamp();
                     cd.requests[curRequest].t_last_request_timeout_ms = parameters.t_last_request_timeout_ms;
                  }
               }
            } break;
            case 4: {
               doUpdate = false;
               if (handler && message.size() > sizeof(int32_t)) {
                  handler(sd->clientId, event::custom,
                          {message.data() + sizeof(int32_t), message.size() - sizeof(int32_t)});
               }
            } break;
            default:
               print("[incppect] unknown message type: {}\n", type);
            };

            if (doUpdate) {
               sd->mainLoop->defer([this]() { this->update(); });
            }
         };
         wsBehaviour.drain = [this](auto* ws) {
            /* Check getBufferedAmount here */
            if (ws->getBufferedAmount() > 0) {
               this->print("[incppect] drain: buffered amount = {}\n", ws->getBufferedAmount());
            }
         };
         wsBehaviour.ping = [](auto* /*ws*/, std::string_view) {

         };
         wsBehaviour.pong = [](auto* /*ws*/, std::string_view) {

         };
         wsBehaviour.close = [this](auto* ws, int /*code*/, std::string_view /*message*/) {
            PerSocketData* sd = ws->getUserData();
            print("[incppect] client with id = {} disconnected\n", sd->clientId);

            clientData.erase(sd->clientId);
            socketData.erase(sd->clientId);

            if (handler) {
               handler(sd->clientId, event::disconnect, {nullptr, 0});
            }
         };

         std::unique_ptr<uWS::TemplatedApp<SSL>> app;

         if constexpr (SSL) {
            uWS::SocketContextOptions ssl_options = {};

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

               if (resources.find(url) != resources.end()) {
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

               if (hasExt(req->getUrl(), ".js")) {
                  res->writeHeader("Content-Type", "text/javascript");
               }

               res->end(str);
            });
         }
         (*app).get("/*", [this](auto* res, auto* req) {
            const std::string url{req->getUrl()};
            print("url = '{}'\n", url);

            res->end("Resource not found");
            return;
         });
         (*app)
            .listen(parameters.port_listen,
                    [this](auto* token) {
                       this->listenSocket = token;
                       if (token) {
                          print("[incppect] listening on port {}\n", parameters.port_listen);

                          const char* kProtocol = SSL ? "https" : "http";
                          print("[incppect] {}://localhost:{}/\n", kProtocol, parameters.port_listen);
                       }
                    })
            .run();
      }

      void update()
      {
         for (auto& [clientId, cd] : clientData) {
            if (socketData[clientId]->ws->getBufferedAmount()) {
               print(
                  "[incppect] warning: buffered amount = {}, not sending updates to client {}. waiting for buffer to "
                  "drain\n",
                  socketData[clientId]->ws->getBufferedAmount(), clientId);
               continue;
            }

            auto& buf = cd.buf;
            auto& prev = cd.prev;
            auto& diff = cd.diff;

            buf.clear();

            uint32_t typeAll = 0;
            buf.append((char*)(&typeAll), sizeof(typeAll));

            for (auto& [requestId, req] : cd.requests) {
               auto& getter = getters[req.getterId];
               auto tCur = timestamp();
               if (((req.t_last_request_timeout_ms < 0 && req.tLastRequested_ms > 0) ||
                    (tCur - req.tLastRequested_ms < req.t_last_request_timeout_ms)) &&
                   tCur - req.tLastUpdated_ms > req.tMinUpdate_ms) {
                  if (req.t_last_request_timeout_ms < 0) {
                     req.tLastRequested_ms = 0;
                  }

                  req.curData = getter(req.idxs);
                  req.tLastUpdated_ms = tCur;

                  const int kPadding = 4;

                  int dataSize_bytes = req.curData.size();
                  int padding_bytes = 0;
                  {
                     int r = dataSize_bytes % kPadding;
                     while (r > 0 && r < kPadding) {
                        ++dataSize_bytes;
                        ++padding_bytes;
                        ++r;
                     }
                  }

                  int32_t type = 0; // full update
                  if (req.prevData.size() == req.curData.size() + padding_bytes && req.curData.size() > 256) {
                     type = 1; // run-length encoding of diff
                  }

                  buf.append((char*)(&requestId), sizeof(requestId));
                  buf.append((char*)(&type), sizeof(type));

                  if (type == 0) {
                     buf.append((char*)(&dataSize_bytes), sizeof(dataSize_bytes));
                     buf.append(req.curData);
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
                     req.diffData.clear();

                     for (int i = 0; i < (int)req.curData.size(); i += 4) {
                        std::memcpy(&a, req.prevData.data() + i, sizeof(uint32_t));
                        std::memcpy(&b, req.curData.data() + i, sizeof(uint32_t));
                        a = a ^ b;
                        if (a == c) {
                           ++n;
                        }
                        else {
                           if (n > 0) {
                              req.diffData.append((char*)(&n), sizeof(uint32_t));
                              req.diffData.append((char*)(&c), sizeof(uint32_t));
                           }
                           n = 1;
                           c = a;
                        }
                     }

                     if (req.curData.size() % 4 != 0) {
                        a = 0;
                        b = 0;
                        uint32_t i = (req.curData.size() / 4) * 4;
                        uint32_t k = req.curData.size() - i;
                        std::memcpy(&a, req.prevData.data() + i, k);
                        std::memcpy(&b, req.curData.data() + i, k);
                        a = a ^ b;
                        if (a == c) {
                           ++n;
                        }
                        else {
                           req.diffData.append((char*)(&n), sizeof(uint32_t));
                           req.diffData.append((char*)(&c), sizeof(uint32_t));
                           n = 1;
                           c = a;
                        }
                     }

                     req.diffData.append((char*)(&n), sizeof(uint32_t));
                     req.diffData.append((char*)(&c), sizeof(uint32_t));

                     dataSize_bytes = req.diffData.size();
                     buf.append((char*)(&dataSize_bytes), sizeof(dataSize_bytes));
                     buf.append(req.diffData);
                  }

                  req.prevData = req.curData;
               }
            }

            if (buf.size() > 4) {
               if (buf.size() == prev.size() && buf.size() > 256) {
                  uint32_t a = 0;
                  uint32_t b = 0;
                  uint32_t c = 0;
                  uint32_t n = 0;
                  diff.clear();

                  uint32_t typeAll = 1;
                  diff.append((char*)(&typeAll), sizeof(typeAll));

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

                  if (socketData[clientId]->ws->send({diff.data(), diff.size()}, uWS::OpCode::BINARY, doCompress) ==
                      false) {
                     print("[incpeect] warning: backpressure for client {} increased \n", clientId);
                  }
               }
               else {
                  if ((int32_t)buf.size() > parameters.max_payload) {
                     print("[incppect] warning: buffer size ({}) exceeds maxPayloadLength ({})\n", (int)buf.size(),
                           parameters.max_payload);
                  }

                  // compress only for message larger than 64 bytes
                  const bool doCompress = buf.size() > 64;
                  if (socketData[clientId]->ws->send({buf.data(), buf.size()}, uWS::OpCode::BINARY, doCompress) ==
                      false) {
                     print("[incpeect] warning: backpressure for client {} increased \n", clientId);
                  }
               }

               txTotal_bytes += buf.size();

               prev = buf;
            }
         }
      }
   };
}
