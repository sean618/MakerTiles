'''
Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
'''

# Contains a lot of the field protocol definitions and packet parsing functions

from enum import Enum
import struct
from typing import Tuple

# TODO: where to get these from
MAX_PACKET_SIZE = 192-14
MASTER_DAEMON_HEADER_SIZE = 11
MAX_PACKET_DATA_SIZE = MAX_PACKET_SIZE - MASTER_DAEMON_HEADER_SIZE

MAX_NODES = 64
MASTER_NODE_ID = 0

# Wire framing limits. A packet is prefixed by its 2-byte little-endian size;
# anything outside this range means the stream is out of sync.
PACKET_SIZE_PREFIX_LEN = 2
MIN_PACKET_SIZE = PACKET_SIZE_PREFIX_LEN
MAX_FRAMED_PACKET_SIZE = 256

# ---------------------------------------------------------------------------
# Packet layout. These byte offsets must match the firmware exactly: the Tx
# side encodes with REQ_* and the Rx side decodes with RESP_*. A request packet
# carries the 2-byte size prefix; a response has already had that prefix
# stripped by Connection.read, so its offsets are shifted left by 2.
# ---------------------------------------------------------------------------

# Request header (includes the size prefix), total MASTER_DAEMON_HEADER_SIZE bytes.
REQ_SIZE = slice(0, 2)        # uint16 total packet size
REQ_RETURN_RX_CREDITS = 2     # unused on the way out
REQ_RETURN_TX_CREDITS = 3     # unused on the way out
REQ_DST_NODE = 4
REQ_ID = 5
REQ_COMMAND = 6
REQ_NUM_FIELDS = slice(7, 9)  # uint16
REQ_FIELD_INDEX = slice(9, 11)  # uint16

# Response header (size prefix already stripped).
RESP_RETURN_RX_CREDITS = 0
RESP_RETURN_TX_CREDITS = 1
RESP_SRC_NODE = 2
RESP_REQUEST_ID = 3
RESP_COMMAND = 4
RESP_NUM_FIELDS = slice(5, 7)  # uint16
RESP_FIELD_INDEX = slice(7, 9)  # uint16
RESP_DATA_START = 9


class Command(Enum):
    NULL_COMMAND = 0
    GET_TILE_INFO = 1
    SET_TILE_INFO = 2
    SENDING_TILE_INFO = 3
    GET_FIELD_INFO = 4
    SENDING_FIELD_INFO = 5
    GET_FIELDS = 6
    SENDING_FIELDS = 7
    GET_MAX_BUFFER_CREDITS = 8
    SENDING_MAX_BUFFER_CREDITS = 9
    GET_CONNECTED_NODES = 10
    SENDING_CONNECTED_NODES = 11
    RETURNING_CREDITS = 12

    @classmethod
    def from_byte(cls, value: int):
        """Decode a command byte, returning None for an unrecognized opcode.

        Lets the receiver distinguish a valid-but-unknown command (e.g. newer
        firmware) from a genuinely corrupt/unparseable packet, rather than
        raising ValueError and discarding the rest of the header.
        """
        try:
            return cls(value)
        except ValueError:
            return None

# Don't use an enum for performance reasons
FIELD_DATA_TYPE_NULL = 0
FIELD_DATA_TYPE_RAW = 1             # Any size
FIELD_DATA_TYPE_ENUM = 2            # 1 byte
FIELD_DATA_TYPE_BOOLEAN = 3         # 1 byte
FIELD_DATA_TYPE_UINT = 4            # 1,2,4,8 bytes
FIELD_DATA_TYPE_INT = 5             # 1,2,4,8 bytes
FIELD_DATA_TYPE_FLOAT = 6           # 4 bytes
FIELD_DATA_TYPE_TIME = 7
FIELD_DATA_TYPE_UTF8_CHAR = 8       # 1 byte
FIELD_DATA_TYPE_UTF8_STRING = 9     # Any size
FIELD_DATA_TYPE_DICTIONARY = 10

def decode_field_data(data : memoryview, idx : int, size : int, field_type : int) -> Tuple[object, int]:
    if field_type == FIELD_DATA_TYPE_UTF8_STRING:
        str_len = data[idx]
        text = (data[idx + 1:idx + str_len+1]).tobytes().decode("utf-8")
        return text, idx + str_len + 1
    else:
        new_idx = idx + size
        field_data = data[idx:new_idx]
        if field_type == FIELD_DATA_TYPE_RAW:
            value = field_data
        elif field_type == FIELD_DATA_TYPE_BOOLEAN:
            value = bool(field_data[0])
        elif field_type == FIELD_DATA_TYPE_UINT:
            value = int.from_bytes(field_data, "little", signed=False)
        elif field_type == FIELD_DATA_TYPE_INT:
            value = int.from_bytes(field_data, "little", signed=True)
        elif field_type == FIELD_DATA_TYPE_FLOAT:
            value = struct.unpack('<f', field_data)[0]
        elif field_type == FIELD_DATA_TYPE_UTF8_CHAR:
            value = field_data.tobytes().decode("utf-8")
        elif field_type == FIELD_DATA_TYPE_ENUM:
            value = int.from_bytes(field_data, "little", signed=False)
        return value, new_idx

