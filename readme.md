T-SIMCAM TinyML PoC
===================

This is a **proof-of-concept** project for running a TinyML model on the **T-SIMCAM** board using **Edge Impulse**.

![sample](./video/esp32_tinyml_detection.gif)

Project Goals
-------------

*   Test TinyML model inference on the T-SIMCAM board
*   Use the onboard camera and ESP32-S3 capabilities
*   Demonstrate hardware integration with Edge Impulse

Important Setup
---------------

**⚠️ Before uploading the code:**

*   Update your Wi-Fi credentials in `config.h`:

    #define WIFI_SSID     "your-wifi-name"
    #define WIFI_PASSWORD "your-wifi-password"

*   Make sure the pin numbers in `config.h` match your T-SIMCAM board version (V1.2 or V1.3).

Build & Flash
-------------

1.  Use Arduino IDE or PlatformIO
2.  Install the ESP32 board support and any required libraries
3.  Compile and upload the firmware to your T-SIMCAM board

Run
---

1.  **Power on the T-SIMCAM board**  
    The board will automatically connect to your configured Wi-Fi network.
2.  **Open the Serial Monitor**  
    Use the Serial Monitor in Arduino IDE, PlatformIO, or any serial terminal.  
    Set the **baud rate to `115200`**.
3.  **Watch the Serial Output**  
    The monitor will display logs, including a message like:
    
        Camera Ready! Use http://x.x.x.x to connect
    
    This is the local IP address of the camera server on your Wi-Fi network.
4.  **Access the Camera Interface**  
    Open the printed URL (e.g., `http://192.168.1.123`) in your web browser.
5.  **Start Object Detection**  
    The camera stream interface will appear in your browser. You can now test real-time TinyML inference using the onboard model.

Notes
-----

*   This is a simple PoC project—feel free to customize it with your own model and dataset.
*   You can extend it with SD card logging, IR filter support (for V1.3), or microphone input.
*   The model is already included in the `lib` folder. You can call the `BonWeiXiang-project-1_inferencing` module directly to perform object detection.

Warning
-------

⚠️ The model accuracy is very limited because it was trained on only **31 images**. This repo’s main purpose is to demonstrate **how to integrate TinyML with an ESP32** board, not to provide a production-quality model.