#ifndef GALAGINO_BTSTACK_CONFIG_H
#define GALAGINO_BTSTACK_CONFIG_H

// BTstack for Bluepad32 on the Pico 2 W: one classic HID pad. Counts cut to
// that (Bluepad32's pico_w example reserves 4 pads and audio/serial
// profiles: 40 KB more RAM). BLE stays compiled because hci.c needs it, but
// Bluepad32 runs with it off (uni_config.h).

#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP
#define ENABLE_CLASSIC
#define ENABLE_LE_CENTRAL
#define ENABLE_LE_PERIPHERAL
#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE

#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (1691 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4

#define MAX_NR_HCI_CONNECTIONS 1
#define MAX_NR_HID_HOST_CONNECTIONS 1
#define MAX_NR_L2CAP_CHANNELS 4
#define MAX_NR_L2CAP_SERVICES 3
#define MAX_NR_SERVICE_RECORD_ITEMS 1
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 1
#define MAX_NR_SM_LOOKUP_ENTRIES 1
#define MAX_NR_LE_DEVICE_DB_ENTRIES 1
#define MAX_NR_WHITELIST_ENTRIES 1
#define MAX_NR_GATT_CLIENTS 0
#define MAX_NR_HIDS_CLIENTS 0
#define MAX_NR_AVDTP_CONNECTIONS 0
#define MAX_NR_AVDTP_STREAM_ENDPOINTS 0
#define MAX_NR_AVRCP_CONNECTIONS 0
#define MAX_NR_BNEP_CHANNELS 0
#define MAX_NR_BNEP_SERVICES 0
#define MAX_NR_HFP_CONNECTIONS 0
#define MAX_NR_RFCOMM_CHANNELS 0
#define MAX_NR_RFCOMM_MULTIPLEXERS 0
#define MAX_NR_RFCOMM_SERVICES 0

// few ACL buffers and controller-to-host flow control: the CYW43 shares
// its bus between WiFi and BT and overruns otherwise
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN 1024
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 3

// link keys in the flash bank (TLV), a few pads remembered
#define NVM_NUM_DEVICE_DB_ENTRIES 4
#define NVM_NUM_LINK_KEYS 4

#define MAX_ATT_DB_SIZE 512
#define HAVE_EMBEDDED_TIME_MS
#define HAVE_ASSERT
#define HCI_RESET_RESEND_TIMEOUT_MS 1000

#endif // GALAGINO_BTSTACK_CONFIG_H
