# esphome-thingsboard

ESPHome external components for [ThingsBoard](https://thingsboard.io) IoT
platform integration. End-to-end support for the ThingsBoard device API
over MQTT or HTTP — telemetry, attributes, RPC, provisioning, claiming,
and OTA — wired through ESPHome's entity discovery so per-component
telemetry and RPC come for free.

## Components

| Component | Purpose |
| --- | --- |
| `thingsboard` | Transport-agnostic core: telemetry batching, entity discovery, RPC dispatch, attribute cache, OTA state machine, action surface (`thingsboard.send_telemetry`, etc.) |
| `thingsboard_mqtt` | MQTT device API ([ref](https://thingsboard.io/docs/reference/mqtt-api/)) |
| `thingsboard_mqtt_ota` | Chunked-binary OTA over MQTT (`v2/fw/*`), exposed as an `ota:` platform |
| `thingsboard_http` | HTTP device API ([ref](https://thingsboard.io/docs/reference/http-api/)) |
| `thingsboard_http_ota` | Streaming HTTPS OTA, exposed as an `ota:` platform |

A firmware uses **exactly one** of `thingsboard_mqtt:` or `thingsboard_http:`
— the core component's final-validator rejects builds that configure both
or neither. See each component's README under `components/` for the YAML
surface and the implemented slice of the ThingsBoard device API.

## Quick start — MQTT

```yaml
external_components:
  - source: github://rjt-rockx/esphome-thingsboard@main
    components: [thingsboard, thingsboard_mqtt, thingsboard_mqtt_ota]

thingsboard:
  id: thingsboard_component
  server_url: !secret thingsboard_server_url

thingsboard_mqtt:
  thingsboard_id: thingsboard_component
  broker: !secret thingsboard_mqtt_broker
  device_token: !secret thingsboard_device_token

ota:
  - platform: thingsboard_mqtt_ota
    thingsboard_id: thingsboard_component
```

## Quick start — HTTP

```yaml
external_components:
  - source: github://rjt-rockx/esphome-thingsboard@main
    components: [thingsboard, thingsboard_http, thingsboard_http_ota]

http_request:
  id: tb_http
  timeout: 10s

thingsboard:
  id: thingsboard_component
  server_url: !secret thingsboard_server_url

thingsboard_http:
  thingsboard_id: thingsboard_component
  http_request_id: tb_http
  device_token: !secret thingsboard_device_token
  poll_interval: 3s
  poll_timeout: 3s

ota:
  - platform: thingsboard_http_ota
    thingsboard_id: thingsboard_component
    http_request_id: tb_http
```

## Versioning

Tags are semver-ish: `vMAJOR.MINOR.PATCH`. Pin to a tag, not `@main`, so
a transport-side bug fix doesn't silently land in production firmware.
Breaking changes bump the minor before `v1.0.0`; afterwards, breaking
changes bump major.

## Platform support

ESP32 (ESP-IDF framework) is the primary target. The MQTT transport
pulls in `espressif/mqtt` for IDF ≥ 6.0 or the built-in `mqtt` component
for older IDF. The HTTP transport uses ESPHome's `http_request`
component and its `esp_http_client` backend.

## License

MIT — see [`LICENSE`](LICENSE).
