import importlib
import sys

import pytest
from fastapi.testclient import TestClient


@pytest.fixture
def client(
    tmp_path,
    monkeypatch
):
    data_dir = (
        tmp_path /
        "backend-data"
    )


    monkeypatch.setenv(
        "SVG_DATA_DIR",
        str(data_dir)
    )


    if "backend.main" in sys.modules:
        del sys.modules[
            "backend.main"
        ]


    import backend.main as backend_main


    backend_main = importlib.reload(
        backend_main
    )


    with TestClient(
        backend_main.app
    ) as test_client:
        yield (
            test_client,
            data_dir
        )


def make_test_jpeg() -> bytes:
    """
    Backend şu anda JPEG SOI/EOI
    marker'larını doğruluyor.

    Bu payload test için gereken
    minimum kontratı sağlar.
    """

    return (
        b"\xFF\xD8"
        b"secure-visual-gateway-test"
        b"\xFF\xD9"
    )


def upload_image(
    client: TestClient,
    session_id: str = "0x12345678",
    image_id: int = 1
):
    jpeg = make_test_jpeg()


    response = client.post(
        "/api/images",
        content=jpeg,
        headers={
            "Content-Type":
                "image/jpeg",

            "X-Session-Id":
                session_id,

            "X-Image-Id":
                str(image_id),

            "X-Image-Width":
                "320",

            "X-Image-Height":
                "240"
        }
    )


    return response, jpeg


def test_health_endpoint(
    client
):
    test_client, _ = client


    response = test_client.get(
        "/api/health"
    )


    assert response.status_code == 200


    assert response.json() == {
        "status": "ok"
    }


def test_upload_image(
    client
):
    test_client, data_dir = client


    response, jpeg = upload_image(
        test_client
    )


    assert response.status_code == 200


    body = response.json()


    assert body["status"] == "saved"

    assert (
        body["session_id"] ==
        "0X12345678"
    )

    assert body["image_id"] == 1

    assert body["width"] == 320

    assert body["height"] == 240

    assert (
        body["size_bytes"] ==
        len(jpeg)
    )


    filename = body["filename"]


    image_path = (
        data_dir /
        "images" /
        filename
    )


    assert image_path.exists()


    assert (
        image_path.read_bytes() ==
        jpeg
    )


def test_latest_image(
    client
):
    test_client, _ = client


    upload_response, _ = upload_image(
        test_client,
        session_id="0xABCDEF01",
        image_id=7
    )


    assert (
        upload_response.status_code ==
        200
    )


    response = test_client.get(
        "/api/images/latest"
    )


    assert response.status_code == 200


    body = response.json()


    assert (
        body["session_id"] ==
        "0XABCDEF01"
    )

    assert body["image_id"] == 7

    assert body["width"] == 320

    assert body["height"] == 240


def test_image_list(
    client
):
    test_client, _ = client


    for image_id in range(
        1,
        4
    ):
        response, _ = upload_image(
            test_client,
            session_id="0x11111111",
            image_id=image_id
        )


        assert (
            response.status_code ==
            200
        )


    response = test_client.get(
        "/api/images?limit=10"
    )


    assert response.status_code == 200


    images = response.json()


    assert len(images) == 3


    assert (
        images[0]["image_id"] ==
        3
    )


    assert (
        images[1]["image_id"] ==
        2
    )


    assert (
        images[2]["image_id"] ==
        1
    )


def test_duplicate_image(
    client
):
    test_client, _ = client


    first, _ = upload_image(
        test_client,
        session_id="0x22222222",
        image_id=5
    )


    second, _ = upload_image(
        test_client,
        session_id="0x22222222",
        image_id=5
    )


    assert first.status_code == 200

    assert second.status_code == 200


    assert (
        first.json()["status"] ==
        "saved"
    )


    assert (
        second.json()["status"] ==
        "duplicate"
    )


    images_response = (
        test_client.get(
            "/api/images?limit=100"
        )
    )


    assert (
        images_response.status_code ==
        200
    )


    assert len(
        images_response.json()
    ) == 1


def test_invalid_jpeg_rejected(
    client
):
    test_client, _ = client


    response = test_client.post(
        "/api/images",
        content=b"not-a-jpeg",
        headers={
            "Content-Type":
                "image/jpeg",

            "X-Session-Id":
                "0x33333333",

            "X-Image-Id":
                "1",

            "X-Image-Width":
                "320",

            "X-Image-Height":
                "240"
        }
    )


    assert response.status_code == 400


def test_missing_image_metadata_rejected(
    client
):
    test_client, _ = client


    response = test_client.post(
        "/api/images",
        content=make_test_jpeg(),
        headers={
            "Content-Type":
                "image/jpeg"
        }
    )


    assert response.status_code == 400


def test_static_image_endpoint(
    client
):
    test_client, _ = client


    upload_response, jpeg = (
        upload_image(
            test_client,
            session_id="0x44444444",
            image_id=10
        )
    )


    body = upload_response.json()


    response = test_client.get(
        body["image_url"]
    )


    assert response.status_code == 200


    assert response.content == jpeg


def test_status_upload_and_latest(
    client
):
    test_client, _ = client


    payload = {
        "session_id":
            "0x55555555",

        "reset_reason":
            1,

        "uptime_ms":
            123456,

        "free_heap":
            4200000,

        "min_free_heap":
            4000000,

        "images_sent":
            12,

        "capture_failures":
            0,

        "transfer_failures":
            1
    }


    response = test_client.post(
        "/api/status",
        json=payload
    )


    assert response.status_code == 200


    assert response.json() == {
        "status": "saved"
    }


    latest_response = (
        test_client.get(
            "/api/status/latest"
        )
    )


    assert (
        latest_response.status_code ==
        200
    )


    body = latest_response.json()


    assert (
        body["session_id"] ==
        "0x55555555"
    )

    assert (
        body["reset_reason"] ==
        1
    )

    assert (
        body["uptime_ms"] ==
        123456
    )

    assert (
        body["images_sent"] ==
        12
    )

    assert (
        body["capture_failures"] ==
        0
    )

    assert (
        body["transfer_failures"] ==
        1
    )


def test_invalid_status_rejected(
    client
):
    test_client, _ = client


    response = test_client.post(
        "/api/status",
        json={
            "session_id":
                "0x66666666"
        }
    )


    assert response.status_code == 400