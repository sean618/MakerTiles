'''
Copyright © 2024 - 2025 Sean Bremner. All rights reserved.>>
'''

import ctypes
from ctypes import c_char_p, c_int, c_bool
import sys
import logging
import threading
import time

from . import field_protocol

class DummyPort:
    def __init__(self):
        self.device = 'dummy'

class ClibConnection:
    def __init__(self, master_node_system_filepath, logger, use_mock_microbus=True):
        self._lock = threading.Lock()
        self.run_interations = 0
        self.logger = logger

        # if use_mock_microbus:
        #     path = '../FieldProtocol/build/'
        # else:
        #     path = '../../SystemTesting/build/'
        # # Load the shared library
        # if sys.platform.startswith('win'):
        #     self.lib = ctypes.CDLL(path + 'masterNodeSystem.dll')
        # elif sys.platform.startswith('darwin'):
        #     self.lib = ctypes.CDLL(path + 'libmasterNodeSystem.dylib')
        # else:
        #     self.lib = ctypes.CDLL(path + 'libmasterNodeSystem.so')
        
        self.lib = ctypes.CDLL(master_node_system_filepath)
        
        # Define function signatures
        self.lib.start_connection.argtypes = []
        self.lib.start_connection.restype = c_int
        
        self.lib.write_packet.argtypes = [c_char_p, c_int]
        self.lib.write_packet.restype = c_int
        
        self.lib.read_packet.argtypes = [c_char_p, c_int]
        self.lib.read_packet.restype = c_int

        self.lib.init.argtypes = [c_bool]
        self.lib.init.restype = None

        self.lib.run.argtypes = [c_int]
        self.lib.run.restype = None 

        self.lib.init(use_mock_microbus)
    
    def set_logger(self, logger):
        self.logger = logger

    def open_port(self, port_name):
        return DummyPort()

    # def start(self):
    #     with self._lock:
    #         return self.lib.start_connection()
    
    def write(self, packet):
        buffer = ctypes.create_string_buffer(packet)
        with self._lock:
            res = self.lib.write_packet(buffer, len(packet))
            self.lib.run(1)
        return res
    
    def read(self):
        bytes_read = 0
        buffer = ctypes.create_string_buffer(field_protocol.MAX_PACKET_SIZE)
        # TODO: continually polling - slow
        while bytes_read == 0:
            self.run_interations += 1
            # if self.run_interations % 10 == 0:
            #     self.logger.info("Run iterations %d", self.run_interations)
            with self._lock:
                self.lib.run(1)
                bytes_read = self.lib.read_packet(buffer, field_protocol.MAX_PACKET_SIZE)
            if bytes_read == 0:
                time.sleep(0.000001)
        packet_size = int.from_bytes(buffer[:2], byteorder='little')
        assert(bytes_read == packet_size)
        # RxManager expects read() to return an iterable of packets (see
        # Connection.read), so wrap the single packet in a list.
        return [bytes(buffer[2:packet_size])]
    
    def run(self, num_frames):
        with self._lock:
            self.lib.run(num_frames)

    # def out_waiting(self):
    #     return self.lib.num_in_usb_tx_buffer()

    def close(self):
        with self._lock:
            self.lib.close()