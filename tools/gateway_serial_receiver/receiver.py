import argparse
import base64
import json
import os
from datetime import datetime
from urllib import error, request

import serial


# ============================================================
# Gateway Serial Protocol Prefixes
# ============================================================

BEGIN_PREFIX = "@@SGP_IMG_BEGIN "
DATA_PREFIX = "@@SGP_IMG_DATA "
END_PREFIX = "@@SGP_IMG_END "
ABORT_PREFIX = "@@SGP_IMG_ABORT "

STATUS_PREFIX = "@@SGP_STATUS "


# ============================================================
# Helpers
# ============================================================

def parse_fields(
    text: str
) -> dict[str, str]:
    result: dict[str, str] = {}

    for part in text.strip().split():
        if "=" not in part:
            continue

        key, value = part.split(
            "=",
            1
        )

        result[key] = value

    return result


# ============================================================
# Image Backend Upload
# ============================================================

def upload_to_backend(
    backend_url: str,
    session_id: str,
    image_id: int,
    width: int,
    height: int,
    jpeg: bytes
) -> bool:

    http_request = request.Request(
        backend_url,
        data=jpeg,
        method="POST",
        headers={
            "Content-Type":
                "image/jpeg",

            "X-Session-Id":
                session_id,

            "X-Image-Id":
                str(image_id),

            "X-Image-Width":
                str(width),

            "X-Image-Height":
                str(height)
        }
    )


    try:
        with request.urlopen(
            http_request,
            timeout=5
        ) as response:

            response_body = (
                response
                .read()
                .decode("utf-8")
            )


            result = json.loads(
                response_body
            )


            print(
                "BACKEND:",
                result.get(
                    "status",
                    "ok"
                ),
                result.get(
                    "image_url",
                    ""
                )
            )


            return True


    except (
        error.URLError,
        TimeoutError,
        json.JSONDecodeError
    ) as exc:

        print(
            "BACKEND ERROR:",
            exc
        )


        return False


# ============================================================
# Device Status Backend Upload
# ============================================================

def upload_status(
    backend_url: str,
    fields: dict[str, str]
) -> bool:

    try:
        payload = {
            "session_id":
                fields["session"],

            "reset_reason":
                int(
                    fields["reset"]
                ),

            "uptime_ms":
                int(
                    fields["uptime"]
                ),

            "free_heap":
                int(
                    fields["free_heap"]
                ),

            "min_free_heap":
                int(
                    fields["min_heap"]
                ),

            "images_sent":
                int(
                    fields["images"]
                ),

            "capture_failures":
                int(
                    fields[
                        "capture_failures"
                    ]
                ),

            "transfer_failures":
                int(
                    fields[
                        "transfer_failures"
                    ]
                )
        }

    except (
        KeyError,
        ValueError
    ) as exc:

        print(
            "INVALID STATUS:",
            exc
        )

        return False


    body = json.dumps(
        payload
    ).encode(
        "utf-8"
    )


    http_request = request.Request(
        backend_url,
        data=body,
        method="POST",
        headers={
            "Content-Type":
                "application/json"
        }
    )


    try:
        with request.urlopen(
            http_request,
            timeout=5
        ) as response:

            response_body = (
                response
                .read()
                .decode(
                    "utf-8"
                )
            )


            if response_body:
                try:
                    result = json.loads(
                        response_body
                    )

                    print(
                        "STATUS BACKEND:",
                        result.get(
                            "status",
                            "saved"
                        )
                    )

                except json.JSONDecodeError:
                    print(
                        "STATUS BACKEND: saved"
                    )

            else:
                print(
                    "STATUS BACKEND: saved"
                )


            return True


    except (
        error.URLError,
        TimeoutError
    ) as exc:

        print(
            "STATUS BACKEND ERROR:",
            exc
        )


        return False


# ============================================================
# Main
# ============================================================

