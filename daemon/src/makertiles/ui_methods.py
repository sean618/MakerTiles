import types

# Screen board helper methods, attached to a UIBoard by add_screen_methods.

def draw_image(self, x, y, width, height, image_path):
    from PIL import Image

    img = Image.open(image_path).convert("RGB")  # Ensure it's RGB format
    image_width, image_height = img.size
    if image_width != width or image_height != height:
        img = img.resize((width, height))

    import time
    start = time.time()

    self.pixel_window_x = x
    self.pixel_window_y = y
    self.pixel_window_width = width
    self.pixel_window_height = height
    self.start_pixel_streaming = True

    start2 = time.time()

    index = 0
    pixels = []
    for y in range(height):
        for x in range(width):
            r,g,b = img.getpixel((x, y))
            pixels.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
            index += 1

            if index == 80:
                self.pixel_data_stream[0:80] = pixels
                index = 0
                pixels = []

    if index > 0:
        self.pixel_data_stream[0:len(pixels)] = pixels
    self.stop_pixel_streaming = True

    self._logger.info("draw_image time taken: %s (%s after setup)", time.time() - start, time.time() - start2)


def draw_rectangle(self, x, y, width, height, colour):

    start_idx = None
    for idx, field in self._fields.items():
        if field.name == 'rectangle_x':
            start_idx = idx
            break

    self._logger.info("Draw rectangle starting")
    self._fields[start_idx + 0].data[0] = x
    self._fields[start_idx + 1].data[0] = y
    self._fields[start_idx + 2].data[0] = width
    self._fields[start_idx + 3].data[0] = height
    self._fields[start_idx + 4].data[0] = colour
    self._fields[start_idx + 5].data[0] = True
    self._board_manager.set_fields(self._board.id, start_idx, 6, self._set_timeout)
    self._logger.info("Draw rectangle finished")


def draw_text(self, x, y, text, size, colour, background_colour):
    start_idx = None
    for idx, field in self._fields.items():
        if field.name == 'text':
            start_idx = idx
            break
    
    self._logger.info("Draw text starting")
    chars = list(text) + ['\0']
    self._fields[start_idx].data[0:len(chars)] = chars
    self._fields[start_idx + 1].data[0] = x
    self._fields[start_idx + 2].data[0] = y
    self._fields[start_idx + 3].data[0] = size
    self._fields[start_idx + 4].data[0] = colour
    self._fields[start_idx + 5].data[0] = background_colour
    self._fields[start_idx + 6].data[0] = True
    self._board_manager.set_fields(self._board.id, start_idx, 64 + 6, self._set_timeout)
    self._logger.info("Draw text finished")


def add_screen_methods(screen):
    # These fields are exposed as convenience methods on the screen board
    # instead of raw field assignments.
    screen.draw_rectangle = types.MethodType(draw_rectangle, screen)
    screen.draw_text = types.MethodType(draw_text, screen)
    screen.draw_image = types.MethodType(draw_image, screen)

