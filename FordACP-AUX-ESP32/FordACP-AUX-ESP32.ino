/*
 * Ford Head Unit to Bluetoot Interface
 * Based on https://github.com/ansonl/FordACP-AUX
 *
 * The code in this file is intended to be uploaded to an ESP32 which is connected to an Ford head unit via ACP serial protocol.
 *
 * Compatible Ford Head Units: Any unit that uses ACP (Automotive CommunicationProtocol) and has a connector for a CD changer.
 *
 * Info on ACP:
 * http://www.mictronics.de/projects/cdc-protocols/#FordACP
 *
 * Some examples are: 4050RDS, 4500, 4600RDS, 5000RDS, 5000RDS EON, 6000CD RDS, 6000 MP3, 7000RDS
 *
 * These are all head units from the '96 - '07 era.
 *
 * The units used during testing are:
 * 5000 RDS EON (Ford 1998-2007)
 *
 * The code for the ACP protocol was originally written by Andrew Hammond for his Yampp and was ported over to the Arduino platform by Krzysztof Pintscher. Please see the ACP and CD headers for more info.
 * *
 **************************************************************************************
 * Revisions:
 *
 * 2014-11-30 version 1.0 - public release - Dale Thomas
 * 2017-05-07 version 1.1 - Removed Bluetooth functionality and edited ports for use with Arduino UNO. - AL
 * 2017-06-16 version 1.2 - Added iOS AUX inline control commands and timing to loop(). - Anson Liu (ansonliu.com)
 * 2017-09-09 version 1.3 - Added BLE vehicle unlock functionality when used with custom wiring. PCB design included in repository.
 * 2026-02-22 version 1.4 - Removed BLE and replaced iOS inline with A2DP bluetooth for ESP32 (Arduino Target:ESP32-WROOM-DA) using ESPSoftwareSerial - Daniel Wolff (info@juppiemusic.com), github.com/wolffd
 *
 */

#define DEBUG_LED false // LED debugging for careduino ESP32-C3 (will only do ACP, need to remove Bluetooth)
#define DEBUG_SERIAL false // serial debugging

// define core used for ACP ESPSoftwareSerial tasks
// assumes arduino event cores and arduino core set to 0
#define ACP_CORE 1

uint16_t wPlayTime = 0;
uint8_t currentTrack = 1;
boolean reset_timer = false;

/*
 * Entry point for program
 */

void setup()
{
  // acp_setup();

  // Read task run on core 1
  //  arduino configured for events on core 0
  xTaskCreatePinnedToCore(
      acp_setup_and_loop, // Function that should be called
      "ACP Updates",      // Name of the task (for debugging)
      4096,               // Stack size (bytes)
      NULL,               // Parameter to pass
      2,                  // Task priority
      NULL,               // Task handle
      ACP_CORE            // Core
  );

  a2dp_bluetooth_setup(); // Won't work on the ESP32-C3, which does not support bt classic
  // inline_control_setup();
}

/*
 * Program loop function
 */
void loop()
{
  // Handle ACP communication
  avrc_control_handler();
  delay(1); // otherwise the i2c makes this crash ?
}

// task on core 1
void acp_setup_and_loop(void *parameter)
{
  // If SDM is initialized here in the task then it both sends and receives
  acp_setup();
  for (;;)
  {
    acp_loop();
  }
}
