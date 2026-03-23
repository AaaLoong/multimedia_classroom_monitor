# Qt 多媒体教室设备监控系统（前后端分离，全 Qt 栈）

## 组件概览
- `sim_backend`：后端 HTTP 服务（cpp-httplib），MQTT 桥接教室端，随机 3–5 秒下发命令，支持锁定、报修、MySQL 持久化。
- `sim_admin_qml`：管理端 QML 界面，需登录。
- `sim_classroom_qml`：教室端 QML 界面（精简监控/控制），无登录，支持锁定时锁屏。
- `sim_device_client`：独立设备仿真客户端（MQTT + 单独 GUI），仅负责模拟心跳、状态、故障与执行延迟。

## 依赖
- Qt 6.5+（Core / Quick / QuickControls2 / Qml / Network / Sql / Concurrent）
- paho-mqtt-cpp（CMake 包 `PahoMqttCpp`，需 `paho-mqttpp3`）
- cpp-httplib（已内置 `third_party/httplib.h`）
- MySQL 8.4.x（强制 QMYSQL 持久化，可通过环境变量配置）
- CMake ≥ 3.21，C++17

## 构建
```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.x/gcc_64" \
  -DPAHO_MQTT_CPP_INCLUDE_DIR="/usr/local/include" \
  -DPAHO_MQTT_CPP_LIBRARY="/usr/local/lib/libpaho-mqttpp3.dylib"
cmake --build build
```

## 运行顺序
1) 启动 MQTT Broker（mosquitto，默认 `tcp://127.0.0.1:1883`）
2) 教室仿真（可选，用于模拟设备）：`build/sim_device_client A101 tcp://127.0.0.1:1883`
   - 也可使用脚本：`scripts/start_simulator.sh`
3) 后端服务：推荐使用统一配置文件，不要求客户手工导出环境变量
   - 先复制 `config/runtime.env.example` 为 `config/runtime.env`
   - 填写 `SIM_DB_HOST` / `SIM_DB_PORT` / `SIM_DB_NAME` / `SIM_DB_USER` / `SIM_DB_PASS`
   - 如需改地址，可同时填写 `SIM_BIND_HOST`、`SIM_BACKEND_PORT`、`SIM_MQTT_URL`、`BACKEND_URL`
   - 首次启动若管理员表为空，仍可额外提供 `SIM_ADMIN_INIT_USERNAME` / `SIM_ADMIN_INIT_PASSWORD`
   - 然后直接运行 `scripts/start_backend.sh` 或 `scripts/start_all.sh`
4) 管理端 QML：`build/sim_admin_qml [http://backend:8080]`
5) 教室端 QML：`build/sim_classroom_qml [roomId] [http://backend:8080]`（默认房间 A101，无需登录）

环境变量仍可覆盖配置文件，但发布版默认走 `config/runtime.env`。

## 行为与约束
- 设备示例：proj-01、amp-01、pc-01、mic-01、screen-01；初始 off/离线。
- 启动随机延迟 3–5s；故障率 <2%；网络随机掉线（1.2%/20s）置 offline。
- 锁定：仅管理端可操作锁；锁定后教室端无法查看/操作，显示锁屏；管理端仍可查看/控制。
- 所有前端操作走 HTTP，MQTT 只在后端↔教室仿真间使用。
- 仿真端已与主程序构建解耦：独立资源、独立启动入口、独立 MQTT 依赖。

## 发布版配置示例
```bash
cp config/runtime.env.example config/runtime.env
vim config/runtime.env
./scripts/start_backend.sh
```

## API 摘要（后端）
- `GET /health`：公共健康检查，返回 `{status, transport}`。
- `POST /login`：body `{username,password}`，返回 `{token, username, expires_at}`。
- `POST /logout`：管理员退出登录，需 Bearer Token。
- `GET /lock` / `POST /lock` / `POST /unlock`：锁定控制（lock/unlock 需 Bearer Token）。
- `GET /devices`：管理员可查看全量设备；未鉴权请求必须带 `room_id`，否则返回 `401`。
- `POST /devices/{room}/{dev}/command`：body `{action}`，随机延迟 3000–5000ms 后经 MQTT 下发。
- `POST /devices/{room}/{dev}/report`：body `{message}`，报修/告警上报。
- `POST /heartbeat/{room}`：前端手动触发一次心跳。

## 备注
- MySQL 强制使用 QMYSQL 驱动，驱动缺失或连接失败时后端会退出；如在 macOS 上通过 Homebrew 安装，可设置 `QT_PLUGIN_PATH=/opt/homebrew/opt/qt/plugins` 与 `DYLD_LIBRARY_PATH=/opt/homebrew/lib`.
- `config/runtime.env` 用于保存本机部署参数，脚本会自动加载；该文件已加入 `.gitignore`，不要把真实密码提交进仓库。
- QML 界面内置操作等待遮罩与锁屏提示；教室端无鉴权。
- MQTT 主题仍为 `classroom/<roomId>/cmd|state|alert/<deviceId>`，心跳 `classroom/<roomId>/heartbeat`。
- 管理员密码已改为数据库中的 PBKDF2 哈希存储，代码和脚本中不再保留默认管理员明文口令。
- 本项目当前部署取舍为 `HTTP + MQTT`，优先保证功能闭环和答辩演示，不再引入 HTTPS/TLS 证书链配置。
