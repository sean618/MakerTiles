
import pytest
from makertiles import field_protocol

def test_encoding_decoding():
    test_values = [
        # (field_protocol.FIELD_DATA_TYPE_RAW,       1, bytes([1,2,3,4])),
        (field_protocol.FIELD_DATA_TYPE_BOOLEAN,   1, [True, False, False, True]),
        (field_protocol.FIELD_DATA_TYPE_UINT,      1, [1,2,3,4]),
        (field_protocol.FIELD_DATA_TYPE_UINT,      2, [7000, 8000, 9000, 10000]),
        (field_protocol.FIELD_DATA_TYPE_UINT,      3, [70000, 80000, 90000, 100000]),
        (field_protocol.FIELD_DATA_TYPE_UINT,      4, [2^24+10, 2^24+11, 2^25+10, 2^26+10]),
        (field_protocol.FIELD_DATA_TYPE_INT,      1, [1,2,3,4]),
        (field_protocol.FIELD_DATA_TYPE_INT,      2, [7000, 8000, 9000, 10000]),
        (field_protocol.FIELD_DATA_TYPE_INT,      3, [70000, 80000, 90000, 100000]),
        (field_protocol.FIELD_DATA_TYPE_INT,      4, [2^24+10, 2^24+11, 2^25+10, 2^26+10]),
        (field_protocol.FIELD_DATA_TYPE_FLOAT,     4, [1.00, 0.1, 1/3, 1/7]),
        (field_protocol.FIELD_DATA_TYPE_UTF8_CHAR, 1, ['a', 'b', 'c', 'd']),
        (field_protocol.FIELD_DATA_TYPE_UTF8_STRING, 1, ["hello", "world", "welcome", ""]),
    ]
    for (type, size, values) in test_values:
        data = memoryview(field_protocol.encode_field_data(values, len(values), type, size))
        idx = 0
        for i, value in enumerate(values):
            result, idx = field_protocol.decode_field_data(data, idx, size, type)
            if type == field_protocol.FIELD_DATA_TYPE_FLOAT:
                math.isclose(result, value, rel_tol=1e-9)
            else:
                assert result == value
            import math


if __name__ == "__main__":
    test_encoding_decoding()
