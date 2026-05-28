# Changelog

## v0.2.3

### Features

- TCP transport: **~1 MiB TX ring buffer** in PSRAM (when available) with background send task to absorb WiFi/TCP backpressure and reduce dropouts
- Kconfig `ESP_AUDIO_PCM_TCP_TX_BUF_SIZE` (default 1048576); falls back to 8 KiB internal RAM without PSRAM

## v0.2.2

### Features

- PC monitor: **Audacity-style M mute** on each waveform track while live playback is on (default all channels audible)
- Live playback and Peak/RMS meter follow unmuted channels only

### Documentation

- `tools/pcm_monitor/README.md`: usage guide for Serial/TCP/UDP modes, live playback + mute, Save WAV → Audacity workflow

## v0.2.0

### Features

- **TCP / UDP** PCM transport (device client → PC server)
- PC monitor: transport selector (Serial / TCP / UDP), network IP list, manual IP override
- Python bridge: TCP/UDP server, `/api/ifaces`, `/api/status`, `/api/version`, HTML cache busting
- `hi_nomi_record_play` main: WiFi STA + menuconfig-driven TCP/UDP
- TCP auto-reconnect on next `esp_audio_pcm_write()` after server restart

## v0.1.0

### Features

- Initial release of **esp_audio_pcm_toolkit**
- Device-side PCM transport abstraction (`esp_audio_pcm_*` API)
- USB Serial/JTAG and UART backends
- Remote control line parser: `vol`, `gain`, `gch`, `stream`
- PC-side **pcm_monitor** tool (waveform, live playback, WAV export, device control)
