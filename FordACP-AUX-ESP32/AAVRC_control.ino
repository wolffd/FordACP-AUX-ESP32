/*
 ******************************************************************
 * Revisions:
 *
 * 2017 Original version Anson Liu (ansonliu.com)
 * 2026-02-22 adapted to A2DP: replaced iOS inline with A2DP bluetooth for ESP32 (Arduino Target:ESP32-WROOM-DA) using ESPSoftwareSerial - Daniel Wolff (info@juppiemusic.com), github.com/wolffd
 *
 */

enum AVRCControlCommand
{
  noCommand,
  cancelCommand,
  playPause,
  nextTrack,
  prevTrack,
  fastForwardTrack,
  rewindTrack,
  activateSiri
};

AVRCControlCommand lastCommand = noCommand;

boolean playingState = false;
boolean rewindState = false;
boolean ffState = false;

/*
 * Simulate inline control based on value of lastCommand.
 */
void avrc_control_handler()
{
  if (lastCommand != noCommand)
  {
    switch (lastCommand)
    {
    case playPause:
      if (!playingState)
      {
        a2dp_sink.play();
      }
      else
      {
        a2dp_sink.pause();
      }
      playingState = !playingState;
      break;
    case nextTrack:
      a2dp_sink.next();
      break;
    case prevTrack:
      a2dp_sink.previous();
      break;
    case fastForwardTrack:
      a2dp_sink.fast_forward();
      break;
    case rewindTrack:
      a2dp_sink.rewind();
      break;
    case activateSiri:

      break;
    }
    lastCommand = noCommand;
  }
}

void avrc_shutdown()
{
  a2dp_sink.end();
}

void avrc_resume()
{
  a2dp_sink.start(BLUETOOTH_SINK_NAME);
}

/*
 * Playback inline control setup
 */
void avrc_control_setup()
{
}