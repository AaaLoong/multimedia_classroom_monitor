# Repository Guidelines

## Project Structure & Modules
- `src/`: C++17 sources (Qt/QML frontends, REST/MQTT backend, DbStore).  
- `qml/`: QML UI assets for admin/classroom.  
- `third_party/`: vendored headers (e.g., `httplib.h`).  
- `CMakeLists.txt`: top-level build config.  
- External build directory: `/Users/achang/cmm_runtime/build/Qt_6_10_1_for_macOS-Debug`.

## Build, Run, and Dev Commands
- Configure & build:  
  ```bash
  cmake -S . -B /Users/achang/cmm_runtime/build/Qt_6_10_1_for_macOS-Debug \
    -G Ninja \
    -DCMAKE_PREFIX_PATH="/path/to/Qt/6.x/gcc_64" \
    -DPAHO_MQTT_CPP_INCLUDE_DIR="/usr/local/include" \
    -DPAHO_MQTT_CPP_LIBRARY="/usr/local/lib/libpaho-mqttpp3.dylib"
  cmake --build /Users/achang/cmm_runtime/build/Qt_6_10_1_for_macOS-Debug
  ```
- Run backend (HTTP+MQTT bridge, MySQL required):  
  ```bash
  SIM_DB_HOST=127.0.0.1 SIM_DB_PORT=3307 \
  SIM_DB_NAME=cmm_sim SIM_DB_USER=root SIM_DB_PASS=1234 \
  QT_PLUGIN_PATH=/opt/homebrew/opt/qt/plugins \
  /Users/achang/cmm_runtime/build/Qt_6_10_1_for_macOS-Debug/sim_backend
  ```
- Run admin UI (QML): `/Users/achang/cmm_runtime/build/Qt_6_10_1_for_macOS-Debug/sim_admin_qml http://127.0.0.1:8080`  
- Run classroom UI (QML): `/Users/achang/cmm_runtime/build/Qt_6_10_1_for_macOS-Debug/sim_classroom_qml A101 http://127.0.0.1:8080`  
- Run classroom simulator (MQTT): `/Users/achang/cmm_runtime/build/Qt_6_10_1_for_macOS-Debug/sim_classroom A101 tcp://127.0.0.1:1883`

## Coding Style & Naming
- C++: clang-format style (Qt conventions); prefer PascalCase for types, camelCase for methods/variables; 2–4 spaces, no tabs.  
- QML: camelCase ids, concise components; keep logic in C++/REST client where possible.  
- Files/dirs in ASCII, English names only.  
- Comments: brief, only for non-obvious logic.

## Testing Guidelines
- No automated test suite yet; perform manual runs: backend + mosquitto + MySQL, then admin/classroom QML and `sim_classroom` for state/command flow.  
- Validate: device list retrieval, lock/unlock, single control, batch on/off, heartbeat updates, fault/report flow, DB persistence.

## Commit & PR Guidelines
- Commits: concise present-tense summaries (e.g., “Add MQTT heartbeat handler”, “Fix lock check on command route”).  
- Scope small; avoid unrelated formatting noise.  
- PRs (if used): include what/why, run/build status, screenshots for UI changes, and mention any DB/MQTT config impacts.

## Architecture Notes
- Frontend: Qt Quick/QML, admin (ruoyi-like layout) + classroom (simplified control).  
- Backend: C++/Qt + cpp-httplib REST, paho-mqtt-cpp for MQTT bridge, MySQL persistence via DbStore (QMYSQL).  
- Messaging: MQTT for state/heartbeat/alerts; REST for control, lock, listings, login.  
- DB: MySQL 8.4 (tables: devices, alerts, operations, meta for lock flag). Keep env vars in runtime config, not in code.
