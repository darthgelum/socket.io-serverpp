socket.io-serverpp (modernized fork)
====================================

This repository is a maintained fork of https://github.com/pnxs/socket.io-serverpp.

It upgrades the original codebase to:

- Build with CMake (no SCons required)
- Use modern C++ standards (C++17)
- Improve compatibility with Socket.IO v5 (Engine.IO v4, WebSocket-only)

The core is still based on the excellent header-only [websocket++](https://github.com/zaphoyd/websocketpp).


Overview
--------
socket.io-serverpp implements the essential parts of the Socket.IO protocol on top of a WebSocket server. It focuses on WebSocket-only transport (no HTTP long-polling) which is enough for most modern clients.

Key capabilities:

- Engine.IO v4 handshake: sends OPEN (type '0') with sid, pingInterval, pingTimeout
- Heartbeats: periodic ping ('2') and pong ('3') handling, disconnect on timeout
- Socket.IO v5 framing inside Engine.IO message (type '4'):
	- CONNECT (0), DISCONNECT (1), EVENT (2)
- Namespaces: default namespace "" (aka "/") and custom namespaces via `Server::of("/chat")`
- Simple event dispatching from namespaces or individual sockets


Architecture (short)
--------------------
- `socket.io-serverpp::Server` wraps a `websocketpp` server and a small SCGI service used by the original project; only the WebSocket path is required for Socket.IO.
- On WebSocket open: generate or reuse a sid (from query `sid` when present), send Engine.IO OPEN packet.
- Heartbeat: the server pings clients at `pingInterval`; clients must respond with pong within `pingTimeout` or the connection is closed.
- Socket.IO handling: inside Engine.IO message ('4'), parse the Socket.IO packet (CONNECT, EVENT, etc.), then dispatch to the matching `SocketNamespace`.
- Namespaces are created with `Server::of("/your-nsp")` and expose `onConnection`/`emit` helpers.


Repository layout
-----------------
- `socket.io-serverpp/`      – core headers and protocol logic
- `examples/test1.cpp`       – minimal server that listens on port 9003 and registers a namespace example
- `examples/usage.cpp`       – another example entry point
- `index_v5.html`            – a minimal Socket.IO v4/v5 browser client via CDN (recommended for testing)
- `index.html`               – a lower-level demo that shows Socket.IO-style frames over raw WebSocket; used during development and pairs with `examples/test1.cpp`
- `external/websocketpp/`    – vendored websocket++


Build requirements
------------------
- CMake >= 3.10
- A C++17 compiler (GCC/Clang)
- pkg-config
- UUID development headers (libuuid)

Install libuuid (one of):

- Debian/Ubuntu
	- `sudo apt-get update && sudo apt-get install -y uuid-dev pkg-config`
- Fedora/RHEL
	- `sudo dnf install -y libuuid-devel pkg-config`


Build
-----
1) Configure and build:

```
cmake -S . -B build
cmake --build build -j
```

This produces example binaries:

- `build/test1`
- `build/usage`


Run the example server
----------------------
The `examples/test1.cpp` server listens on port `9003` and exposes namespaces (e.g. `/chat`). Start it:

```
./build/test1
```


Test with the browser client
----------------------------
You can test using either HTML page at the repo root.

 - `index_v4.html` (recommended)
	 - Standard Socket.IO client loaded from CDN.
	 - Steps:
		 - Open `index_v5.html` in your browser.
		 - Server URL: `http://localhost:9001`
		 - Namespace: `/` 
		 - Click Connect. You should see logs for connect, heartbeat, and events.


Notes and compatibility
-----------------------
- Transport: WebSocket-only. Modern Socket.IO clients negotiate fine as long as they can connect via WebSocket.
- Heartbeat: implemented according to Engine.IO v4. The server will close connections that don't pong in time.
- SIDs: the server accepts a `sid` from the client (e.g. reconnect) or generates a new one.


Troubleshooting
---------------
- Connection timeout or `connect_error` in the browser:
	- Ensure the server is running and listening on the expected port.
	- Verify the URL uses `http://localhost:9001` (not HTTPS) when testing locally.
	- Check browser console network logs for WebSocket errors.
- No events received:
	- Confirm namespace selection ("/" vs "/chat").
	- Ensure you're emitting the same event name on both client and server.