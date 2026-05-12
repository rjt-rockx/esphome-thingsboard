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

## Picking a transport

| Use case | Pick |
| --- | --- |
| Fleets above ~100 devices, sub-second telemetry, RPC + shared attribute push | `thingsboard_mqtt` |
| One-off devices, polling cadence above a few seconds, restrictive network | `thingsboard_http` |

MQTT keeps a persistent broker session, so the server can push RPC and
shared-attribute updates without the device polling. HTTP uses long-poll
under the hood: each device holds an open request to TB for up to
`poll_timeout`, which costs more per-device server resources at scale.

## TLS

TLS is opt-in on both transports.

**MQTT** — supply server CA, client certificate, or both via
`thingsboard_mqtt:`. Port defaults to `8883` when any TLS material is
present, `1883` otherwise.

```yaml
thingsboard_mqtt:
  thingsboard_id: thingsboard_component
  broker: !secret thingsboard_mqtt_broker
  # port: 8883          # optional: derived from TLS presence
  server_ca_pem: !secret thingsboard_server_ca   # for ACCESS_TOKEN over TLS
  credentials:                                   # for X.509 mTLS
    type: X509_CERTIFICATE
    certificate_pem: !secret tb_client_cert
    private_key_pem: !secret tb_client_key
  device_token: !secret thingsboard_device_token  # ignored when X509
```

`server_ca_pem` alone enables TLS with `ACCESS_TOKEN` or `MQTT_BASIC` auth
(no client cert). `X509_CERTIFICATE` requires both `certificate_pem` and
`private_key_pem`; `server_ca_pem` is recommended alongside it.

**HTTP** — TLS material lives on the parent `http_request:` component
(provided by ESPHome itself). The `thingsboard_http:` block reuses
whatever the `http_request:` it points at is configured with:

```yaml
http_request:
  id: tb_http
  timeout: 10s
  verify_ssl: true
  ca_certificate: !secret thingsboard_server_ca
```

Set `verify_ssl: false` only for local TB instances with self-signed
material that you can't get a copy of, never in production.

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
