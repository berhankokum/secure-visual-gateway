import {
  useEffect,
  useState
} from "react";

import "./App.css";

import type {
  DeviceStatus,
  ImageRecord
} from "./types";


const API_BASE =
  "http://127.0.0.1:8000";


function formatBytes(
  bytes: number
): string {
  if (bytes < 1024) {
    return `${bytes} B`;
  }

  if (bytes < 1024 * 1024) {
    return `${
      (bytes / 1024).toFixed(1)
    } KB`;
  }

  return `${
    (
      bytes /
      (1024 * 1024)
    ).toFixed(2)
  } MB`;
}


function formatDate(
  value: string
): string {
  return new Date(
    value
  ).toLocaleString();
}


function formatUptime(
  milliseconds: number
): string {
  const totalSeconds =
    Math.floor(
      milliseconds / 1000
    );

  const hours =
    Math.floor(
      totalSeconds / 3600
    );

  const minutes =
    Math.floor(
      (
        totalSeconds % 3600
      ) / 60
    );

  const seconds =
    totalSeconds % 60;


  if (hours > 0) {
    return (
      `${hours}h ` +
      `${minutes}m ` +
      `${seconds}s`
    );
  }


  if (minutes > 0) {
    return (
      `${minutes}m ` +
      `${seconds}s`
    );
  }


  return `${seconds}s`;
}


function resetReasonName(
  reason: number
): string {
  switch (reason) {
    case 1:
      return "Power On";

    case 2:
      return "External Reset";

    case 3:
      return "Software Reset";

    case 4:
      return "Panic";

    case 5:
      return "Interrupt WDT";

    case 6:
      return "Task WDT";

    case 7:
      return "Watchdog";

    case 8:
      return "Deep Sleep";

    case 9:
      return "Brownout";

    default:
      return `Reason ${reason}`;
  }
}


function isCameraOnline(
  status: DeviceStatus | null
): boolean {
  if (status === null) {
    return false;
  }


  const received =
    new Date(
      status.received_at
    ).getTime();


  const age =
    Date.now() -
    received;


  /*
   * Telemetry yaklaşık her görüntüden
   * sonra geliyor.
   *
   * 30 saniyeden eski status'u offline
   * kabul ediyoruz.
   */
  return (
    age >= 0 &&
    age < 30000
  );
}


