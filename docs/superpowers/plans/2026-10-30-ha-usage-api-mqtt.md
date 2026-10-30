# HA Usage API + MQTT Implementation Plan

> **For agentic workers:** Implemented directly after design approval (approach 1).

**Goal:** Flat HTTP sensors + MQTT Discovery for Home Assistant.

**Architecture:** Shared `HaSensors` registry; `WebPortal` exposes `/api/ha/*`; `HaMqtt` publishes to external broker.

**Tech Stack:** Arduino-ESP32, ArduinoJson 7, PubSubClient 2.8

## Tasks
- [x] Design doc
- [x] HaSensors flat registry
- [x] HaMqtt client + Discovery
- [x] AppConfig NVS + Web UI
- [x] `/api/ha/*` endpoints
- [x] main.cpp publish on refresh
- [x] README + version bump
