# Proposed, not built

Designs that belong to a component of the firmware and are not part of it yet. `PLAN.MD` "Not in this plan" lists them as later work.

## sys_data_provider: more drivers

- **MQTT link** (3, kept in the config): a driver (`sys_mqtt`) whose provider is a topic; its channel the topic's FNV-1a hash (32 bits), which the app computes the same way; two topics on one hash refused at add. The client (broker, login) is the driver's config, made by its own command; a topic's QoS and retain are set when the topic is made. The driver works on the `esp-mqtt` client's own task, so a topic costs no task:
  - `open` subscribes, `close` unsubscribes; the driver keeps its topics and subscribes them again after a reconnect.
  - `send` is `esp_mqtt_client_enqueue`: into the client's outbox, never waiting for the broker.
  - `MQTT_EVENT_DATA` (the client's task, the driver's RX task) comes in pieces for a long message: the driver joins them, up to `CMD_FRAME_MAX`, then `deliver`.
  - Broker lost: `set_max_frame(0)`, so nothing is sent into a growing outbox; connected again: the frame limit back.
- **UART create command**: opening a port as a provider (port, baud, pins, flow control).
