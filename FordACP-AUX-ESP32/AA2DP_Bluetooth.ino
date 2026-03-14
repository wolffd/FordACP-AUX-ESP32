/*
 ******************************************************************
 * Revisions:
 *
 * 2026-02-22 Original implementation for A2DP bluetooth with ESP32 (Arduino Target:ESP32-WROOM-DA) using ESPSoftwareSerial - Daniel Wolff (info@juppiemusic.com), github.com/wolffd.
 *
 */

#include "AudioTools.h"
#include "BluetoothA2DPSink.h"

#define INTERNAL_DAC false
#define USE_ESP_OLD_I2S false
#define BLUETOOTH_SINK_NAME "ZetecRules"

#if !INTERNAL_DAC
#if USE_ESP_OLD_I2S == true
// ESP i2s
#include "ESP_I2S.h"
I2SClass i2s;
#else
I2SStream i2s;
#endif
#endif

// internal adc
#if INTERNAL_DAC
AnalogAudioStream out;
BluetoothA2DPSink a2dp_sink(out);
#else
BluetoothA2DPSink a2dp_sink(i2s);
#endif

// i2s
const uint8_t I2S_BLCK = 4;  // Audio data bit clock
const uint8_t I2S_WS = 15;   // Audio data left and right clock
const uint8_t I2S_SDOUT = 2; // ESP32 audio data output (to speakers)

// callback when bluetooth delivers meta-data (e.g. switching track)
void avrc_metadata_callback(uint8_t data1, const uint8_t *data2)
{
  if (DEBUG_SERIAL)
  {
    Serial.printf("AVRC metadata rsp: attribute id 0x%x, %s\n", data1, data2);
  }
  wPlayTime = atoi((const char *)data2) / 1000;
  acp_displaytime();
}

void a2dp_bluetooth_setup()
{

// ESP i2s
#if !INTERNAL_DAC
#if USE_ESP_OLD_I2S
  i2s.setPins(I2S_BLCK, I2S_WS, I2S_SDOUT);
  if (!i2s.begin(I2S_MODE_STD, 44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH))
  {
    Serial.println("Failed to initialize I2S!");
    while (1)
      ; // do nothing
  }
#else
  // I2SS i2s ?
  auto cfg = i2s.defaultConfig();
  // -- TODO: try for performance / stability improvements
  // I2S buffer length at most 1024 and more than 8
  cfg.buffer_size = 128; // default: 64
  cfg.buffer_count = 12; // default: 8

  // --
  cfg.pin_bck = I2S_BLCK;
  cfg.pin_ws = I2S_WS;
  cfg.pin_data = I2S_SDOUT;
  i2s.begin(cfg);

  // i2s.begin(); // standard ports
#endif
#endif

  // get track information
  a2dp_sink.set_avrc_metadata_callback(avrc_metadata_callback);
  a2dp_sink.set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_PLAYING_TIME);


  // reconnect after connection drop from the source (not restart of this device)
  a2dp_sink.set_auto_reconnect(true, 10); // default is 1000

  a2dp_sink.start(BLUETOOTH_SINK_NAME);
}
