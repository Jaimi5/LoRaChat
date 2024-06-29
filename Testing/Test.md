# Start configuration

- ``` pip install paho-mqtt ```
- ``` pip install platformio ```


- The MQTT broker should be running.


# Usage

- ``` python main.py experiment_name ```

Write me a two folders named "Testing" and inside it a folder named "Monitoring" and 3 files named "status.json", "data.json"

How would you represent a file in a folder?
- Folder\file.ans

There is another graphic representation using md?
- [Folder\file.ans](Folder\file.ans)

## Test.md
- [Testing\Monitoring\status.json](Testing\Monitoring\status.json)
- [Testing\Monitoring\data.json](Testing\Monitoring\data.json)

## Monitoring
- [status.json](status.json)
- [data.json](data.json)

## Testing
- [Test.md](Test.md)

# End configuration

# MQTT
- The topic is ``` "test/monitoring" ```
- The payload is a JSON with the following structure:
```
{
    "device": "DeviceName",
    "timestamp": "2021-03-15T13:00:00Z",
    "data": {
        "temperature": 0,
        "humidity": 0,
        "voltage": 0,
        "current": 0
    }
}
```

# Example
```
{
    "device": "DeviceName",
    "timestamp": "2021-03-15T13:00:00Z",
    "data": {
        "temperature": 20,
        "humidity": 50,
        "voltage": 12,
        "current": 1
    }
}
```