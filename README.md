# LoRaChat

A LoRa Chat long-distance communication using LoRa technology and ESP32 LoRa boards. You can connect to your devices via phone using a Bluetooth Serial Terminal.
We are using the [LoRaMesher](https://github.com/LoRaMesher/LoRaMesher) which allows us a LoRa Mesh communication protocol to build the routing tables and the contact lists. Large messages are possible using LargePayloads feature.

# Configuration 

1. Initialize the code with VSCode and PlatformIO
2. Update the code to an specific device. (Tested in ttgo-t-beam and ttgo-lora32-v1)
3. Install in your mobile device a "Bluetooth Terminal". Tested apps in the ICDCS 2022 QR image bellow.
4. Start the device and your mobile device.
5. With your mobile device, connect the bluetooth and connect to one of the device ID shown in the display OLED.
6. Open the "Bluetooth Terminal" and search for the connected device ID.
7. Write `/help` to show all the available commands.

# Available Commands
- `/help` - Show all the available commands.
- `/change name` - Will change the name of your device. If you don't change it, your default name will be the ID shown in the display. 
- `/search contacts` - It will start searching possible contacts and will add their name in the contacts list.
- `/print contacts` - Will show a list of possible contacts to chat.
- `/chat` - You could start talking to a contact of your contact list.
    - After you write this command, the device will ask you which contact you want to chat with.
    - Start chatting.
    - Write `/back` to finish the chat. 
- `/get location` - Will print the current location of your device. 


# ICDCS 2022 Demonstration

![ICDCS demonstration](images/ICDCS2022.png)
