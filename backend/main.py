import os
from datetime import datetime, timezone
from pathlib import Path
import sqlite3

from fastapi import FastAPI, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles


BASE_DIR = Path(__file__).resolve().parent

DATA_DIR = Path(
    os.environ.get(
        "SVG_DATA_DIR",
        str(BASE_DIR / "data")
    )
).resolve()

IMAGE_DIR = DATA_DIR / "images"

DB_PATH = DATA_DIR / "telemetry.db"

MAX_IMAGE_SIZE = 256 * 1024


DATA_DIR.mkdir(
    parents=True,
    exist_ok=True
)

IMAGE_DIR.mkdir(
    parents=True,
    exist_ok=True
)


app = FastAPI(
    title="Secure Visual Gateway API",
    version="0.1.0"
)


app.add_middleware(
    CORSMiddleware,
    allow_origins=[
        "http://localhost:5173",
        "http://127.0.0.1:5173"
    ],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"]
)


def get_connection():
    connection = sqlite3.connect(
        DB_PATH
    )

    connection.row_factory = (
        sqlite3.Row
    )

    return connection


def initialize_database():
    with get_connection() as connection:
        connection.execute(
            """
            CREATE TABLE IF NOT EXISTS images
            (
                id INTEGER PRIMARY KEY AUTOINCREMENT,

                session_id TEXT NOT NULL,

                image_id INTEGER NOT NULL,

                width INTEGER NOT NULL,

                height INTEGER NOT NULL,

                size_bytes INTEGER NOT NULL,

                filename TEXT NOT NULL,

                received_at TEXT NOT NULL,

                UNIQUE(session_id, image_id)
            )
            """
        )

        connection.execute(
            """
            CREATE TABLE IF NOT EXISTS device_status
            (
                id INTEGER PRIMARY KEY AUTOINCREMENT,

                session_id TEXT NOT NULL,

                reset_reason INTEGER NOT NULL,

                uptime_ms INTEGER NOT NULL,

                free_heap INTEGER NOT NULL,

                min_free_heap INTEGER NOT NULL,

                images_sent INTEGER NOT NULL,

                capture_failures INTEGER NOT NULL,

                transfer_failures INTEGER NOT NULL,

                received_at TEXT NOT NULL
            )
            """
        )
        

        connection.commit()


def row_to_dict(row):
    return {
        "id": row["id"],
        "session_id": row["session_id"],
        "image_id": row["image_id"],
        "width": row["width"],
        "height": row["height"],
        "size_bytes": row["size_bytes"],
        "received_at": row["received_at"],
        "filename": row["filename"],
        "image_url": (
            f"/images/{row['filename']}"
        )
    }


initialize_database()


@app.get("/api/health")
def health():
    return {
        "status": "ok"
    }


