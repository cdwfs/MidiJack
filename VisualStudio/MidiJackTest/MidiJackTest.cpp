#include <stdint.h>
#include <stdio.h>

#include <chrono>
#include <string>
#include <thread>

#define IMPORT_API extern "C" __declspec(dllimport)

// Counts the number of endpoints.
IMPORT_API int MidiJackCountEndpoints();
// Get the unique ID of an endpoint.
IMPORT_API uint32_t MidiJackGetEndpointIDAtIndex(int index);
// Get the name of an endpoint.
IMPORT_API const char* MidiJackGetEndpointName(uint32_t id);
// Retrieve and erase an MIDI message data from the message queue.
IMPORT_API uint64_t MidiJackDequeueIncomingData();

int main(int argc, char* argv[]) {
  // Must dequeue once to force the endpoints to refresh
  do
  {
    uint64_t msg = MidiJackDequeueIncomingData();
    if (msg == 0) {
      break; // no more data to dequeue
    }
  } while (true);

  int endpointCount = MidiJackCountEndpoints();
  printf("Detected %d endpoints:\n", endpointCount);
  for (int i = 0; i < endpointCount; ++i) {
    uint32_t id = MidiJackGetEndpointIDAtIndex(i);
    const std::string name = MidiJackGetEndpointName(id);
    printf("- %3d: 0x%016X %s\n", i, id, name.c_str());
  }

  printf("MIDI Message log:\n");
  do {
    // Process messages for this "frame"
    do
    {
      uint64_t msg = MidiJackDequeueIncomingData();
      if (msg == 0) {
        break; // no more data to dequeue
      }
      // process msg here
      uint32_t source = (uint32_t)((msg >>  0) & 0xFFFFFFFF);
      uint8_t status  =  (uint8_t)((msg >> 32) & 0xFF);
      uint8_t data1   =  (uint8_t)((msg >> 40) & 0xFF);
      uint8_t data2   =  (uint8_t)((msg >> 48) & 0xFF);
      const std::string name = MidiJackGetEndpointName(source);
      printf("0x%08X (%s): 0x%02X 0x%02X 0x%02X\n", source, name.c_str(), status, data1, data2);
    } while (true);

    // sleep until the next frame
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(16ms);
  } while (true);
}
