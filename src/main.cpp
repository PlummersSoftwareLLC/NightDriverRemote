// NightDriverRemote - Demo of ESPNOW commands being sent to a NightDriverStrip
// using an ESP32-based remote control with an OLED display and a button.
// Steps through the 7 available effects on the target device.

#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <array>
#include "Bounce2.h"
#include "heltec.h"  // Heltec library for OLED support

namespace 
{
    // Effect names that correspond to patterns available on the target NightDriverStrip.
    // These must be kept in sync with the target device's effect list, as indices are used
    // to trigger specific effects.

    constexpr std::array<const char*, 7> EFFECT_NAMES = 
    {
        "Solid White",
        "Solid Red",
        "Solid Amber",
        "Fire Effect",
        "Rainbow Fill",
        "Color Meteors",
        "Off"
    };

    // Broadcast MAC allows control of all NightDriverStrip instances in range.
    // This allows front and back license plate NightDriverStrips to be controlled
    // at the same time.
    // For selective control, replace with the specific target device's MAC.
    // Format: {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}

    constexpr std::array<uint8_t, 6> RECEIVER_MAC = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Command set for ESPNOW protocol. Values must match the receiver's expectations.
    // Starting at 1 allows detection of uninitialized/corrupted commands.
    // INVALID provides error detection in network protocol.

    enum class ESPNowCommand : uint8_t 
    {
        NextEffect = 1,
        PrevEffect,
        SetEffect,
        SetBrightness,
        INVALID = 255
    };

    // Network message format for ESPNOW communication.
    // Packed to ensure consistent wire format between different compilers/platforms.
    // Includes size field for protocol versioning and validation.

    class Message 
    {
    public:
        constexpr Message(ESPNowCommand cmd, uint32_t argument) : size(sizeof(Message)), command(cmd), arg1(argument) 
        {
        }

        // Provides raw byte access for network transmission while maintaining type safety
        const uint8_t* data() const 
        {
            return reinterpret_cast<const uint8_t*>(this);
        }

        constexpr size_t byte_size() const 
        {
            return sizeof(Message);
        }

    private:
        uint8_t       size;       // Protocol versioning and message validation
        ESPNowCommand command;    // Operation to perform
        uint32_t      arg1;       // Command-specific parameter (e.g., effect index)
    } __attribute__((packed));    // Packed on both ends (send and receive) so they agree on the size

    // Main controller class implementing the remote functionality.

    class NightDriverRemote 
    {
      public:
        NightDriverRemote() = default;
        ~NightDriverRemote() = default;

        // Prevent copying and moving as this class manages hardware resources
        NightDriverRemote(const NightDriverRemote&) = delete;
        NightDriverRemote& operator=(const NightDriverRemote&) = delete;
        NightDriverRemote(NightDriverRemote&&) = delete;
        NightDriverRemote& operator=(NightDriverRemote&&) = delete;

        // Initializes all hardware in the correct sequence.
        // Returns false if any stage fails, preventing partial initialization.
        bool initialize() 
        {
            return initializeDisplay() && initializeButton() && initializeWiFi() && initializeESPNow() && addPeer();
        }

        // Main update loop - polls button and sends commands when pressed.
        // Cycles through effects in reverse order due to physical button placement.
        void update() 
        {
            button.update();
            if (button.pressed()) 
            {
                currentEffect = (currentEffect + 1) % EFFECT_NAMES.size();
                setEffect(currentEffect);
                updateDisplay();  // Update display when effect changes
            }
        }

        // setBrightness
        //
        // Sends brightness change command to target device(s)

        bool setBrightness(uint8_t brightness)
        {
            Message msg{ESPNowCommand::SetBrightness, brightness};
            auto result = esp_now_send(RECEIVER_MAC.data(), 
                                    msg.data(), 
                                    msg.byte_size());

            if (result == ESP_OK) {
                Serial.print(F("Set brightness to: "));
                Serial.println(brightness);
                return true;
            }

            Serial.println(F("Error sending message"));
            return false;
        }

        // Sends effect change command to target device(s)
        // Returns false if effect index is invalid or transmission fails

