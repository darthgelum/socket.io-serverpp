# socket.io-serverpp

A C++17 implementation of a Socket.IO server with a clean, layered architecture:
- Transport layer (Boost.Beast): WebSocket, HTTP long‑polling, or a single unified port with upgrade (polling -> WebSocket)
- Engine.IO v4 layer: sessions, ping/pong, batching, upgrade coordination
- Socket.IO layer: namespaces, events, simple handler API

Vendored RapidJSON is used for lightweight JSON needs. No external web server is required; everything runs on Boost.Asio/Beast.

## Features
- WebSocket transport (Boost.Beast)
- HTTP long‑polling transport
- Unified transport on one port with automatic upgrade (polling → WebSocket)
- Namespaces and per‑socket event handlers
- Simple factory API: SocketIOServer::create(io_service)
- Minimal examples and browser test pages included

## Requirements
- C++17 compiler (GCC/Clang)
- CMake ≥ 3.10
- Boost (headers are sufficient in this repo; link to system Boost libraries)
- pkg-config and libuuid (for UUID generation)

Optional:
- GoogleTest (fetched automatically only if tests are enabled later)

On Debian/Ubuntu you’ll typically need:
- build-essential cmake pkg-config libboost-dev uuid-dev

## Build
```sh
# from the repo root
mkdir -p build && cd build
cmake ..
cmake --build . -j
```

This produces example executables:
- build/usage          (unified: polling + WebSocket on a single port)
- build/usage_polling  (separate ports: WebSocket and polling)

## Run the examples
- Unified, single port:
  - Starts Engine.IO polling and WebSocket on port 8081 with auto‑upgrade
  - File: examples/usage.cpp
  - Run: build/usage

You should see log messages indicating the listening ports.

## Try it in the browser
Open one of the provided pages in your browser (double‑click the file or use a small static file server):
- index_v4.html           – WebSocket only
- index_v4_polling.html   – Polling only
- index_v4_upgrade.html   – Polling + WebSocket (auto‑upgrade)

Then set “Server URL” in the page to match the example:
- Unified (build/usage on 8081): http://localhost:8081
- Polling only (build/usage_polling on 8080): http://localhost:8080
- WebSocket only (build/usage_polling on 8081): http://localhost:8081

Open the page in two tabs and send a message to see broadcasts.

## Quick start (C++)
```cpp
#include "socket.io-serverpp/socketio-serverpp.hpp"
#include <boost/asio.hpp>

int main() {
  using namespace socketio_serverpp::lib;
  boost::asio::io_service io;

  auto server = socketio::SocketIOServer::create(io);
  Logger::instance().set_level(LogLevel::DEBUG);

  // Unified single-port server (polling + websocket with upgrade)
  server->listen(8081);

  // Register handlers on the default namespace
  auto nsp = server->get_namespace("/");
  nsp->onConnection([&](Socket& s){
    s.emit("welcome", std::string("Hello from server!"));
    s.on("chat_message", [&nsp](const Event& e){ nsp->emit("broadcast_message", e.data()); });
  });

  io.run();
}
```

## Architecture (high level)
- Transport (socket.io-serverpp/transport)
  - WebSocketTransport: Boost.Beast WebSocket implementation
  - PollingTransport: Engine.IO HTTP long‑polling
  - UnifiedTransport: both on a single port, handles CORS, long‑poll holding, and upgrade
- Engine.IO (socket.io-serverpp/engineio)
  - EngineIOServer, EngineIOSession: session registry, ping timers, message framing, upgrade coordination
- Socket.IO (socket.io-serverpp/socketio)
  - SocketIOServer, SocketNamespace, Socket: namespaces, event routing, per‑socket callbacks

## Project layout
- socket.io-serverpp/ … library sources
- examples/ … tiny runnable servers
- index_v4*.html … quick browser clients for manual testing
- CMakeLists.txt … builds the examples (library targets can be added later)

## Notes & status
- Binary events/ACKs are placeholders in parts of the Socket.IO layer
- More tests and hardening are welcome
- RapidJSON is vendored under socket.io-serverpp/lib/rapidjson

## Troubleshooting
- Port already in use: change the port in examples or stop the other process
- CORS: the transports add permissive headers for local testing; adjust for production
- Logging: set level via Logger::instance().set_level(LogLevel::<LEVEL>)
