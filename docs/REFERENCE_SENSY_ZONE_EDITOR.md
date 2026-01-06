# Reference: Sensy-One Zone Editor Architecture

This document analyzes the Sensy-One Zone Editor Home Assistant addon as a reference for building similar functionality for RS-1.

**Source**: https://github.com/sensy-one/home-assistant-addons

**License**: Proprietary (cannot be used with non-Sensy hardware)

---

## Overview

The Sensy-One Zone Editor is a Home Assistant addon that provides a visual interface for configuring polygon-based detection zones on LD2450-based presence sensors. It runs as a containerized web application inside Home Assistant.

---

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     Home Assistant Host                          │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │                  Zone Editor Addon Container               │ │
│  │                                                            │ │
│  │  ┌────────────────────────────────────────────────────┐   │ │
│  │  │                 Nginx (:8099)                       │   │ │
│  │  │              Reverse Proxy + Static Files           │   │ │
│  │  └───────────────────┬─────────────────────────────────┘   │ │
│  │                      │                                      │ │
│  │       ┌──────────────┴──────────────┐                      │ │
│  │       │                             │                       │ │
│  │       ▼                             ▼                       │ │
│  │  ┌──────────┐               ┌─────────────┐                │ │
│  │  │  /www/*  │               │   /api/*    │                │ │
│  │  │ (static) │               │   (Flask)   │                │ │
│  │  │          │               │             │                │ │
│  │  │ index.html│              │ backend.py  │                │ │
│  │  │ (178KB)  │               │  (12KB)     │                │ │
│  │  └──────────┘               └──────┬──────┘                │ │
│  │                                    │                        │ │
│  └────────────────────────────────────┼────────────────────────┘ │
│                                       │                          │
│                                       ▼                          │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │                    Home Assistant Core                      │ │
│  │                                                            │ │
│  │  ┌─────────────┐  ┌─────────────┐  ┌────────────────────┐ │ │
│  │  │ REST API    │  │ States      │  │ Services           │ │ │
│  │  │ /api/*      │  │             │  │                    │ │ │
│  │  │             │  │ zone_editor │  │ esphome.xyz_set_*  │ │ │
│  │  │             │  │ _floorplan. │  │                    │ │ │
│  │  │             │  │ <floor_id>  │  │                    │ │ │
│  │  └─────────────┘  └──────┬──────┘  └────────────────────┘ │ │
│  │                          │                                  │ │
│  └──────────────────────────┼──────────────────────────────────┘ │
│                             │                                    │
└─────────────────────────────┼────────────────────────────────────┘
                              │
                              ▼ (ESPHome native API or polling)
                    ┌───────────────────┐
                    │  Sensy-One Sensor │
                    │   (ESP32 + LD2450)│
                    │                   │
                    │ Reads zone config │
                    │ from HA state     │
                    └───────────────────┘
```

---

## Repository Structure

```
home-assistant-addons/
├── LICENSE                    # Proprietary license
├── README.md                  # Installation instructions
├── repository.yaml            # HA addon repository manifest
│
└── zone-editor/
    ├── config.yaml            # HA addon configuration
    ├── Dockerfile             # Container build instructions
    ├── docker-compose.yaml    # Standalone deployment option
    ├── .env                   # Environment variables template
    │
    ├── backend.py             # Flask API server (~400 lines)
    ├── nginx.conf             # Reverse proxy configuration
    │
    ├── www/
    │   └── index.html         # Bundled SPA (178KB)
    │
    ├── services/              # s6 service definitions
    │   ├── nginx/
    │   │   └── run            # Nginx startup script
    │   └── backend/
    │       └── run            # Python backend startup script
    │
    ├── icon.png               # Addon icon
    ├── logo.png               # Addon logo
    └── CHANGELOG.md           # Version history
```

---

## Component Deep Dive

### 1. Addon Configuration (`config.yaml`)

```yaml
name: "S1 Zone editor"
version: "v1.3.8"
slug: sensy_one_device_finder
description: "Add-on for creating and tuning detection zones"

# Supported CPU architectures
arch:
  - aarch64
  - amd64

# Lifecycle
init: false
startup: services
boot: auto

# Home Assistant integration
homeassistant_api: true    # Enables SUPERVISOR_TOKEN access
ingress: true              # Embeds in HA sidebar
ingress_port: 8099

# UI panel
panel_icon: mdi:vector-polygon
panel_title: "S1 Zone editor"
```

**Key Points:**
- `homeassistant_api: true` grants access to `SUPERVISOR_TOKEN` environment variable
- `ingress: true` enables iframe embedding in HA UI with automatic auth
- Supports both ARM64 (Raspberry Pi) and AMD64 (x86)

---

### 2. Container Build (`Dockerfile`)

```dockerfile
ARG BUILD_FROM="ghcr.io/hassio-addons/base:17.2.5"
FROM ${BUILD_FROM}

ENV LANG=C.UTF-8

# Install runtime dependencies
RUN apk add --no-cache \
    ca-certificates \
    nginx \
    python3 \
    py3-requests \
    py3-flask

# Copy application files
COPY backend.py /app/
COPY www/ /app/www/
COPY nginx.conf /etc/nginx/http.d/default.conf

# Copy s6 service definitions
COPY services/ /etc/services.d/
```

**Key Points:**
- Uses official Home Assistant addon base image (Alpine Linux)
- Minimal dependencies: nginx, python3, flask, requests
- Uses s6-overlay for process supervision (multiple services in one container)

---

### 3. Backend API (`backend.py`)

The Flask backend (~400 lines) provides these endpoints:

#### Core Endpoints

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/floors` | GET | List all floors configured in HA |
| `/api/floor_entities` | GET | Get entities for a specific floor |
| `/api/floorplan` | GET | Retrieve saved zone polygons |
| `/api/floorplan` | POST | Save zone polygon configuration |
| `/api/device_entities` | GET | Get entities for a device |
| `/api/services/<domain>/<service>` | POST | Proxy service calls to HA |
| `/api/template` | POST | Render Jinja2 templates via HA |
| `/api/historical_targets` | GET | Get historical target coordinates |

#### Home Assistant Connection

```python
import os
import requests

# Determine connection mode
if "SUPERVISOR_TOKEN" in os.environ:
    # Running inside Home Assistant as addon
    HA_URL = "http://supervisor/core/api"
    HA_TOKEN = os.environ["SUPERVISOR_TOKEN"]
else:
    # Running standalone (Docker)
    HA_URL = os.environ.get("HA_URL", "http://homeassistant:8123/api")
    HA_TOKEN = os.environ.get("HA_TOKEN", "")

# Standard headers for all HA API calls
HEADERS = {
    "Authorization": f"Bearer {HA_TOKEN}",
    "Content-Type": "application/json"
}

def call_ha_api(endpoint, method="GET", data=None):
    """Make authenticated request to Home Assistant API"""
    url = f"{HA_URL}/{endpoint}"
    if method == "GET":
        response = requests.get(url, headers=HEADERS)
    else:
        response = requests.post(url, headers=HEADERS, json=data)
    return response.json()
```

#### Zone Storage Strategy

Zones are stored in two places:

1. **Local JSON file** (backup/persistence):
```python
FLOORPLAN_FILE = "/data/floorplan.json"

def save_floorplan(floor_id, data):
    # Load existing data
    if os.path.exists(FLOORPLAN_FILE):
        with open(FLOORPLAN_FILE) as f:
            all_data = json.load(f)
    else:
        all_data = {}

    # Update floor data
    all_data[floor_id] = data

    # Save back
    with open(FLOORPLAN_FILE, "w") as f:
        json.dump(all_data, f)
```

2. **Home Assistant state entity** (for sensor access):
```python
def sync_to_ha_state(floor_id, data):
    """Create/update HA state entity with zone data"""
    entity_id = f"zone_editor_floorplan.{floor_id}"
    call_ha_api("states/" + entity_id, method="POST", data={
        "state": "configured",
        "attributes": {
            "zones": data["zones"],
            "sensors": data["sensors"],
            "updated_at": datetime.now().isoformat()
        }
    })
```

---

### 4. Frontend (`www/index.html`)

The frontend is a **single bundled HTML file** (178KB) containing:
- Inlined JavaScript (likely Vue.js or similar framework, compiled)
- Inlined CSS
- SVG assets

#### Features Implemented

1. **2D Floorplan Editor**
   - Upload floor plan image
   - Draw walls as line segments
   - Place sensors with FOV indicators
   - Draw polygon zones (up to 8 vertices)

2. **3D Visualization**
   - Three.js-based 3D view
   - Real-time target positions
   - Heatmap overlay

3. **Live Sensor Data**
   - WebSocket or polling connection to HA
   - Real-time target X/Y/speed display
   - Zone occupancy status

4. **Zone Configuration**
   - Polygon drawing tool
   - Zone naming
   - Inclusion/exclusion zone types
   - Save/load configurations

---

### 5. Nginx Configuration (`nginx.conf`)

```nginx
server {
    listen 8099;

    # Serve static frontend
    location / {
        root /app/www;
        index index.html;
        try_files $uri $uri/ /index.html;
    }

    # Proxy API requests to Flask backend
    location /api/ {
        proxy_pass http://127.0.0.1:5000/api/;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
}
```

---

### 6. Process Supervision (s6-overlay)

The addon runs multiple processes using s6-overlay:

```
/etc/services.d/
├── nginx/
│   └── run           # #!/command/execlineb
│                     # nginx -g "daemon off;"
│
└── backend/
    └── run           # #!/command/execlineb
                      # cd /app
                      # python3 backend.py
```

s6 ensures both processes start and restart if they crash.

---

## Data Flow

### Zone Configuration Flow

```
┌─────────┐     ┌─────────┐     ┌─────────┐     ┌─────────┐
│  User   │────▶│ Web UI  │────▶│  Flask  │────▶│  HA     │
│ draws   │     │ (JS)    │     │ backend │     │ State   │
│ polygon │     │         │     │         │     │ Entity  │
└─────────┘     └─────────┘     └─────────┘     └────┬────┘
                                                     │
                                                     ▼
                                              ┌─────────────┐
                                              │   Sensor    │
                                              │ (polls HA   │
                                              │  for zones) │
                                              └─────────────┘
```

### Live Tracking Flow

```
┌─────────┐     ┌─────────┐     ┌─────────┐     ┌─────────┐
│ Sensor  │────▶│  HA     │────▶│  Flask  │────▶│ Web UI  │
│ reports │     │ States  │     │ proxies │     │ renders │
│ X/Y/Z   │     │         │     │ data    │     │ targets │
└─────────┘     └─────────┘     └─────────┘     └─────────┘
```

---

## Key Implementation Insights

### 1. Zones Are Not Pushed to Sensors

The addon does **not** directly communicate with sensors. Instead:
- Zones are saved as HA state entities
- Sensors must poll HA or subscribe to state changes
- This decouples the editor from sensor firmware

### 2. Single-File Frontend

The 178KB `index.html` is a compiled SPA bundle. Benefits:
- No build step needed in container
- Single file to serve
- Works offline once loaded

Drawbacks:
- Hard to modify without source
- Large initial download

### 3. Dual Deployment Modes

The addon supports both:
- **HA Addon mode**: Uses Supervisor API, ingress auth
- **Standalone Docker**: Uses environment variables for HA connection

This is useful for development/testing outside HA.

### 4. Minimal Backend

The Flask backend is just a thin proxy layer:
- ~400 lines of Python
- No database (uses JSON file + HA state)
- No authentication (relies on HA ingress)
- No business logic (just API routing)

---

## Implications for RS-1

### If Building a Similar Web UI

```
rs1-zone-editor/
├── Dockerfile
├── config.yaml              # Copy structure from Sensy
├── backend.py               # Flask, ~200-400 lines
├── nginx.conf
└── www/
    └── index.html           # Could use simpler 2D-only UI
```

Estimated effort: 2-3 weeks for basic polygon editor

### If Building a TUI Instead

A terminal-based alternative would be simpler:

```
rs1-tui/
├── pyproject.toml
└── src/
    └── rs1_tui/
        ├── __init__.py
        ├── cli.py           # Entry point
        ├── ha_client.py     # HA REST API client
        ├── sensor.py        # Sensor data models
        ├── ui.py            # Textual/Rich TUI
        └── zones.py         # Zone configuration
```

**TUI Advantages:**
- No web bundling complexity
- Works over SSH
- Smaller codebase (~500-1000 lines)
- Hacker-friendly aesthetic
- Differentiates from Sensy

**TUI Libraries:**
- `textual` - Modern Python TUI framework
- `rich` - Beautiful terminal formatting
- `curses` - Standard library, low-level

Estimated effort: 1-2 weeks for basic TUI

### Hybrid Approach

Could offer both:
1. TUI for power users / SSH access
2. Simple web UI for casual users

Both share the same HA client code.

---

## Reference Links

- **Sensy-One Zone Editor**: https://github.com/sensy-one/home-assistant-addons
- **HA Addon Development**: https://developers.home-assistant.io/docs/add-ons
- **ESPHome LD2450**: https://esphome.io/components/sensor/ld2450/
- **Textual (Python TUI)**: https://textual.textualize.io/
- **Flask**: https://flask.palletsprojects.com/

---

## License Note

The Sensy-One Zone Editor is under a **proprietary license** that prohibits:
- Commercial use without permission
- Use with non-Sensy hardware

Any RS-1 zone editor must be built from scratch, not derived from Sensy code.
