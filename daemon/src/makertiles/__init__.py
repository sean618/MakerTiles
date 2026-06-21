'''
Copyright © 2024 - 2025 Sean Bremner. All rights reserved.>>
'''

import logging
import random

from . import board_manager
from .ui import UIBoard

def start(port_name=None, conn=None, max_requests=250):
    if conn == None:
        from . import connection
        conn = connection.Connection()
    # Start logger
    logfilename = 'makertiles'
    if port_name is not None:
        logfilename += '_' + port_name
    logfilename += '.log'
    logger = logging.getLogger(__name__)
    logger.setLevel(logging.INFO)
    # Use an explicit FileHandler so this works even when the root logger already
    # has handlers (e.g. when run under pytest, which configures logging first and
    # makes basicConfig a no-op).
    fh = logging.FileHandler(logfilename, mode="w")
    fh.setFormatter(logging.Formatter("%(asctime)s - %(thread)d - %(levelname)s - %(message)s"))
    logger.addHandler(fh)
    conn.set_logger(logger)
    port_name = conn.open_port(port_name)
    logger.info("Opened port: %s", port_name)
    bm = board_manager.BoardManager(logger, conn, max_requests)
    bm.start()
    return bm.ui_tiles, bm

# def signal_handler(sig, frame):
#     print("Terminating...")
#     manager.stop()
#     sys.exit(0)
# signal.signal(signal.SIGINT, signal_handler)  # Catch Ctrl+C



black = 0
grey = 0x808080
white = 0xFFFFFF
red = 0xFF0000
orange = 0xFF8C00
yellow = 0xFFFF00
green = 0x00FF00
cyan = 0x00FFFF
blue = 0x0000FF
magenta = 0xFF00FF
pink = 0xFF007F
purple = 0x800080

def hsv_colour(hue, sat, val):
    import colorsys
    hue = hue % 1
    sat = max(0, min(sat, 1.0))
    val = max(0, min(val, 1.0))
    (r, g, b) = colorsys.hsv_to_rgb(hue, sat, val)
    return (int(r * 255) << 16) + (int(g * 255) << 8) + int(b * 255)


def random_colour():
    return hsv_colour(random.random(), 1, 1)