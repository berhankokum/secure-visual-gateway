export interface ImageRecord {
  id: number;
  session_id: string;
  image_id: number;

  width: number;
  height: number;

  size_bytes: number;

  received_at: string;

  filename: string;
  image_url: string;
}


export interface DeviceStatus {
  id: number;

  session_id: string;

  reset_reason: number;

  uptime_ms: number;

  free_heap: number;
  min_free_heap: number;

  images_sent: number;

  capture_failures: number;
  transfer_failures: number;

  received_at: string;
}