'''
Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
'''

import logging
import threading
import serial
import serial.tools.list_ports

import time

from . import field_protocol

# The MakerTiles master node enumerates as a 10 Mbaud CDC serial device.
BAUD_RATE = 10000000

class Connection:
    def __init__(self):
        self.port = None
        self.saved_byte = None
        self.logger = None
        self._lock = threading.Lock()
        self._prev_read_data = bytes([])

    def set_logger(self, logger):
        self.logger = logger

    def open_port(self, port_name):
        if port_name == None:
            for port in serial.tools.list_ports.comports():
                if port.product and 'MakerTiles' in port.product:
                    if port_name != None:
                        raise Exception("Multiple MakerTiles devices found. Specify port name.")
                    port_name = port.device
            if port_name == None:
                raise Exception("No MakerTiles device found. Check it is plugged in.")

        try:
            self.port = serial.Serial(port_name, baudrate=BAUD_RATE, write_timeout=1.0, timeout=0.0)
            return port_name
        except serial.SerialException as e:
            if self.logger is not None:
                self.logger.error("Failed to open %s: %s", port_name, e)
            return None


    def write(self, packet):
        try:
            self.last_packet = packet
            if self.port is None:
                raise Exception("Connection not open")
            with self._lock:
                num_written = self.port.write(packet)
                self.logger.info(f"Tx: {num_written} bytes")
            # print(f"Write data: {packet}")
            assert len(packet) == num_written
            return num_written
        except Exception:
            self.logger.exception("Write failed, closing port")
            self.close()
            return None

    # def write_test(self):
    #     self.port.write(self.last_packet)


    def read(self):
        try:
            packets = []
            if self.port is None:
                raise Exception("Connection not open")
            
            self.port.timeout = 0.001

            data = self.port.read(1000000)
            # if len(data) > 0:
            #     self.logger.info(f"Read {len(data)} bytes")

            if data:
                # if len(self._prev_read_data) > 0:
                #     self.logger.info(f"Adding {len(self._prev_read_data)} prev bytes")

                read_data = self._prev_read_data + data
                self._prev_read_data = bytes()

                idx = 0
                prefix_len = field_protocol.PACKET_SIZE_PREFIX_LEN
                while idx < len(read_data):
                    packet_size = int.from_bytes(read_data[idx:idx + prefix_len], byteorder='little', signed=False)
                    if packet_size < field_protocol.MIN_PACKET_SIZE:
                        self.logger.info(f'packet_size < {field_protocol.MIN_PACKET_SIZE}, packet_size:{packet_size}, prevLength:{len(self._prev_read_data)}, newLength:{len(data)}, idx:{idx}, data:{read_data[idx:].hex()}')
                        break
                    if packet_size > field_protocol.MAX_FRAMED_PACKET_SIZE:
                        self.logger.info(f"packet_size > {field_protocol.MAX_FRAMED_PACKET_SIZE} - {packet_size}")
                        break
                    if len(read_data[idx:]) < packet_size:
                        self._prev_read_data = read_data[idx:]
                        # self.logger.info(f"Packet size: {packet_size}, Storing {len(self._prev_read_data)}, data:{self._prev_read_data}")
                        break
                    packets.append(read_data[idx + prefix_len : idx + packet_size])
                    # self.logger.info(f"Got packet: size:{packet_size}, data:{read_data[idx + prefix_len : idx + packet_size].hex()}")
                    idx += packet_size
            return packets
        except Exception:
            self.logger.exception("Read failed, closing port")
            self.close()
            return []

    def close(self):
        if self.port is not None:
            self.logger.info("Port closed")
            self.port.close()
            self.port = None
