# Fixed Issue: Incorrect Firmware Module Count

## Issue Found

The documentation incorrectly stated that HardwareOS has **11 modules** when it actually has **12 modules**. The 12th module (M12) is the IAQ module (`HARDWAREOS_MODULE_IAQ.md`), which was added later but the module count was never updated in several places.

### Affected Files

1. **README.md** - Three occurrences:
   - Line 98: `# Device firmware specs (11 modules)` in project structure
   - Line 136: `HardwareOS is an 11-module firmware stack`
   - Line 178: `Firmware (11 modules) + Cloud (4 services)` in development status

2. **CLAUDE.md** (symlinked to AGENTS.md) - One occurrence:
   - Line 43: `# Device firmware specs (11 modules)` in repository structure

### Evidence

The actual module files in `docs/firmware/` confirm 12 modules exist:
- M01: HARDWAREOS_MODULE_RADAR_INGEST.md
- M02: HARDWAREOS_MODULE_TRACKING.md
- M03: HARDWAREOS_MODULE_ZONE_ENGINE.md
- M04: HARDWAREOS_MODULE_PRESENCE_SMOOTHING.md
- M05: HARDWAREOS_MODULE_NATIVE_API.md
- M06: HARDWAREOS_MODULE_CONFIG_STORE.md
- M07: HARDWAREOS_MODULE_OTA.md
- M08: HARDWAREOS_MODULE_TIMEBASE.md
- M09: HARDWAREOS_MODULE_LOGGING.md
- M10: HARDWAREOS_MODULE_SECURITY.md
- M11: HARDWAREOS_MODULE_ZONE_EDITOR.md
- M12: HARDWAREOS_MODULE_IAQ.md

CLAUDE.md line 92 and the module table (lines 94-107) correctly reference 12 modules, creating an internal inconsistency within the same file.

## How It Was Fixed

Changed all occurrences of "11 modules" to "12 modules" in:
- README.md (3 occurrences)
- CLAUDE.md (1 occurrence)

## Commands/Tests Run

- `ls docs/firmware/HARDWAREOS_MODULE_*.md` - Verified 12 module files exist
- No automated tests exist for documentation consistency
