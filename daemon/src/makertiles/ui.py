'''
Copyright © 2024 - 2025 Sean Bremner. All rights reserved.>>
'''

# from pprint import pprint
# import board_methods
import time
import types

from . import board
from . import ui_methods

class ArrayFieldDescriptor:

    def __init__(self, field : board.Field, board_manager, board, uiboard):
        self.uiboard = uiboard
        self.field = field
        self.board_manager = board_manager
        self.board = board

    def __getitem__(self, key):
        if self.field.streaming:
            return self.board.read_stream(self.field.start_field)

        if isinstance(key, slice):
            start, stop, step = key.indices(self.field.span)
            if step != 1:
                raise ValueError("A step of 1 must be used to access field arrays")

            self.board_manager.get_fields(self.board.id, self.field.start_field + start, stop - start, self.uiboard._get_timeout)
            return self.field.data[start:stop]

        elif isinstance(key, int):
            if key < 0:
                key += self.field.span
            if key < 0 or key >= self.field.span:
                raise IndexError("Field index out of range")

            self.board_manager.get_fields(self.board.id, self.field.start_field + key, 1, self.uiboard._get_timeout)
            return self.field.data[key]
        else:
            raise TypeError("Field indices must be integers or slices")

    def __setitem__(self, key, value):
        # print("Setting field array key", key)
        self.board_manager.logger.info(f"Setting attribute {self.field.name}[{key}]: {value}")
        if isinstance(key, slice):
            start, stop, step = key.indices(self.field.span)
            if step != 1:
                raise ValueError("Only step of 1 is supported for setting slices")
            if not hasattr(value, '__len__'):
                raise ValueError(f"assignment object is not a list")
            if len(value) != stop - start:
                raise ValueError(f"assignment has a different length exp:{stop - start}, got:{len(value)}")
            old_value = self.field.data[start:stop]
            self.field.data[start:stop] = value
            # Writes are fire-and-forget; the only catchable failure is a local
            # encoding error, so roll the local cache back to keep it in sync.
            try:
                self.board_manager.set_fields(self.board.id, self.field.start_field + start, stop - start, self.uiboard._set_timeout)
            except Exception as e:
                self.field.data[start:stop] = old_value
                raise TypeError(f"Cannot encode {value!r} for field {self.field.name}") from e


        elif isinstance(key, int):
            if key < 0 or key >= self.field.span:
                raise IndexError("Field index out of range")
            old_value = self.field.data[key]
            self.field.data[key] = value
            try:
                self.board_manager.set_fields(self.board.id, self.field.start_field + key, 1, self.uiboard._set_timeout)
            except Exception as e:
                self.field.data[key] = old_value
                raise TypeError(f"Cannot encode {value!r} for field {self.field.name}") from e
        else:
            raise TypeError("Field indices must be integers or slices")

    def __len__(self):
        return self.field.span

    def __iter__(self):
        self.board_manager.get_fields(self.board.id, self.field.start_field, self.field.span, self.uiboard._get_timeout)
        return iter(self.field.data)


class FieldDescriptor:

    def __init__(self, field : board.Field, board_manager, board, uiboard):
        self.uiboard = uiboard
        self.field = field
        self.board_manager = board_manager
        self.board = board

    def local_get(self):
        """Return the cached value without triggering a device read."""
        return self.field.data[0]

    def get(self):
        if self.field.streaming:
            return self.board.read_stream(self.field.start_field)

        self.board_manager.get_fields(self.board.id, self.field.start_field, 1, self.uiboard._get_timeout)
        return self.field.data[0]

    def set(self, value):
        # print("Setting field", self.field.name, " value", value)
        # Writes are fire-and-forget (non-blocking): set_fields queues the
        # request and returns without waiting for the device. The only failure
        # we can catch here is a local encoding error (the value doesn't fit the
        # field's type), so roll the local cache back to keep it in sync.
        old_value = self.field.data[0]
        self.field.data[0] = value
        try:
            self.board_manager.set_fields(self.board.id, self.field.start_field, 1, self.uiboard._set_timeout)
        except Exception as e:
            self.field.data[0] = old_value
            raise TypeError(f"Cannot encode {value!r} for field {self.field.name}") from e

class UIBoard:

    def __init__(self, board_manager, logger, board):
        object.__setattr__(self, "_logger", logger)
        object.__setattr__(self, "_set_timeout", 0.0)
        object.__setattr__(self, "_get_timeout", 2.0)
        object.__setattr__(self, "_fields", board.fields)
        object.__setattr__(self, "_board_manager", board_manager)
        object.__setattr__(self, "_board", board)
        object.__setattr__(self, "_field_names", [])
        object.__setattr__(self, "_method_names", [])
        object.__setattr__(self, "_field_objs", [])

        for idx, field in board.fields.items():
            self._field_names.append(field.name)
            if field.span == 1:
                field_obj = FieldDescriptor(field, board_manager, board, self)
                super().__setattr__(field.name, field_obj)
            elif field.span > 1:
                field_obj = ArrayFieldDescriptor(field, board_manager, board, self)
                super().__setattr__(field.name, field_obj)
            else:
                raise ValueError("Field with zero span")
            self._field_objs.append(field_obj)

        if board.name == "screen":
            ui_methods.add_screen_methods(self)
    
    def __getattribute__(self, name):
        if name[0] == '_':
            return super().__getattribute__(name)
        elif name in self._field_names:
            field = super().__getattribute__(name)
            # print(name)
            self._logger.info(f"Getting attribute {name}")
            if isinstance(field, FieldDescriptor):
                return field.get()
            elif isinstance(field, ArrayFieldDescriptor):
                return field
        elif name in self._method_names:
            return super().__getattribute__(name)
        else:
            raise AttributeError('"' + self._board.full_name + '" board has no field "' + name + '"')
        

    def __setattr__(self, name, value):
        if name[0] == '_':
            object.__setattr__(self, name, value)
        elif name in self._field_names:
            self._logger.info(f"Setting attribute {name} : {value}")
            field = super().__getattribute__(name)
            field.set(value)
        elif isinstance(value, types.MethodType):
            object.__setattr__(self, name, value)
            self._method_names.append(name)
        else:
            raise AttributeError('"' + self._board.full_name + '" board has no field "' + name + '"')
            


class UITiles():
    def __init__(self, logger):
        self._logger = logger
        pass

    # All tiles are dynamically created and added to the UITiles object