def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Secure Visual Gateway "
            "serial receiver"
        )
    )


    parser.add_argument(
        "--port",
        default="COM9",
        help="Gateway serial port"
    )


    parser.add_argument(
        "--baud",
        type=int,
        default=115200,
        help="Serial baud rate"
    )


    parser.add_argument(
        "--output",
        default="received_images",
        help=(
            "Directory used for local "
            "JPEG storage"
        )
    )


    parser.add_argument(
        "--backend",
        default=(
            "http://127.0.0.1:"
            "8000/api/images"
        ),
        help="Backend image endpoint"
    )


    parser.add_argument(
        "--status-backend",
        default=(
            "http://127.0.0.1:"
            "8000/api/status"
        ),
        help="Backend status endpoint"
    )


    args = parser.parse_args()


    os.makedirs(
        args.output,
        exist_ok=True
    )


    current = None


    print(
        f"Opening {args.port} at "
        f"{args.baud} baud..."
    )


    print(
        "Image backend:",
        args.backend
    )


    print(
        "Status backend:",
        args.status_backend
    )


    try:
        serial_port = serial.Serial(
            args.port,
            args.baud,
            timeout=1
        )

    except serial.SerialException as exc:
        print(
            "SERIAL ERROR:",
            exc
        )

        return


    print(
        "Waiting for Gateway data. "
        "Press Ctrl+C to stop."
    )


    with serial_port as ser:

        while True:
            try:
                raw = ser.readline()

            except KeyboardInterrupt:
                print(
                    "\nStopped."
                )

                break


            except serial.SerialException as exc:
                print(
                    "SERIAL ERROR:",
                    exc
                )

                break


            if not raw:
                continue


            line = raw.decode(
                "ascii",
                errors="replace"
            ).strip()


            if not line:
                continue


            # =================================================
            # Device telemetry / status
            # =================================================

            if line.startswith(
                STATUS_PREFIX
            ):
                fields = parse_fields(
                    line[
                        len(
                            STATUS_PREFIX
                        ):
                    ]
                )


                upload_status(
                    args.status_backend,
                    fields
                )


                continue


            # =================================================
            # Image begin
            # =================================================

            if line.startswith(
                BEGIN_PREFIX
            ):
                fields = parse_fields(
                    line[
                        len(
                            BEGIN_PREFIX
                        ):
                    ]
                )


                try:
                    current = {
                        "session":
                            fields[
                                "session"
                            ],

                        "id":
                            int(
                                fields["id"]
                            ),

                        "size":
                            int(
                                fields["size"]
                            ),

                        "width":
                            int(
                                fields["width"]
                            ),

                        "height":
                            int(
                                fields["height"]
                            ),

                        "parts":
                            []
                    }


                except (
                    KeyError,
                    ValueError
                ):
                    print(
                        "Invalid IMG_BEGIN:",
                        line
                    )

                    current = None

                    continue


                print(
                    "Receiving image "
                    f"session="
                    f"{current['session']} "
                    f"id="
                    f"{current['id']} "
                    f"{current['width']}x"
                    f"{current['height']} "
                    f"{current['size']} bytes"
                )


                continue


            # =================================================
            # Image Base64 data
            # =================================================

            if line.startswith(
                DATA_PREFIX
            ):
                if current is not None:
                    current[
                        "parts"
                    ].append(
                        line[
                            len(
                                DATA_PREFIX
                            ):
                        ]
                    )


                continue


            # =================================================
            # Image export aborted
            # =================================================

            if line.startswith(
                ABORT_PREFIX
            ):
                print(
                    "Gateway aborted "
                    "image export"
                )


                current = None


                continue


            # =================================================
            # Image completed
            # =================================================

            if line.startswith(
                END_PREFIX
            ):
                if current is None:
                    print(
                        "IMG_END received "
                        "without active image"
                    )

                    continue


                try:
                    encoded = "".join(
                        current["parts"]
                    )


                    jpeg = (
                        base64.b64decode(
                            encoded,
                            validate=True
                        )
                    )


                except Exception as exc:
                    print(
                        "Base64 decode failed:",
                        exc
                    )


                    current = None


                    continue


                # ---------------------------------------------
                # Length validation
                # ---------------------------------------------

                if (
                    len(jpeg) !=
                    current["size"]
                ):
                    print(
                        "Size mismatch: "
                        f"expected="
                        f"{current['size']} "
                        f"received="
                        f"{len(jpeg)}"
                    )


                    current = None


                    continue


                # ---------------------------------------------
                # JPEG marker validation
                # ---------------------------------------------

                if (
                    len(jpeg) < 4
                    or jpeg[0:2] !=
                    b"\xFF\xD8"
                    or jpeg[-2:] !=
                    b"\xFF\xD9"
                ):
                    print(
                        "JPEG validation failed"
                    )


                    current = None


                    continue


                # ---------------------------------------------
                # Local filename
                # ---------------------------------------------

                timestamp = (
                    datetime.now()
                    .strftime(
                        "%Y%m%d_"
                        "%H%M%S_"
                        "%f"
                    )
                )


                session_name = (
                    current[
                        "session"
                    ]
                    .replace(
                        "0x",
                        ""
                    )
                    .replace(
                        "0X",
                        ""
                    )
                )


                filename = (
                    f"{session_name}_"
                    f"image_"
                    f"{current['id']:06d}_"
                    f"{timestamp}.jpg"
                )


                path = os.path.join(
                    args.output,
                    filename
                )


                # ---------------------------------------------
                # Save JPEG locally
                # ---------------------------------------------

                try:
                    with open(
                        path,
                        "wb"
                    ) as file:

                        file.write(
                            jpeg
                        )

                except OSError as exc:
                    print(
                        "FILE ERROR:",
                        exc
                    )


                    current = None


                    continue


                print(
                    f"SAVED: {path} "
                    f"({len(jpeg)} bytes)"
                )


                # ---------------------------------------------
                # Upload JPEG to backend
                # ---------------------------------------------

                upload_to_backend(
                    args.backend,
                    current[
                        "session"
                    ],
                    current[
                        "id"
                    ],
                    current[
                        "width"
                    ],
                    current[
                        "height"
                    ],
                    jpeg
                )


                current = None


                continue


            # =================================================
            # Normal Gateway logs
            # =================================================

            print(
                line
            )


if __name__ == "__main__":
    main()