@app.post("/api/images")
async def upload_image(
    request: Request
):
    session_id = (
        request.headers.get(
            "x-session-id"
        )
        or ""
    ).strip().upper()


    if not session_id:
        raise HTTPException(
            status_code=400,
            detail="Missing X-Session-Id"
        )


    try:
        image_id = int(
            request.headers[
                "x-image-id"
            ]
        )

        width = int(
            request.headers[
                "x-image-width"
            ]
        )

        height = int(
            request.headers[
                "x-image-height"
            ]
        )

    except (
        KeyError,
        ValueError
    ):
        raise HTTPException(
            status_code=400,
            detail="Invalid image metadata"
        )


    if (
        image_id <= 0
        or width <= 0
        or height <= 0
    ):
        raise HTTPException(
            status_code=400,
            detail="Invalid image metadata"
        )


    jpeg = await request.body()


    if (
        len(jpeg) < 4
        or len(jpeg) >
        MAX_IMAGE_SIZE
    ):
        raise HTTPException(
            status_code=400,
            detail="Invalid image size"
        )


    if (
        jpeg[0:2] != b"\xFF\xD8"
        or jpeg[-2:] != b"\xFF\xD9"
    ):
        raise HTTPException(
            status_code=400,
            detail="Invalid JPEG"
        )


    received_at = (
        datetime.now(
            timezone.utc
        ).isoformat()
    )


    timestamp = (
        datetime.now(
            timezone.utc
        ).strftime(
            "%Y%m%dT%H%M%S_%fZ"
        )
    )


    safe_session = (
        session_id
        .replace("0X", "")
    )


    filename = (
        f"{safe_session}_"
        f"{image_id:06d}_"
        f"{timestamp}.jpg"
    )


    image_path = (
        IMAGE_DIR /
        filename
    )


    image_path.write_bytes(
        jpeg
    )


    try:
        with get_connection() as connection:
            cursor = connection.execute(
                """
                INSERT INTO images
                (
                    session_id,
                    image_id,
                    width,
                    height,
                    size_bytes,
                    filename,
                    received_at
                )
                VALUES (?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    session_id,
                    image_id,
                    width,
                    height,
                    len(jpeg),
                    filename,
                    received_at
                )
            )

            connection.commit()

            database_id = (
                cursor.lastrowid
            )


    except sqlite3.IntegrityError:
        image_path.unlink(
            missing_ok=True
        )


        with get_connection() as connection:
            row = connection.execute(
                """
                SELECT *
                FROM images
                WHERE session_id = ?
                  AND image_id = ?
                """,
                (
                    session_id,
                    image_id
                )
            ).fetchone()


        if row is None:
            raise HTTPException(
                status_code=500,
                detail="Database error"
            )


        result = row_to_dict(
            row
        )


        result["status"] = (
            "duplicate"
        )


        return result


    with get_connection() as connection:
        row = connection.execute(
            """
            SELECT *
            FROM images
            WHERE id = ?
            """,
            (
                database_id,
            )
        ).fetchone()


    result = row_to_dict(
        row
    )


    result["status"] = "saved"


    return result


@app.get("/api/images")
def list_images(
    limit: int = 50
):
    limit = max(
        1,
        min(limit, 100)
    )


    with get_connection() as connection:
        rows = connection.execute(
            """
            SELECT *
            FROM images
            ORDER BY id DESC
            LIMIT ?
            """,
            (
                limit,
            )
        ).fetchall()


    return [
        row_to_dict(row)
        for row in rows
    ]


@app.get("/api/images/latest")
def latest_image():
    with get_connection() as connection:
        row = connection.execute(
            """
            SELECT *
            FROM images
            ORDER BY id DESC
            LIMIT 1
            """
        ).fetchone()


    if row is None:
        raise HTTPException(
            status_code=404,
            detail="No images available"
        )


    return row_to_dict(
        row
    )


app.mount(
    "/images",
    StaticFiles(
        directory=str(
            IMAGE_DIR
        )
    ),
    name="images"
)

@app.post("/api/status")
async def upload_status(
    request: Request
):
    data = await request.json()


    required = [
        "session_id",
        "reset_reason",
        "uptime_ms",
        "free_heap",
        "min_free_heap",
        "images_sent",
        "capture_failures",
        "transfer_failures"
    ]


    for key in required:
        if key not in data:
            raise HTTPException(
                status_code=400,
                detail=f"Missing {key}"
            )


    received_at = (
        datetime.now(
            timezone.utc
        ).isoformat()
    )


    with get_connection() as connection:
        connection.execute(
            """
            INSERT INTO device_status
            (
                session_id,
                reset_reason,
                uptime_ms,
                free_heap,
                min_free_heap,
                images_sent,
                capture_failures,
                transfer_failures,
                received_at
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                data["session_id"],
                data["reset_reason"],
                data["uptime_ms"],
                data["free_heap"],
                data["min_free_heap"],
                data["images_sent"],
                data["capture_failures"],
                data["transfer_failures"],
                received_at
            )
        )

        connection.commit()


    return {
        "status": "saved"
    }


@app.get("/api/status/latest")
def latest_status():
    with get_connection() as connection:
        row = connection.execute(
            """
            SELECT *
            FROM device_status
            ORDER BY id DESC
            LIMIT 1
            """
        ).fetchone()


    if row is None:
        raise HTTPException(
            status_code=404,
            detail="No status available"
        )


    return dict(row)