function App() {
  const [images, setImages] =
    useState<ImageRecord[]>([]);


  const [latest, setLatest] =
    useState<ImageRecord | null>(
      null
    );


  const [deviceStatus, setDeviceStatus] =
    useState<DeviceStatus | null>(
      null
    );


  const [
    backendOnline,
    setBackendOnline
  ] =
    useState(false);


  const [loading, setLoading] =
    useState(true);


  const fetchData =
    async () => {
      try {
        const healthResponse =
          await fetch(
            `${API_BASE}/api/health`
          );


        if (!healthResponse.ok) {
          throw new Error(
            "Backend health failed"
          );
        }


        setBackendOnline(
          true
        );


        const imagesResponse =
          await fetch(
            `${API_BASE}/api/images?limit=20`
          );


        if (!imagesResponse.ok) {
          throw new Error(
            "Images request failed"
          );
        }


        const imageData:
          ImageRecord[] =
            await imagesResponse.json();


        setImages(
          imageData
        );


        if (imageData.length > 0) {
          setLatest(
            imageData[0]
          );
        } else {
          setLatest(
            null
          );
        }


        const statusResponse =
          await fetch(
            `${API_BASE}/api/status/latest`
          );


        if (
          statusResponse.ok
        ) {
          const statusData:
            DeviceStatus =
              await statusResponse.json();


          setDeviceStatus(
            statusData
          );
        } else if (
          statusResponse.status === 404
        ) {
          setDeviceStatus(
            null
          );
        }


      } catch (error) {
        console.error(
          error
        );


        setBackendOnline(
          false
        );


      } finally {
        setLoading(
          false
        );
      }
    };


  useEffect(() => {
    fetchData();


    const timer =
      window.setInterval(
        fetchData,
        2000
      );


    return () => {
      window.clearInterval(
        timer
      );
    };
  }, []);


  const cameraOnline =
    isCameraOnline(
      deviceStatus
    );


  const totalFailures =
    deviceStatus
      ? (
          deviceStatus
            .capture_failures +
          deviceStatus
            .transfer_failures
        )
      : 0;


  return (
    <div className="app">

      <header className="header">

        <div>
          <p className="eyebrow">
            SECURE EMBEDDED TELEMETRY
          </p>

          <h1>
            Secure Visual Gateway
          </h1>
        </div>


        <div className="header-statuses">

          <div
            className={
              backendOnline
                ? "status online"
                : "status offline"
            }
          >
            <span
              className="status-dot"
            />

            {backendOnline
              ? "Backend Online"
              : "Backend Offline"}
          </div>


          <div
            className={
              cameraOnline
                ? "status online"
                : "status offline"
            }
          >
            <span
              className="status-dot"
            />

            {cameraOnline
              ? "Camera Online"
              : "Camera Offline"}
          </div>

        </div>

      </header>


      <main>

        <section className="overview">

          <article className="camera-panel">

            <div className="section-header">

              <div>
                <p className="label">
                  LIVE CAMERA
                </p>

                <h2>
                  Latest frame
                </h2>
              </div>


              {cameraOnline && (
                <span className="live-badge">
                  LIVE
                </span>
              )}

            </div>


            <div className="image-container">

              {loading ? (

                <div className="placeholder">
                  Loading...
                </div>

              ) : latest ? (

                <img
                  src={
                    `${API_BASE}${latest.image_url}`
                  }
                  alt={
                    "Latest ESP32-CAM frame"
                  }
                />

              ) : (

                <div className="placeholder">
                  Waiting for image...
                </div>

              )}

            </div>

          </article>


          <aside className="telemetry">

            <div className="section-header">

              <div>
                <p className="label">
                  TELEMETRY
                </p>

                <h2>
                  Current status
                </h2>
              </div>

            </div>


            <div className="telemetry-grid">

              <div className="metric">

                <span>
                  Session
                </span>

                <strong>
                  {deviceStatus
                    ? deviceStatus.session_id
                    : latest
                      ? latest.session_id
                      : "—"}
                </strong>

              </div>


              <div className="metric">

                <span>
                  Image ID
                </span>

                <strong>
                  {latest
                    ? latest.image_id
                    : "—"}
                </strong>

              </div>


              <div className="metric">

                <span>
                  Resolution
                </span>

                <strong>
                  {latest
                    ? (
                        `${latest.width} × ` +
                        `${latest.height}`
                      )
                    : "—"}
                </strong>

              </div>


              <div className="metric">

                <span>
                  JPEG Size
                </span>

                <strong>
                  {latest
                    ? formatBytes(
                        latest.size_bytes
                      )
                    : "—"}
                </strong>

              </div>


              <div className="metric">

                <span>
                  Uptime
                </span>

                <strong>
                  {deviceStatus
                    ? formatUptime(
                        deviceStatus
                          .uptime_ms
                      )
                    : "—"}
                </strong>

              </div>


              <div className="metric">

                <span>
                  Images Sent
                </span>

                <strong>
                  {deviceStatus
                    ? deviceStatus
                        .images_sent
                    : "—"}
                </strong>

              </div>


              <div className="metric">

                <span>
                  Free Heap
                </span>

                <strong>
                  {deviceStatus
                    ? formatBytes(
                        deviceStatus
                          .free_heap
                      )
                    : "—"}
                </strong>

              </div>


              <div className="metric">

                <span>
                  Min Free Heap
                </span>

                <strong>
                  {deviceStatus
                    ? formatBytes(
                        deviceStatus
                          .min_free_heap
                      )
                    : "—"}
                </strong>

              </div>


              <div className="metric">

                <span>
                  Reset Reason
                </span>

                <strong
                  className={
                    deviceStatus
                      ?.reset_reason === 9
                      ? "warning-text"
                      : ""
                  }
                >
                  {deviceStatus
                    ? resetReasonName(
                        deviceStatus
                          .reset_reason
                      )
                    : "—"}
                </strong>

              </div>


              <div className="metric">

                <span>
                  Total Errors
                </span>

                <strong
                  className={
                    totalFailures > 0
                      ? "error-text"
                      : "success-text"
                  }
                >
                  {deviceStatus
                    ? totalFailures
                    : "—"}
                </strong>

              </div>


              <div className="metric">

                <span>
                  Capture Failures
                </span>

                <strong>
                  {deviceStatus
                    ? deviceStatus
                        .capture_failures
                    : "—"}
                </strong>

              </div>


              <div className="metric">

                <span>
                  Transfer Failures
                </span>

                <strong>
                  {deviceStatus
                    ? deviceStatus
                        .transfer_failures
                    : "—"}
                </strong>

              </div>


              <div className="metric full">

                <span>
                  Last image received
                </span>

                <strong>
                  {latest
                    ? formatDate(
                        latest.received_at
                      )
                    : "—"}
                </strong>

              </div>


              <div className="metric full">

                <span>
                  Last telemetry
                </span>

                <strong>
                  {deviceStatus
                    ? formatDate(
                        deviceStatus
                          .received_at
                      )
                    : "—"}
                </strong>

              </div>

            </div>

          </aside>

        </section>


        <section className="history">

          <div className="section-header">

            <div>
              <p className="label">
                IMAGE HISTORY
              </p>

              <h2>
                Received frames
              </h2>
            </div>


            <span className="image-count">
              {images.length} frames
            </span>

          </div>


          <div className="image-grid">

            {images.map(
              (image) => (

                <article
                  className="image-card"
                  key={image.id}
                >

                  <img
                    src={
                      `${API_BASE}${image.image_url}`
                    }
                    alt={
                      `Frame ${image.image_id}`
                    }
                  />


                  <div className="image-card-body">

                    <div>

                      <strong>
                        Frame #{image.image_id}
                      </strong>

                      <span>
                        {formatDate(
                          image.received_at
                        )}
                      </span>

                    </div>


                    <div className="image-meta">

                      <span>
                        {image.width}
                        ×
                        {image.height}
                      </span>


                      <span>
                        {formatBytes(
                          image.size_bytes
                        )}
                      </span>

                    </div>


                    <code>
                      {image.session_id}
                    </code>

                  </div>

                </article>

              )
            )}

          </div>

        </section>

      </main>

    </div>
  );
}


export default App;