# `thingsboard_mqtt`

MQTT transport for the [`thingsboard`](../thingsboard/) ESPHome custom component.
Implements ThingsBoard's MQTT device API as documented at
<https://thingsboard.io/docs/reference/mqtt-api/>.

End firmware uses **either** `thingsboard_mqtt:` **or** `thingsboard_http:` — never
both. The core component enforces this via its `FINAL_VALIDATE_SCHEMA`.

## YAML

```yaml
thingsboard:
  server_url: !secret thingsboard_server_url
  device_name: ${name}

thingsboard_mqtt:
  thingsboard_id: thingsboard_component
  broker: !secret thingsboard_mqtt_broker
  port: 1883
  device_token: !secret tb_device_token # optional if `provisioning:` set
  provisioning:
    key: !secret tb_provisioning_key
    secret: !secret tb_provisioning_secret
    # credentials variants — see TB device-provisioning docs
```

## TB device API coverage

| Operation                         | Status        | Notes                                          |
| --------------------------------- | ------------- | ---------------------------------------------- |
| Telemetry upload (server-ts)      | live          | pub `v1/devices/me/telemetry`                  |
| Telemetry upload (client-ts)      | live          | same topic, `{ts, values}` shape               |
| Client-attribute upload           | live          | pub `v1/devices/me/attributes`                 |
| Attribute request                 | live          | request_id round-trip                          |
| Shared-attribute push             | live          | sub `v1/devices/me/attributes`                 |
| Server-side RPC (TB→device)       | live          | request/response topics                        |
| Client-side RPC (device→TB)       | live          | `send_rpc_request` action                      |
| `getSessionLimits`                | live          | called automatically post-connect              |
| Device claim                      | live          | `claim_device` action                          |
| Provisioning — server token       | live          |                                                |
| Provisioning — device token       | payload wired | `credentialsType:"ACCESS_TOKEN"` + `token`     |
| Provisioning — `MQTT_BASIC`       | payload wired | `clientId`/`username`/`password`               |
| Provisioning — `X509_CERTIFICATE` | payload wired | `hash` (cert PEM)                              |
| OTA over MQTT (`v2/fw/*`)         | live          | chunked binary, see `../thingsboard_mqtt_ota/` |
| SOTA (software updates)           | unsupported   |                                                |
| Gateway protocol                  | out-of-scope  | device firmware, not a gateway                 |
| Auth — access token               | live          | `username = $TOKEN` in CONNECT                 |
| Auth — X.509 mTLS                 | live          | `set_client_certificate` + `set_server_ca`     |
| Auth — `MQTT_BASIC`               | live          | `set_basic_credentials`                        |

## Credential rotation

`ThingsBoardMQTT::connect()` re-pushes the esp-mqtt client config via
`esp_mqtt_set_config()` whenever the configured device token differs from the
last value pushed into the live client. This covers provisioning →
device-token transitions and admin-driven token rotations. Without this the
existing client would simply reconnect with stale credentials and the broker
would reject the CONNECT with an auth error.
