# `thingsboard_http_ota`

OTA platform for the [`thingsboard_http`](../thingsboard_http/) transport.
Implements HTTPS firmware download per ThingsBoard's HTTP device API, as
documented at
<https://thingsboard.io/docs/reference/http-api/#download-firmware>.

## YAML

```yaml
thingsboard:
  server_url: !secret thingsboard_server_url

http_request:
  id: tb_http
  timeout: 10s

thingsboard_http:
  thingsboard_id: thingsboard_component
  http_request_id: tb_http
  device_token: !secret tb_device_token

ota:
  - platform: thingsboard_http_ota
    thingsboard_id: thingsboard_component
    http_request_id: tb_http
    on_begin: ...
    on_progress: ...
    on_end: ...
    on_error: ...
```

Standard ESPHome OTA triggers (`on_begin` / `on_progress` / `on_end` /
`on_error`) are inherited from the base `ota:` schema. Reuses the same
`http_request:` component the transport already uses.

## How it works

1. ThingsBoard pushes firmware metadata as shared attributes
   (`fw_title`, `fw_version`, `fw_size`, `fw_checksum`,
   `fw_checksum_algorithm`).
2. The component GETs `/api/v1/$TOKEN/firmware?title=$TITLE&version=$VERSION`
   and streams the response into the next OTA partition in 512-byte reads.
3. When `fw_checksum_algorithm == "MD5"`, the value is handed to the OTA
   backend for verification. Other algorithms are accepted but not verified
   on-device (ThingsBoard itself only emits MD5 for HTTP-served firmware).
4. State transitions (`DOWNLOADING` → `DOWNLOADED` → `VERIFIED` →
   `UPDATING` → `UPDATED`, or `FAILED` with `fw_error`) are reported back as
   telemetry so ThingsBoard's firmware dashboard can track progress.

There is no chunk-size schema knob: the response body is read in fixed
512-byte buffers regardless of payload.
