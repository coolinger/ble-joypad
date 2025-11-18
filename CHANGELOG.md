# BLE Joypad - Changelog

## Version 2.0.0 - Major Update (November 2025)

### New Features

#### Elite Dangerous Log Stream Integration
- **UDP Listener**: Receives Elite Dangerous game events on port 12345
- **Event Types Supported**:
  - `JOURNAL` - Game journal events
  - `STATUS` - Real-time ship status
  - `CARGO` - Cargo hold inventory
  - `NAVROUTE` - Navigation route information

#### Dual Page LVGL Interface
- **Page 1: Fighter Control** (Original functionality)
  - Button matrix for Elite Dangerous fighter commands
  - Swipe left to switch to log viewer
  
- **Page 2: Log Viewer** (New!)
  - Real-time event log display (last 5 events)
  - Swipe right to return to fighter control

#### Log Viewer Features

**Header Bar (Top)**:
- Jump count from navigation route
- Fuel bar (orange) showing current fuel percentage
- Hull bar (green) showing hull integrity

**Event Log Area (Center)**:
- Scrolling display of recent Elite Dangerous events
- Special handling for ProspectedAsteroid events
- **Motherlode Detection**: Screen blinks 3 times when a motherlode material is detected

**Cargo Bar (Bottom - 40px)**:
- Visual bar showing cargo capacity utilization
- Displays total capacity, used space, and drone count
- Distinguishes between regular cargo and drones

### Technical Changes

#### Memory Optimizations
- Reduced LVGL buffer from 1/20 to 1/50 screen size
- Switched from String to fixed-size char arrays
- Reduced event log from 10 to 5 entries
- Optimized UDP buffer size (800 bytes)
- Event log entries limited to 60 characters

#### Dependencies Added
- ArduinoJson 7.2.0 for JSON parsing

#### Event Processing
- **ProspectedAsteroid**: 
  - Detects motherlode materials
  - Triggers 3-blink screen flash
  - Logs material name (localized)
  
- **LoadGame/Loadout**: 
  - Captures fuel capacity
  - Captures cargo capacity
  
- **STATUS**: 
  - Updates fuel level
  - Updates cargo count
  
- **CARGO**: 
  - Parses inventory
  - Separates drones from regular cargo
  
- **NAVROUTE**: 
  - Tracks remaining jumps

### UI/UX Improvements
- Touch gesture support for page switching
- Swipe left/right to navigate between pages
- Display remains on during BLE connection
- Real-time status updates in log viewer

### Build Information
- RAM Usage: 37.9% (124,344 bytes)
- Flash Usage: 69.3% (1,362,997 bytes)
- Platform: ESP32 (Arduino framework 2.0.14)

### Notes
- WiFi must be configured for UDP reception
- Default UDP port: 12345
- Compatible with Elite Dangerous log streamer applications
