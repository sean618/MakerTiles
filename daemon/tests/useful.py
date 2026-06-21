
import random

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

