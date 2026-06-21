'''
Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
'''

# from daemon import *
import threading
import time
import os
import pytest
import time

import sys
import makertiles
from makertiles import clib_connection

# Get the path to the shared library

tiles = None

def get_mock_connection():
    use_mock_microbus = True
    # The unified build passes the exact path to the freshly built shared
    # library via this env var. Fall back to the legacy relative path so the
    # test still runs when invoked by hand.
    master_node_system_filepath = os.environ.get('MASTER_NODE_SYSTEM_LIB')
    if not master_node_system_filepath:
        if use_mock_microbus:
            master_node_system_filepath = '../protocol/fieldprotocol/build/'
        else:
            master_node_system_filepath = '../../tests/system/build/'
        # Load the shared library
        if sys.platform.startswith('win'):
            master_node_system_filepath += 'masterNodeSystem.dll'
        elif sys.platform.startswith('darwin'):
            master_node_system_filepath += 'libmasterNodeSystem.dylib'
        else:
            master_node_system_filepath += 'libmasterNodeSystem.so'
    return clib_connection.ClibConnection(master_node_system_filepath, None, use_mock_microbus)

use_mock_connection = True

@pytest.fixture(scope="session", autouse=True)
def global_setup():
    global tiles
    conn = None
    if use_mock_connection:
        conn = get_mock_connection()
    tiles, bm = makertiles.start(None, conn)
    bm.print_tiles()

def test_all_tiles():
    global tiles
    usb_rx_full = tiles.usb.usb_rx_full
    battery_level = tiles.battery.level
    button_pressed_counts = {}
    for i in range(5):
        button_pressed_counts[i] = tiles.button.pressed_counts[i]
    tiles.dcmotors.powers[0] = 50
    assert(tiles.dcmotors.powers[0] == 50)
    distance = tiles.distance_sensor.distance
    pitch = tiles.imu.pitch
    roll = tiles.imu.roll
    yaw = tiles.imu.yaw
    horizontal = tiles.joystick.horizontal
    vertical = tiles.joystick.vertical
    tiles.led_strip.colours[0] = 0xA1A2A3
    encoder_pos = tiles.rotary_encoder.position
    tiles.screen.text_x = 10
    assert(tiles.screen.text_x == 10)
    tiles.screen.text_y = 20
    tiles.screen.text_size = 2
    tiles.screen.text_colour = 0xFF
    tiles.screen.text_background_colour = 0x0
    tiles.screen.text_draw = True
    # tiles.start_pixel_streaming = 
    # tiles.pixel_data_stream (80 * 16 bit)
    # tiles.stop_pixel_streaming
    for board in range(2):
        for servo in range(2):
            tiles.servos[board].angles[servo] = 80
    temp = tiles.temperature_sensor[0].temperature


def test_basic_leds():
    global tiles
    tiles.led_strip.colours[0:2] = [0x010203, 0x040506]
    tiles.led_strip.colours[10:20] = [i for i in range(10)]
    val = tiles.led_strip.colours[0:2]
    val2 = tiles.led_strip.colours[10:20]
    assert(val == [0x010203, 0x040506])
    assert(val2 == [i for i in range(10)])

    tiles.led_strip.colours[0:350] = [i for i in range(350)]
    assert tiles.led_strip.colours[0] == 0x00

# def test_high_bandwidth_leds():
#     try:
#         for z in range(10000):
#             # tiles.led_strip.colours[0:64] = [i for i in range(64)]
#             tiles.led_strip.colours[0:350] = [i for i in range(350)]
#         orig_timeout = tiles.led_strip._get_timeout
#         tiles.led_strip._get_timeout = 100.0
#         assert tiles.led_strip.colours[0] == 0
#         tiles.led_strip._get_timeout = orig_timeout
#     except Exception as e:
#         tiles._logger.exception("An error occurred {e}")
#         raise

# def test_multiple_threads():
#     def set_leds(thread):
#         global tiles
#         for i in range(10):
#             tiles.led_strip.colours[20*i:20*i+10] = [i+1 for i in range(10)]

#     global tiles
#     threads = []
#     for i in range(100):
#         thread1 = threading.Thread(target=set_leds, args=(i, ))
#         thread1.start()
#         threads.append(thread1)
#     for thread in threads:
#         thread.join()
#     for i in range(10):
#         assert tiles.led_strip.colours[20*i:20*i+10] == [i+1 for i in range(10)]

if __name__ == "__main__":
    start = time.time()
    tiles, bm = makertiles.start(None, get_mock_connection())
    bm.print_tiles()
    test_all_tiles()
    test_basic_leds()
    # test_multiple_threads()
    # test_high_bandwidth_leds()
    print("Time:", time.time() - start)
