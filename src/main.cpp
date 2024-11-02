// NightDriverRemote
//
// An ESPNOW remote control for the NightDriver LED controller.  Sends ESPNOW messages to the NightDriverStrip
// instance identified by the receiverMAC address.  The remote has a single button that cycles through the
// available effects on the NightDriverStrip.  Currently, the effects are defined in a table that is assumed
// to match the PLATECOVER project in the NightDriverStrip repository.

#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <array>
#include "Bounce2.h"                            // For Bounce button class

using namespace std;

// Define the button - connected to GPIO0 on the Heltec

Bounce2::Button Button1;

// Define the effect names - these must match the effect names in the NightDriverStrip project

static const array<string, 7> effectNames = 
{
    "Solid White",
    "Solid Red",
    "Solid Amber",
    "Fire Effect",
    "Rainbow Fill",
    "Color Meteors",
    "Off"
};

// Define the MAC address of the receiver - We'll use the broadcast ID, but could be any MAC
// address on a NightDriverStrip that is listening for ESPNOW messages.

uint8_t receiverMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // Replace with the receiver's MAC address

// The commands that can be sent to the NightDriverStrip

typedef enum
{
    ESPNOW_NEXTEFFECT,
    ESPNOW_PREVEFFECT,
    ESPNOW_SETEFFECT
} ESPNOW_COMMAND;

// The message structure; the message is a single byte command followed by a 32-bit argument and
// this structure must match the one in network.cpp of the NightDriverStrip project it is controlling.

typedef struct Message 
{
    byte            cbSize;
    ESPNOW_COMMAND  command;
    uint32_t        arg1;
} Message;

Message message;

// Report the success or failure of the message send

void onSend(const uint8_t *macAddr, esp_now_send_status_t status) 
{
    Serial.print("Message send status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

// Set up the ESP32 and the button and add the ESPNOW peer

void setup() 
{
    Serial.begin(115200);

    Button1.attach(0, INPUT_PULLUP);
    Button1.interval(1);
    Button1.setPressedState(LOW);

    // Initialize WiFi in station mode (does not connect to any network)
    WiFi.mode(WIFI_STA);

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    // Register send callback function
    esp_now_register_send_cb(onSend);

    // Configure peer information
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverMAC, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;  // Set interface to station mode

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }
}

// Set the effect on the target NightDriverStrip instance via EESPNOW

bool SetEffect(uint32_t effect)
{
    if (effect >= effectNames.size())
    {
        return false;
    }

    message.cbSize = sizeof(message);
    message.command = ESPNOW_SETEFFECT;
    message.arg1    = effect;

    esp_err_t result = esp_now_send(receiverMAC, (uint8_t *) &message, sizeof(message));
    
    if (result == ESP_OK) {
        Serial.println("Message sent successfully");
    } else {
        Serial.println("Error sending message");
    }

    Serial.print("Setting effect to: ");
    Serial.println(effectNames[effect].c_str());

    return true;
}

// Main loop - cycle through the effects on the NightDriverStrip as the button is pressed

void loop() 
{
    static uint32_t currentEffect = 0;
    SetEffect(currentEffect);

    do
    {
        Button1.update();
        if (Button1.pressed())
        {
            currentEffect++;
            if (currentEffect >= effectNames.size())
                currentEffect = 0;
            SetEffect(currentEffect);
        }
        delay(10);
    } while (true);
}