'''
Copyright © 2024 - 2025 Sean Bremner. All rights reserved.>>
'''

from typing import List, Tuple
import threading

from . import field_protocol

class Field:
    def __init__(self, index : int) -> None:
        self.index = index
        self.data = []
        self.name = ""
        self.units = ""
        self.start_field = 0
        self.span = 0
        self.type = None
        self.size = 0
        self.gettable = False
        self.settable = False

class Board:

    def __init__(self, id : int, logger) -> None:
        self.logger = logger
        self.id = id
        self.name = ""
        self.custom_name = ""
        self.unique_id = 0
        self.num_fields = 0
        self.fields = {}

    def get_field_from_flat_index(self, flat_field_index : int) -> Field:
        for field in self.fields.values():
            if flat_field_index >= field.start_field and flat_field_index < field.start_field + field.span:
                return field
        return None


    def update_tile_info_from_packet(self, data: bytes) -> None:
        idx = 0
        self.name, idx          = field_protocol.decode_field_data(data, idx, 0, field_protocol.FIELD_DATA_TYPE_UTF8_STRING)
        self.custom_name, idx   = field_protocol.decode_field_data(data, idx, 0, field_protocol.FIELD_DATA_TYPE_UTF8_STRING)
        self.unique_id, idx     = field_protocol.decode_field_data(data, idx, 8, field_protocol.FIELD_DATA_TYPE_INT)
        self.num_fields, idx    = field_protocol.decode_field_data(data, idx, 4, field_protocol.FIELD_DATA_TYPE_INT)
        self.full_name = self.name + "_" + field_protocol.uuid_to_alphanumeric(self.unique_id)
        self.logger.info(f"Tile info {self.id}: {self.full_name}, {self.custom_name}, {self.num_fields}, {self.unique_id}")


    def update_field_info_from_packet(self, field_index : int, data : bytes) -> None:
        field = Field(field_index)
        idx = 0
        field.name, idx          = field_protocol.decode_field_data(data, idx, 0, field_protocol.FIELD_DATA_TYPE_UTF8_STRING)
        field.start_field, idx   = field_protocol.decode_field_data(data, idx, 2, field_protocol.FIELD_DATA_TYPE_INT)
        field.span, idx          = field_protocol.decode_field_data(data, idx, 2, field_protocol.FIELD_DATA_TYPE_INT)
        field.type, idx          = field_protocol.decode_field_data(data, idx, 1, field_protocol.FIELD_DATA_TYPE_ENUM)
        field.size, idx          = field_protocol.decode_field_data(data, idx, 1, field_protocol.FIELD_DATA_TYPE_INT)
        raw_flags,  idx          = field_protocol.decode_field_data(data, idx, 1, field_protocol.FIELD_DATA_TYPE_INT)
        field.units, idx         = field_protocol.decode_field_data(data, idx, 0, field_protocol.FIELD_DATA_TYPE_UTF8_STRING)
        field.gettable   = ((raw_flags >> 0) & 1) == 1
        field.settable   = ((raw_flags >> 1) & 1) == 1
        field.joined     = ((raw_flags >> 2) & 1) == 1
        field.streaming  = ((raw_flags >> 3) & 1) == 1
        field.data = [0] * field.span
        if field.streaming:
            field.stream_lock = threading.Lock()
            field.data_stream = []
        self.fields[field_index] = field
        self.logger.info(f"Tile {self.id} field info {field_index}: {field.name}")

    def read_stream(self, flat_field_index : int):
        for idx, tmp_field in self.fields.items():
            if flat_field_index < tmp_field.start_field + tmp_field.span:
                if flat_field_index < tmp_field.start_field:
                    raise ValueError("Invalid field index:", flat_field_index, " - ", tmp_field.start_field, tmp_field.span)
                field = tmp_field
                break
        if field.streaming:
            with field.stream_lock:
                data_to_process = field.data_stream[:]
                field.data_stream.clear()
                return data_to_process

    def update_field_data_from_packet(self, flat_field_index, num_flat_fields, incoming_data) -> None:
        remaining_fields = num_flat_fields
        fi = flat_field_index
        pos = 0
        for idx, field in self.fields.items():
            if fi < field.start_field + field.span:
                if fi < field.start_field:
                    raise ValueError("Invalid field index:", fi)
                offset = fi - field.start_field
                num_sub_fields = min(remaining_fields, field.span - offset)

                # Convert from packet to field data
                for i in range(num_sub_fields):
                    try:
                        field.data[i + offset], pos = field_protocol.decode_field_data(incoming_data, pos, field.size, field.type)
                    except Exception:
                        self.logger.exception(f"Error processing read field data, field:{i}, name:{field.name}, size:{field.size}, read_data:{incoming_data.hex()}")

                if field.streaming:
                    with field.stream_lock:
                        field.data_stream += field.data[offset:offset+num_sub_fields]

                fi += num_sub_fields
                remaining_fields -= num_sub_fields
                if remaining_fields == 0:
                    return

    def convert_field_data_to_packets(self, flat_field_index, num_flat_fields) -> List[Tuple[int, int, bytearray]]:
        packets = []
        remaining_fields = num_flat_fields
        ffi = flat_field_index
        packet_start_field = flat_field_index
        packet_data = bytearray()
        packet_num_fields = 0
        packet_len = 0

        for field in self.fields.values():
            if ffi < field.start_field + field.span:
                field_size = field.size
                field_span = field.span
                start_field = field.start_field
                if ffi < start_field:
                    raise ValueError("Invalid field index:", ffi)
                
                # How many subfields are left in this field
                remaining_subfields = min(remaining_fields, start_field + field_span - ffi)

                while remaining_subfields > 0:
                    # How many more subfields can fit in this packet
                    num_sub_fields = min(remaining_subfields, (field_protocol.MAX_PACKET_DATA_SIZE - packet_len) // field_size)

                    if num_sub_fields > 0:
                        offset = ffi - start_field
                        try:
                            new_bytes = field_protocol.encode_field_data(field.data[offset:offset+num_sub_fields], num_sub_fields, field.type, field_size)
                        except(Exception):
                            # self.logger.exception(f"Error processing read field data, field:{i}, name:{field.name}, size:{field.size}, read_data:{incoming_data.hex()}")
                            raise Exception(f"Field {start_field}, offset:{offset}: data is invalid format, exp:{field.type} data:{field.data[offset:offset+num_sub_fields]}")
                        packet_data += new_bytes
                        packet_len += num_sub_fields * field_size
                        remaining_subfields -= num_sub_fields
                        packet_num_fields += num_sub_fields
                        ffi += num_sub_fields
                        remaining_fields -= num_sub_fields

                    # If there are remaining fields then we've filled this packet and need a new one
                    if remaining_subfields > 0:
                        packets.append((packet_start_field, packet_num_fields, packet_data))
                        packet_start_field = packet_start_field + packet_num_fields
                        packet_num_fields = 0
                        packet_data = bytearray()
                        packet_len = 0

                if remaining_fields == 0:
                    if len(packet_data) > 0:
                        packets.append((packet_start_field, packet_num_fields, packet_data))
                    return packets
        
        return packets
    

    def print(self) -> None:
        print(self.name, self.unique_id, field_protocol.uuid_to_alphanumeric(self.unique_id))
        for field in self.fields.values():
            if field.name == "":
                print("Failed to read field name")
            else:
                print("    ", field.name, ":", field.data[0] if 0 in field.data else "Failed to read")


