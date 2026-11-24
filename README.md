# MAHI AI

An open-source ESP32 voice-assistant firmware project maintained by **CSM Mohasin Alam**. MAHI AI brings voice interaction, configurable wake words, device controls, and display-based feedback to supported ESP32 boards.

- **Developer:** CSM Mohasin Alam
- **Website:** [cam.mohasin.bro.bd](https://csm.mohasin.bro.bd)
- **Contact:** [admin@mohasin.bro.bd](mailto:admin@mohasin.bro.bd)

> The website and contact details identify the MAHI AI project. Configure a device with the service endpoint and credentials provided for your deployment; this README does not imply that a particular cloud backend is already configured or available.

## Features

- Network connectivity through Wi-Fi, wired Ethernet, USB RNDIS, and supported 4G modem boards
- Offline wake-word detection on supported hardware, with configurable wake-word models
- WebSocket and MQTT + UDP communication options
- Opus audio streaming and speech-processing options supported by the selected board
- Display support for OLED and LCD boards, including expressions and status information
- Camera input on supported boards
- Battery status and power-management features on supported hardware
- Wi-Fi provisioning through hotspot or BluFi, where supported
- Device-side MCP capabilities for supported device controls
- A broad range of ESP32-family targets and community hardware configurations

Available capabilities depend on the selected board, its peripherals, firmware configuration, and the MAHI AI service deployment.

## Supported hardware

The repository contains board configurations under [`main/boards`](main/boards). Consult each board's README and `config.json` for its hardware requirements and supported build variants. Board images and wiring examples, where present, are maintained in [`docs`](docs).

## Build and flash

This project requires ESP-IDF **v6.0.1 or later**. ESP-IDF `release/v6.1` is the recommended branch for the current project configuration.

From the project root, activate your ESP-IDF environment, then select and configure the target board:

```bash
idf.py set-target esp32s3
idf.py menuconfig
```

Choose the appropriate board and options in the **Xiaozhi Assistant** configuration menu currently used by the firmware, then build and flash:

```bash
idf.py build
idf.py flash monitor
```

The menu label is an existing firmware configuration label; it does not identify the MAHI AI service. Follow the selected board's README for its exact target, partition table, peripheral settings, and flashing instructions. Some boards also support the repository build helper documented in their board README.

## Documentation

- [Custom board guide](docs/custom-board.md)
- [OTA configuration and firmware updates](docs/ota.md)
- [MCP usage](docs/mcp-usage.md)
- [MCP protocol](docs/mcp-protocol.md)
- [MQTT + UDP protocol](docs/mqtt-udp.md)
- [WebSocket protocol](docs/websocket.md)
- [Code style](docs/code_style.md)

## Project and licensing

MAHI AI is maintained by **CSM Mohasin Alam**. This repository incorporates open-source software and hardware support; see [`LICENSE`](LICENSE) and the relevant source files for license and attribution details. Please retain applicable third-party notices when redistributing or modifying the project.

For questions or project inquiries, email [admin@mohasin.bro.bd](mailto:admin@mohasin.bro.bd) or visit [cam.mohasin.bro.bd](https://csm.mohasin.bro.bd).
