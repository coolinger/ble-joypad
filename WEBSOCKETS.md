## Protocol

### Message Format

All messages are JSON objects with this structure:

```json
{
  "type": "MESSAGE_TYPE",
  "id": 123,
  "data": { ... },
  "timestamp": "2025-11-18T14:30:00.123456Z"
}
```

**Fields**:
- `type`: Message type (JOURNAL, STATUS, CARGO, NAVROUTE, SUMMARY, etc.)
- `id`: Packet/message ID (sequential, 0 for initial summary)
- `data`: Event data (varies by type)
- `timestamp`: ISO 8601 timestamp when message was created

## Server-to-Client Messages

### Automatic Messages

#### Initial Summary

When a client connects, the server automatically sends the current game state:

```json
{
  "type": "SUMMARY",
  "id": 0,
  "data": {
    "location": {
      "system": "Col 285 Sector XV-C c13-20",
      "station": null
    },
    "fuel": {
      "percent": 14.6,
      "current": 9.53,
      "capacity": 65.07
    },
    "cargo": {
      "count": 5.0,
      "capacity": 64,
      "drones": 5
    },
    "hull": 100.0,
    "shields": 100.0,
    "balance": 2664399295,
    "legal_state": "Clean",
    "route": 25,
    "destination": {
      "System": 5581410702074,
      "Body": 70,
      "Name": "Emilys Exchange"
    },
    "status": {
      "docked": false,
      "landed": false,
      "supercruise": true
    }
  },
  "timestamp": "2025-11-18T14:30:00.123456Z"
}
```

#### Real-time Events

As game events occur, they are pushed to all connected clients:

**JOURNAL Event**:
```json
{
  "id": 45,
  "type": "JOURNAL",
  "data": {
    "timestamp": "2025-11-18T14:30:05Z",
    "event": "Docked",
    "StationName": "Jameson Memorial",
    "StarSystem": "Shinrarta Dezhra"
  },
  "timestamp": "2025-11-18T14:30:05.789012Z"
}
```

**STATUS Event**:
```json
{
  "id": 46,
  "type": "STATUS",
  "data": {
    "timestamp": "2025-11-18T14:30:10Z",
    "event": "Status",
    "Flags": 16777217,
    "Fuel": {
      "FuelMain": 8.5,
      "FuelReservoir": 0.68
    },
    "Cargo": 5.0
  },
  "timestamp": "2025-11-18T14:30:10.456789Z"
}
```

## Client-to-Server Commands

### Command Format

Clients can send JSON commands to the server:

```json
{
  "command": "COMMAND_NAME",
  "parameter": "value"
}
```

### Available Commands

#### SUMMARY - Request Current Summary

Request the current game state summary.

**Request**:
```json
{
  "command": "SUMMARY"
}
```

**Response**: Same format as initial summary (see above)

#### HISTORY - Request Recent Events

Request the last N events from the history buffer.

**Request**:
```json
{
  "command": "HISTORY",
  "count": 10
}
```

**Parameters**:
- `count` (optional): Number of events to return (default: 10, max: 100)

**Response**:
```json
{
  "type": "HISTORY",
  "data": [
    {
      "id": 35,
      "type": "STATUS",
      "data": { ... },
      "timestamp": "2025-11-18T14:29:50.123456Z"
    },
    {
      "id": 36,
      "type": "JOURNAL",
      "data": { ... },
      "timestamp": "2025-11-18T14:29:55.789012Z"
    }
  ],
  "timestamp": "2025-11-18T14:30:00.123456Z"
}
```

#### SINCE - Request Events Since ID

Request all events with ID greater than the specified value.

**Request**:
```json
{
  "command": "SINCE",
  "id": 100
}
```

**Parameters**:
- `id` (required): Request all events with ID > this value

**Response**:
```json
{
  "type": "SINCE",
  "data": [
    {
      "id": 101,
      "type": "STATUS",
      "data": { ... },
      "timestamp": "2025-11-18T14:30:00.123456Z"
    },
    {
      "id": 102,
      "type": "JOURNAL",
      "data": { ... },
      "timestamp": "2025-11-18T14:30:05.789012Z"
    }
  ],
  "timestamp": "2025-11-18T14:30:10.456789Z"
}
```

### Error Responses

If a command fails, the server responds with an error message:

```json
{
  "error": "Error description"
}
```

**Example**:
```json
{
  "error": "Invalid JSON"
}
```
