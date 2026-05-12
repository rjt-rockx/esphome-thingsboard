# `thingsboard_http`

HTTP transport for the [`thingsboard`](../thingsboard/) ESPHome custom component.
Implements ThingsBoard's HTTP device API as documented at
<https://thingsboard.io/docs/reference/http-api/>.

End firmware uses **either** `thingsboard_mqtt:` **or** `thingsboard_http:` — never
both. The core component enforces this via its `FINAL_VALIDATE_SCHEMA`.

## YAML

```yaml
thingsboard:
  server_url: !secret thingsboard_server_url
  device_name: ${name}

http_request:
  id: tb_http
  timeout: 10s

thingsboard_http:
  thingsboard_id: thingsboard_component
  http_request_id: tb_http
  device_token: "<your-token>"
  poll_interval: 3s
  poll_timeout: 3s # 0 = short-poll (TB returns immediately)
```

### Polling cadence

`/rpc` and `/attributes/updates` long-poll the server by passing
`?timeout=<poll_timeout>`. The synchronous `esp_http_client` call runs on a
dedicated worker task, so `poll_timeout` is bounded by `http_request`'s
socket timeout rather than the system WDT.

Set `poll_timeout: 0s` for pure short-poll — TB returns immediately and
each request completes in one network round-trip. Trade-off:
shared-attribute pushes only flush _during_ an active poll, so a
short-poll firmware can silently miss changes between cadence ticks. The
recommended default is `poll_interval: 3s` + `poll_timeout: 3s`.

## TB device API coverage

Operations are sourced from the upstream ThingsBoard
[HTTP device API](https://thingsboard.io/docs/reference/http-api/) and
[device provisioning](https://thingsboard.io/docs/user-guide/device-provisioning/)
references.

| Operation                              | Status        | Notes                                                                                   |
| -------------------------------------- | ------------- | --------------------------------------------------------------------------------------- |
| Telemetry upload (server-ts)           | implemented   | POST `/api/v1/$TOKEN/telemetry`                                                         |
| Telemetry upload (client-ts)           | implemented   | same endpoint, `{ts, values}` shape passes through                                      |
| Client-attribute upload                | implemented   | POST `/api/v1/$TOKEN/attributes`                                                        |
| Attribute request                      | implemented   | GET `/api/v1/$TOKEN/attributes?clientKeys=...&sharedKeys=...`                           |
| Shared-attribute push (long-poll)      | implemented   | GET `/api/v1/$TOKEN/attributes/updates?timeout=<poll_timeout>`                          |
| Server-side RPC (TB→device, long-poll) | implemented   | GET `/api/v1/$TOKEN/rpc?timeout=<poll_timeout>`; response POST `/api/v1/$TOKEN/rpc/$id` |
| Client-side RPC (device→TB)            | implemented   | POST `/api/v1/$TOKEN/rpc`; sync response body re-dispatched                             |
| Device claim                           | implemented   | POST `/api/v1/$TOKEN/claim`                                                             |
| Provisioning — server token            | implemented   | POST `/api/v1/provision`                                                                |
| Provisioning — device token            | implemented   | same endpoint + `credentials.accessToken`                                               |
| OTA over HTTPS (streaming GET)         | implemented   | sibling [`thingsboard_http_ota`](../thingsboard_http_ota/)                              |
| Auth — access token                    | implemented   | path/query `/api/v1/$TOKEN/...`                                                         |
| Long-poll timeout semantics            | implemented   | `timeout=<poll_timeout>` on `/rpc` and `/attributes/updates`                            |
| `getSessionLimits`                     | not supported | not exposed in TB's HTTP API                                                            |
| Provisioning — `MQTT_BASIC`, `X509`    | not supported | not exposed in TB's HTTP API                                                            |
| Auth — X.509 mTLS, `MQTT_BASIC`        | not supported | MQTT-only TB features                                                                   |