        bool setEffect(uint32_t effect) 
        {
            if (effect >= EFFECT_NAMES.size()) 
            {
                return false;
            }

            Message msg{ESPNowCommand::SetEffect, effect};
            auto result = esp_now_send(RECEIVER_MAC.data(), 
                                    msg.data(), 
                                    msg.byte_size());

            if (result == ESP_OK) {
                Serial.print(F("Set effect to: "));
                Serial.println(EFFECT_NAMES[effect]);
                return true;
            }

            Serial.println(F("Error sending message"));
            return false;
        }

     private:
        // Initialize the OLED display
        bool initializeDisplay() 
        {
            Heltec.begin(true /*DisplayEnable Enable*/, false /*LoRa Disable*/, true /*Serial Enable*/);
            Heltec.display->setFont(ArialMT_Plain_16);  // Set a readable font size
            updateDisplay();  // Show initial display
            return true;
        }

        // Update the OLED display with current effect information

        void updateDisplay() 
        {
            // This code assumes the default screen size of 128x64 pixels

            assert(Heltec.display->width() == 128 && Heltec.display->height() == 64);

            Heltec.display->clear();
            
            // Display effect index
            Heltec.display->setFont(ArialMT_Plain_10);
            Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);

            char indexStr[30];
            snprintf(indexStr, sizeof(indexStr), "Effect: %d/%d", currentEffect + 1, EFFECT_NAMES.size());
            Heltec.display->drawString(64, 0, indexStr);
            
            // Display effect name
            Heltec.display->setFont(ArialMT_Plain_16);
            Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
            Heltec.display->drawString(64, 20, EFFECT_NAMES[currentEffect]);
            
            // Draw a progress bar
            int progressWidth = (currentEffect * 128) / (EFFECT_NAMES.size() - 1);
            Heltec.display->drawProgressBar(0, 50, 128, 10, (progressWidth * 100) / 128);
            
            Heltec.display->display();
        }

        // Configures button with internal pull-up and debouncing

        bool initializeButton() 
        {
            button.attach(0, INPUT_PULLUP);
            button.interval(1);
            button.setPressedState(LOW);
            return true;
        }

        // Sets up WiFi in station mode without connecting to any network

        bool initializeWiFi() 
        {
            WiFi.mode(WIFI_STA);
            return true;
        }

        // ESPNOW transmission status callback
        // Used for debugging and could be extended for retry logic
        static void onSendCallback(const uint8_t* macAddr, esp_now_send_status_t status) 
        {
            Serial.print(F("Send status: "));
            Serial.println(status == ESP_NOW_SEND_SUCCESS ? F("Success") : F("Fail"));
        }

        // Initializes ESPNOW protocol and registers callback

        bool initializeESPNow() 
        {
            if (esp_now_init() != ESP_OK) {
                Serial.println(F("Error initializing ESP-NOW"));
                return false;
            }

            esp_now_register_send_cb(onSendCallback);
            return true;
        }

        // Registers the target device(s) as ESPNOW peer(s)
        
        bool addPeer() 
        {
            esp_now_peer_info_t peerInfo = {};
            std::copy(RECEIVER_MAC.begin(), RECEIVER_MAC.end(), peerInfo.peer_addr);
            peerInfo.channel = 0;        // Auto channel selection
            peerInfo.encrypt = false;    // No encryption for broadcast support
            peerInfo.ifidx = WIFI_IF_STA;

            if (esp_now_add_peer(&peerInfo) != ESP_OK) 
            {
                Serial.println(F("Failed to add peer"));
                return false;
            }
            return true;
        }

        Bounce2::Button button;      // Hardware button with debouncing
        uint32_t currentEffect = 0;  // Current effect index in EFFECT_NAMES array
    };

} // anonymous namespace

NightDriverRemote remote;

void setup() 
{
    if (!remote.initialize()) 
    {
        Serial.println(F("Failed to initialize NightDriverRemote"));
    }
    remote.setEffect(0);  // Start with the first effect
}

void loop() 
{
    remote.update();
    delay(10);  // Cooperative multitasking delay
}