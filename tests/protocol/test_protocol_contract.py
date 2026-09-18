import re
import struct
from pathlib import Path


ROOT_DIR = (
    Path(__file__)
    .resolve()
    .parents[2]
)


PROTOCOL_HEADER = (
    ROOT_DIR
    / "firmware"
    / "components"
    / "sgp_protocol"
    / "include"
    / "sgp_protocol.h"
)


IMAGE_HEADER = (
    ROOT_DIR
    / "firmware"
    / "components"
    / "sgp_image"
    / "include"
    / "sgp_image.h"
)


TELEMETRY_HEADER = (
    ROOT_DIR
    / "firmware"
    / "components"
    / "sgp_telemetry"
    / "include"
    / "sgp_telemetry.h"
)


def read_define(
    path: Path,
    name: str
) -> str:
    text = path.read_text(
        encoding="utf-8"
    )


    pattern = (
        rf"#define\s+"
        rf"{re.escape(name)}"
        rf"\s+([^\s/]+)"
    )


    match = re.search(
        pattern,
        text
    )


    assert match is not None, (
        f"{name} not found "
        f"in {path}"
    )


    return match.group(1)


def c_integer(
    value: str
) -> int:
    value = (
        value
        .replace("U", "")
        .replace("L", "")
    )


    return int(
        value,
        0
    )


def encode_header(
    version: int,
    message_type: int,
    flags: int,
    session_id: int,
    sequence: int
) -> bytes:

    return struct.pack(
        ">BBBII",
        version,
        message_type,
        flags,
        session_id,
        sequence
    )


def decode_header(
    packet: bytes
):
    return struct.unpack(
        ">BBBII",
        packet
    )


def test_protocol_version():
    value = read_define(
        PROTOCOL_HEADER,
        "SGP_VERSION"
    )


    assert c_integer(value) == 1


def test_header_size():
    value = read_define(
        PROTOCOL_HEADER,
        "SGP_HEADER_SIZE"
    )


    assert c_integer(value) == 11


def test_wire_header_vector():
    packet = encode_header(
        version=1,
        message_type=0x20,
        flags=0,
        session_id=0x12345678,
        sequence=0x00000009
    )


    assert len(packet) == 11


    assert packet.hex().upper() == (
        "012000"
        "12345678"
        "00000009"
    )


def test_wire_header_roundtrip():
    expected = (
        1,
        0x21,
        0,
        0xA1B2C3D4,
        42
    )


    packet = encode_header(
        version=expected[0],
        message_type=expected[1],
        flags=expected[2],
        session_id=expected[3],
        sequence=expected[4]
    )


    decoded = decode_header(
        packet
    )


    assert decoded == expected


def test_message_type_contract():
    expected = {
        "SGP_MSG_HELLO":
            0x01,

        "SGP_MSG_ACK":
            0x03,

        "SGP_MSG_HEARTBEAT":
            0x10,

        "SGP_MSG_SECURE_ACK":
            0x11
    }


    for name, value in (
        expected.items()
    ):
        actual = c_integer(
            read_define(
                PROTOCOL_HEADER,
                name
            )
        )


        assert actual == value


def test_image_message_types():
    expected = {
        "SGP_MSG_IMAGE_META":
            0x20,

        "SGP_MSG_IMAGE_CHUNK":
            0x21,

        "SGP_MSG_IMAGE_DONE":
            0x22
    }


    for name, value in (
        expected.items()
    ):
        actual = c_integer(
            read_define(
                IMAGE_HEADER,
                name
            )
        )


        assert actual == value


def test_image_chunk_size():
    value = c_integer(
        read_define(
            IMAGE_HEADER,
            "SGP_IMAGE_CHUNK_DATA_MAX"
        )
    )


    assert value == 200


def test_image_meta_size():
    value = c_integer(
        read_define(
            IMAGE_HEADER,
            "SGP_IMAGE_META_SIZE"
        )
    )


    assert value == 16


def test_telemetry_message_type():
    value = c_integer(
        read_define(
            TELEMETRY_HEADER,
            "SGP_MSG_TELEMETRY"
        )
    )


    assert value == 0x30


def test_telemetry_size():
    value = c_integer(
        read_define(
            TELEMETRY_HEADER,
            "SGP_TELEMETRY_SIZE"
        )
    )


    assert value == 28