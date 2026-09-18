#ifndef SGP_DEVICE_CONFIG_H
#define SGP_DEVICE_CONFIG_H


/*
 * ============================================================
 * Network
 * ============================================================
 */

#define SGP_ESPNOW_CHANNEL              1U


/*
 * ============================================================
 * Reliability
 * ============================================================
 */

#define SGP_ACK_TIMEOUT_MS              1000U

#define SGP_MAX_RETRY_COUNT             3U


/*
 * ============================================================
 * Camera
 * ============================================================
 */

#define SGP_IMAGE_INTERVAL_MS           10000U

#define SGP_SOFTWARE_JPEG_QUALITY       80U


/*
 * ============================================================
 * Device MAC Addresses
 * ============================================================
 *
 * Camera Node:
 * B0:CB:D8:C7:C2:08
 *
 * Gateway:
 * D4:D4:DA:F7:57:1C
 */

#define SGP_CAMERA_NODE_MAC_BYTES       \
{                                       \
    0xB0, 0xCB, 0xD8,                  \
    0xC7, 0xC2, 0x08                   \
}


#define SGP_GATEWAY_MAC_BYTES           \
{                                       \
    0xD4, 0xD4, 0xDA,                  \
    0xF7, 0x57, 0x1C                   \
}


#endif