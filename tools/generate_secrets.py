from pathlib import Path
import secrets


ROOT_DIR = (
    Path(__file__)
    .resolve()
    .parent
    .parent
)


OUTPUT_FILE = (
    ROOT_DIR
    / "firmware"
    / "components"
    / "sgp_config"
    / "include"
    / "sgp_secrets.h"
)


KEY_SIZE = 32


def format_key(
    key: bytes
) -> str:
    lines = []

    for index in range(
        0,
        len(key),
        4
    ):
        chunk = key[
            index:index + 4
        ]

        values = ", ".join(
            f"0x{value:02X}"
            for value in chunk
        )

        if index + 4 < len(key):
            values += ","

        lines.append(
            f"    {values}"
        )

    return " \\\n".join(
        lines
    )


def main() -> None:
    hmac_key = secrets.token_bytes(
        KEY_SIZE
    )

    kdf_key = secrets.token_bytes(
        KEY_SIZE
    )


    content = f"""\
#ifndef SGP_SECRETS_H
#define SGP_SECRETS_H


#define SGP_SECRET_KEY_SIZE {KEY_SIZE}U


#define SGP_HMAC_PSK_BYTES \\
{{ \\
{format_key(hmac_key)} \\
}}


#define SGP_KDF_MASTER_KEY_BYTES \\
{{ \\
{format_key(kdf_key)} \\
}}


#endif
"""


    OUTPUT_FILE.parent.mkdir(
        parents=True,
        exist_ok=True
    )


    OUTPUT_FILE.write_text(
        content,
        encoding="utf-8"
    )


    print(
        "Generated:"
    )

    print(
        OUTPUT_FILE
    )

    print()
    print(
        "IMPORTANT: "
        "Do not commit this file."
    )


if __name__ == "__main__":
    main()