def encode_field_data(values : list, num_values : int, field_type : int, size : int) -> bytes:
    if field_type == FIELD_DATA_TYPE_RAW:
        return bytes(values)
    elif field_type == FIELD_DATA_TYPE_BOOLEAN:
        return struct.pack(f"<{num_values}B", *map(int, values))
    elif field_type == FIELD_DATA_TYPE_UINT:
        if size == 1:
            return struct.pack(f"<{num_values}B", *values)
        elif size == 2:
            return struct.pack(f"<{num_values}H", *values)
        elif size == 4:
            return struct.pack(f"<{num_values}I", *values)
        else:
            result = bytearray()
            for x in values:
                result += x.to_bytes(size, 'little')
            return result
    elif field_type == FIELD_DATA_TYPE_INT:
        if size == 1:
            return struct.pack(f"<{num_values}b", *values)
        elif size == 2:
            return struct.pack(f"<{num_values}h", *values)
        elif size == 4:
            return struct.pack(f"<{num_values}i", *values)
        else:
            result = bytearray()
            for x in values:
                result += x.to_bytes(size, 'little')
            return result
    elif field_type == FIELD_DATA_TYPE_FLOAT:
        if size == 4:
            return struct.pack(f"<{num_values}f", *values)
        # elif size == 8:
        #     return struct.pack(f"<{num_values}d", *values)
    elif field_type == FIELD_DATA_TYPE_UTF8_CHAR:
        return struct.pack(f"<{num_values}B", *map(ord, values))
    elif field_type == FIELD_DATA_TYPE_UTF8_STRING:
        bytes = bytearray()
        for str in values:
            new_bytes = str.encode("utf-8")
            new_bytes_len = len(new_bytes)
            assert new_bytes_len < 256
            bytes += new_bytes_len.to_bytes(1, 'little') + new_bytes
        return bytes
    # elif field_type == FIELD_DATA_TYPE_ENUM:
    #     return  value.to_bytes(1, "little")

def uuid_to_alphanumeric(uuid: int):
    # Define the character set for the alphanumeric string
    chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    # Convert the integer to a base-62 representation
    result = ""
    while uuid:
        uuid, remainder = divmod(uuid, 62)
        result = chars[remainder] + result
    return result

class Request:
    def __init__(self, dst_node: int, command: Command, field_index: int, num_fields: int, data: bytes = None, response_event = None) -> None:
        self.packet = None
        self.id = 0
        self.dst_node = dst_node
        self.command = command
        self.field_index = field_index
        self.num_fields = num_fields
        self.data = data
        self.response_event = response_event

def request_to_packet(request) -> bytes:
    packet = bytearray(MASTER_DAEMON_HEADER_SIZE)
    packet_size = MASTER_DAEMON_HEADER_SIZE + (len(request.data) if request.data else 0)
    packet[REQ_SIZE] = packet_size.to_bytes(2, byteorder='little')
    packet[REQ_RETURN_RX_CREDITS] = 0  # return credits - not used here
    packet[REQ_RETURN_TX_CREDITS] = 0  # return credits - not used here
    packet[REQ_DST_NODE] = request.dst_node
    packet[REQ_ID] = request.id
    packet[REQ_COMMAND] = request.command.value
    packet[REQ_NUM_FIELDS] = request.num_fields.to_bytes(2, byteorder='little')
    packet[REQ_FIELD_INDEX] = request.field_index.to_bytes(2, byteorder='little')
    if request.data is not None:
        assert len(request.data) < MAX_PACKET_DATA_SIZE
        packet = packet + request.data
    return packet

def set_id_in_packet(packet : bytes, id : int):
    packet[REQ_ID] = id

class Response:
    def __init__(self, packet : bytes) -> None:
        # 2 byte packet size has already been stripped off
        self.return_rx_credits = packet[RESP_RETURN_RX_CREDITS]
        self.return_tx_credits = packet[RESP_RETURN_TX_CREDITS]
        self.src_node = packet[RESP_SRC_NODE]
        self.request_id = packet[RESP_REQUEST_ID]
        self.command_byte = packet[RESP_COMMAND]
        # None if the opcode isn't recognized; the rx handler treats that as an
        # unknown-command warning rather than a packet parse failure.
        self.command = Command.from_byte(self.command_byte)
        self.num_fields = int.from_bytes(packet[RESP_NUM_FIELDS], byteorder='little')
        self.field_index = int.from_bytes(packet[RESP_FIELD_INDEX], byteorder='little')
        self.data = packet[RESP_DATA_START:]
        self.packet = packet

def request_to_string(request : Request):
    num_fields = int.from_bytes(request.packet[7:9], byteorder='little')
    field_index = int.from_bytes(request.packet[9:11], byteorder='little')
    if num_fields != request.num_fields or field_index != request.field_index:
        return f"node:{request.dst_node} - ID:{request.id} {request.command} starting {request.field_index} num {request.num_fields} (packet starting {field_index} num {num_fields}) - Packet header: {bytes(request.packet)[0:11]}, payload: {bytes(request.packet)[11:]}"
    else:
        return f"node:{request.dst_node} - ID:{request.id} {request.command} starting {request.field_index} num {request.num_fields} - Packet header: {bytes(request.packet)[0:11]}, payload: {bytes(request.packet)[11:]}"

def response_to_string(response : Response):
    return f"node:{response.src_node} - ID:{response.request_id} {response.command} starting {response.field_index} num {response.num_fields} - Packet header: {bytes(response.packet)[0:9]}, payload: {bytes(response.data)}"


