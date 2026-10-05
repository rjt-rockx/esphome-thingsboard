# `thingsboard_mqtt_ota`

OTA platform for the [`thingsboard_mqtt`](../thingsboard_mqtt/) transport.
Implements chunked-binary firmware download over MQTT per ThingsBoard's
MQTT device API (`v2/fw/*`), as documented at
<https://thingsboard.io/docs/reference/mqtt-api/#firmware-api>.

## YAML

```yaml
thingsboard:
  server_url: !secret thingsboard_server_url

thingsboard_mqtt:
  thingsboard_id: thingsboard_component
  broker: !secret thingsboard_mqtt_broker
  device_token: !secret tb_device_token

ota:
  - platform: thingsboard_mqtt_ota
    thingsboard_id: thingsboard_component
    chunk_size: 4096   # optional, default 4096, range 512-65536
    on_begin: ...
    on_progress: ...
    on_end: ...
    on_error: ...
```

Standard ESPHome OTA triggers (`on_begin` / `on_progress` / `on_end` /
`on_error`) are inherited from the base `ota:` schema.

## How it works

1. ThingsBoard pushes firmware metadata as shared attributes
   (`fw_title`, `fw_version`, `fw_size`, `fw_checksum`, `fw_checksum_algorithm`).
   An advertisement whose `fw_title` differs from the project name is
   reported `FAILED`; one whose `fw_version` equals the running project
   version is reported `UPDATED` and nothing is downloaded.
2. The component publishes chunk requests on
   `v2/fw/request/{request_id}/chunk/{chunk_index}` with a payload equal to
   the requested chunk size.
3. The server responds on `v2/fw/response/{request_id}/chunk/{chunk_index}`
   with the binary payload. The platform writes each chunk to the next OTA
   partition, verifies the checksum (SHA256 or MD5), and reboots.
4. State transitions (`DOWNLOADING` → `DOWNLOADED` → `VERIFIED` → `UPDATING`
   → `UPDATED`, or `FAILED` with `fw_error`) are reported back as telemetry
   so ThingsBoard's firmware dashboard can track progress.

## QoS1 redelivery

Chunk responses are subscribed at QoS1, so the broker redelivers any chunk
it has not seen ACKed yet. Duplicate and older chunk indices are therefore
normal, not errors: they are ignored, and only a forward gap (a chunk that
was never requested) aborts the update. Each accepted chunk is written to
flash exactly once, in order. The flash write and the next-chunk request run
on the main loop rather than the MQTT client task, so a flash erase never
stalls the socket (a stalled socket is what triggers the redelivery storm in
the first place). Verified end to end on ESP32-C6 hardware against a
ThingsBoard staging tenant.

## `chunk_size`

`chunk_size` is bounded by the broker's `maxPayloadSize` limit (see
`getSessionLimits`, exposed in the core component's diagnostic text
sensor). 4 KiB is the ThingsBoard default and works for all known broker
configurations.
