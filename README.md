# Qt 多媒体教室管理系统

自有项目，目标是完成“管理端 + 教室端 + 后端 + 设备仿真端”的功能闭环。

## 技术栈
- 前端：Qt Quick / QML
- 后端：Qt C++ + `cpp-httplib`
- 设备链路：HTTP + MQTT
- 持久化：MySQL
- MQTT SDK：Paho MQTT C++

## 组件
- `sim_backend`：后端 HTTP 服务，负责认证、设备查询、锁定、命令受理、MQTT 下发、SSE 推送、MySQL 持久化。
- `sim_admin_qml`：管理端 QML，支持登录、设备总览、锁定、告警处理、管理员管理。
- `sim_classroom_qml`：教室端 QML，支持终端绑定、单设备控制、上课/下课、报修。
- `sim_device_client`：设备仿真端，支持 GUI 与 headless 两种模式。

## 目录
- 仓库：`/Users/achang/Desktop/multimedia_classroom_monitor`
- 外置运行时：`/Users/achang/cmm_runtime`
- 外置脚本：`/Users/achang/cmm_runtime/scripts`
- 外置构建目录：
  - Debug：`/Users/achang/cmm_runtime/build/Qt_6_10_1_for_macOS-Debug`
  - Release：`/Users/achang/cmm_runtime/build/Qt_6_10_1_for_macOS-Release`

## 依赖
- Qt 6.5+
- CMake 3.21+
- Ninja
- MySQL 8
- Mosquitto 或兼容 MQTT Broker
- `PahoMqttCpp`

## 构建
```bash
cmake -S /Users/achang/Desktop/multimedia_classroom_monitor \
  -B /Users/achang/cmm_runtime/build/Qt_6_10_1_for_macOS-Release \
  -G Ninja \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.x/macos"

cmake --build /Users/achang/cmm_runtime/build/Qt_6_10_1_for_macOS-Release --parallel
```

## 运行
推荐优先使用外置脚本：

```bash
/Users/achang/cmm_runtime/scripts/start_backend.sh
/Users/achang/cmm_runtime/scripts/start_clients.sh
/Users/achang/cmm_runtime/scripts/start_device.sh
```

Release 启动示例：

```bash
CMM_BUILD_TYPE=Release /Users/achang/cmm_runtime/scripts/start_backend.sh
```

## 配置
运行配置通过 `config/runtime.env` / `config/runtime.env.local` 注入。程序入口会自动加载，不依赖 shell 手工 `export`。

参考模板：

```bash
cp /Users/achang/Desktop/multimedia_classroom_monitor/config/runtime.env.example \
   /Users/achang/Desktop/multimedia_classroom_monitor/config/runtime.env
```

关键参数：
- `SIM_BIND_HOST`
- `SIM_BACKEND_PORT`
- `SIM_MQTT_URL`
- `SIM_DB_HOST`
- `SIM_DB_PORT`
- `SIM_DB_NAME`
- `SIM_DB_USER`
- `SIM_DB_PASS`
- `SIM_HTTP_WORKER_THREADS`
- `SIM_HTTP_KEEP_ALIVE_MAX_COUNT`
- `SIM_HTTP_KEEP_ALIVE_TIMEOUT_SEC`
- `SIM_WORKER_THREADS`
- `SIM_STATE_WORKER_THREADS`
- `SIM_PERSIST_WORKER_THREADS`
- `SIM_DB_POOL_SIZE`

## 当前架构要点
- 前端到后端：HTTP + SSE
- 后端到设备：MQTT
- 命令热路径：内存状态机为主，错误异步落库
- `command_jobs` 仅保留 `timeout` / `failed`
- `command_events` 已弃用
- 数据库连接为可借还连接池，避免线程永久占坑
- 后端命令受理接口对重复同目标命令采用幂等 `202 Accepted`

## MQTT 与设备仿真
- MQTT topic：`classroom/<roomId>/cmd|state|alert/<deviceId>`
- headless 设备进程已修复为“单进程只服务一个房间”
- fleet 启动脚本会：
  - 分批启动设备
  - 等待每个房间 `mqtt_ready=true`
  - 启动前自动清理残留 `emqtt_bench` 占用，避免 broker 连接被历史 benchmark 卡死

## 压测脚本
位于 `/Users/achang/cmm_runtime/scripts/perf`：

- `k6_500_users_10m.js`：纯 HTTP 稳态压测
- `k6_500_cold_start_burst.js`：冷启动/突发压测
- `k6_mqtt_command_e2e.js`：HTTP 命令 + MQTT ACK 全链路压测
- `run_k6_mqtt_command_e2e.sh`：E2E 编排脚本
- `start_headless_device_fleet.sh`：批量 headless 设备启动
- `run_emqtt_bench_mqtt_ingest.sh`：MQTT 入站压测

## 当前验证结果
- `k6_500_users_10m`
  - `checks = 100%`
  - `http_req_failed = 0%`
  - `p95 = 1.63ms`
  - `p99 = 7.12ms`
- `k6_mqtt_command_e2e`
  - `100 房间 / 100 VU` 下已达到 `command accepted = 100%`
  - `command_ack_timeout = 0`
  - `http_req_failed = 0%`

## 说明
- 项目口径是“本科原型系统，闭环可演示、并发能力经过压测验证”，不要对外表述成生产级分布式系统。
- 默认不启用 HTTPS、MQTT TLS、证书体系。
- 教室端和仿真端不再默认初始化 `A101`；房间号必须显式传入或由后端绑定。
