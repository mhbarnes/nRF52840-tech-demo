/*******************************************************************************
 * Project: nRF52840 Tech Demo (Server)
 * File: main.c
 * Author: Michael Barnes
 * Last Modified: 12/10/20
 * Description: Basic communication test for the nRF52840. Can be connected to
 * a separate dongle (via nRF connect) to control the on-board LED.
*******************************************************************************/

/***************************************
 * Libraries/Modules
***************************************/
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "ble_advdata.h"
#include "ble_conn_params.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"

#include "boards.h"
#include "app_timer.h"
#include "app_button.h"


/***************************************
 * Definitions/Constants
***************************************/
// BLE priority value
#define APP_BLE_OBSERVER_PRIO 3
// Config tag
#define APP_BLE_CONN_CFG_TAG 1
// GAP details
#define DEVICE_NAME "nRF52840_TechDemo"
#define MIN_CONN_INTERVAL MSEC_TO_UNITS(100, UNIT_1_25_MS)
#define MAX_CONN_INTERVAL MSEC_TO_UNITS(200, UNIT_1_25_MS)
#define SLAVE_LATENCY 0
#define CONN_SUP_TIMEOUT MSEC_TO_UNITS(4000, UNIT_10_MS)
// Advertising constants
#define APP_ADV_INTERVAL 64
#define APP_ADV_DURATION BLE_GAP_ADV_TIMEOUT_GENERAL_UNLIMITED
//Connection parameters
#define FIRST_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(20000)
#define NEXT_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(5000)
#define MAX_CONN_PARAMS_UPDATE_COUNT 3
// UUID
#define UUID_BASE {0x23, 0xD1, 0xBC, 0xEA, 0x5F, 0x78, 0x23, 0x15, \
                   0xDE, 0xEF, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00}
#define UUID_SERVICE 0x1234
#define UUID_BUTTON_CHAR 0x1234

NRF_BLE_GATT_DEF(m_gatt);
NRF_BLE_QWR_DEF(m_qwr);

//Current connection handle
static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;
// Advertising handle
static uint8_t m_adv_handle = BLE_GAP_ADV_SET_HANDLE_NOT_SET;
// Advertising data buffer
static uint8_t m_enc_advdata[BLE_GAP_ADV_SET_DATA_SIZE_MAX];
// Scan data buffer
static uint8_t m_enc_scan_response_data[BLE_GAP_ADV_SET_DATA_SIZE_MAX];
// Advertising data
static ble_gap_adv_data_t m_adv_data = {
    .adv_data = {
        .p_data = m_enc_advdata,
        .len = BLE_GAP_ADV_SET_DATA_SIZE_MAX
    },
    .scan_rsp_data = {
        .p_data = m_enc_scan_response_data,
        .len = BLE_GAP_ADV_SET_DATA_SIZE_MAX
    }
};

ble_gatts_char_handles_t button_char_handles;

/****************************************************************
 * Function: gap_params_init()
 * Description: Sets up all the necessary GAP 
 *  (Generic Access Profile) parameters of the device (device
 *  name, appearance, and preferred connection settings).
****************************************************************/
static void gap_params_init() {
    ble_gap_conn_params_t gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
    sd_ble_gap_device_name_set(
        &sec_mode, 
        (const uint8_t *)DEVICE_NAME, 
        strlen(DEVICE_NAME)
    );
    memset(&gap_conn_params, 0, sizeof(gap_conn_params));
    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.slave_latency = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout = CONN_SUP_TIMEOUT;
    sd_ble_gap_ppcp_set(&gap_conn_params);
}

// NOTE: error handler blank since no error handling needs to be done for this demo
static void nrf_qwr_error_handler(uint32_t nrf_error) {
    // Ignore errors
}


/****************************************************************
 * Function: services_init()
 * Description: Initializes services that will be used by the 
 *  program.
****************************************************************/
static void services_init() {
    nrf_ble_qwr_init_t qwr_init = {0};
    qwr_init.error_handler = nrf_qwr_error_handler;
    nrf_ble_qwr_init(&m_qwr, &qwr_init);
}


/****************************************************************
 * Function: services_init()
 * Description: Encodes the required advertising data and 
 *  passes it to the stack. Also builds a structure to be passed
 *  to the stack when starting advertising.
****************************************************************/
static void advertising_init() {
    // Add service
    ble_uuid_t ble_uuid;
    ble_add_char_params_t add_char_params;
    ble_uuid128_t base_uuid = {UUID_BASE};
    uint8_t uuid_type;
    sd_ble_uuid_vs_add(&base_uuid, &uuid_type);
    ble_uuid.type = uuid_type;
    ble_uuid.uuid = UUID_SERVICE;
    uint16_t service_handle;
    sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &ble_uuid, &service_handle);

    // Add button characteristic
    memset(&add_char_params, 0, sizeof(add_char_params));
    add_char_params.uuid                = UUID_BUTTON_CHAR;
    add_char_params.uuid_type           = uuid_type;
    add_char_params.init_len            = sizeof(uint8_t);
    add_char_params.max_len             = sizeof(uint8_t);
    add_char_params.char_props.read     = 1;
    add_char_params.char_props.notify   = 1;
    add_char_params.read_access         = SEC_OPEN;
    add_char_params.cccd_write_access   = SEC_OPEN;
    characteristic_add(service_handle, &add_char_params, &button_char_handles);
    
    // Build and set advertising data
    ble_advdata_t advdata, srdata;
    ble_uuid_t adv_uuids[] = {{UUID_SERVICE, uuid_type}};
    memset(&advdata, 0, sizeof(advdata));
    advdata.name_type = BLE_ADVDATA_FULL_NAME;
    advdata.include_appearance = true;
    advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
    memset(&srdata, 0, sizeof(srdata));
    srdata.uuids_complete.uuid_cnt  = sizeof(adv_uuids) / sizeof(adv_uuids[0]);
    srdata.uuids_complete.p_uuids   = adv_uuids;
    ble_advdata_encode(&advdata, m_adv_data.adv_data.p_data, &m_adv_data.adv_data.len);
    ble_advdata_encode(&srdata, m_adv_data.scan_rsp_data.p_data, &m_adv_data.scan_rsp_data.len);
    ble_gap_adv_params_t adv_params;
    memset(&adv_params, 0, sizeof(adv_params));

    // Initialize advertising parameters
    adv_params.primary_phy      = BLE_GAP_PHY_1MBPS;
    adv_params.duration         = APP_ADV_DURATION;
    adv_params.properties.type  = BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED;
    adv_params.p_peer_addr      = NULL;
    adv_params.filter_policy    = BLE_GAP_ADV_FP_ANY;
    adv_params.interval         = APP_ADV_INTERVAL;
    sd_ble_gap_adv_set_configure(&m_adv_handle, &m_adv_data, &adv_params);
}


/****************************************************************
 * Function: conn_params_init()
 * Description: Initializes the Connection Parameters module.
 * 
 *  From ble_conn_params.h:
 *      Module for initiating and executing a connection
 *      parameters negotiation procedure.
****************************************************************/
static void conn_params_init() {
    ble_conn_params_init_t params;
    memset(&params, 0, sizeof(params));
    params.p_conn_params = NULL;
    params.first_conn_params_update_delay   = FIRST_CONN_PARAMS_UPDATE_DELAY;
    params.next_conn_params_update_delay    = NEXT_CONN_PARAMS_UPDATE_DELAY;
    params.max_conn_params_update_count     = MAX_CONN_PARAMS_UPDATE_COUNT;
    params.start_on_notify_cccd_handle      = BLE_GATT_HANDLE_INVALID;
    params.disconnect_on_fail               = true;
    ble_conn_params_init(&params);
}


/****************************************************************
 * Function: advertising_start()
 * Description: Begins BLE advertising.
****************************************************************/
static void advertising_start() {
    sd_ble_gap_adv_start(m_adv_handle, APP_BLE_CONN_CFG_TAG);
    bsp_board_led_on(BSP_BOARD_LED_2);
}


/****************************************************************
 * Function: send_button()
 * Description: Sends the button state to the connected board or
 *  BLE peripheral (server)
****************************************************************/
void send_button(uint8_t button_state) {
    ble_gatts_hvx_params_t params;
    uint16_t len = sizeof(button_state);
    memset(&params, 0, sizeof(params));
    params.type = BLE_GATT_HVX_NOTIFICATION;
    params.handle = button_char_handles.value_handle;
    params.p_data = &button_state;
    params.p_len = &len;
    sd_ble_gatts_hvx(m_conn_handle, &params);
}


/****************************************************************
 * Function: button_handler()
 * Description: Processes the button state of the client board
****************************************************************/
static void button_handler(uint8_t pin, uint8_t action) {
    if (pin == BSP_BOARD_BUTTON_0) {
        if (action == APP_BUTTON_PUSH) {
            bsp_board_led_on(BSP_BOARD_LED_1);
        }
        else if (action == APP_BUTTON_RELEASE) {
            bsp_board_led_off(BSP_BOARD_LED_1);
        }
        send_button(action);
    }
} 


/****************************************************************
 * Function: ble_evt_handler()
 * Description: Function to process BLE events.
 *  BLE_GAP_EVT_CONNECTED    - Connected to peer
 *  BLE_GAP_EVT_DISCONNECTED - Disconnected from peer
****************************************************************/
static void ble_evt_handler(ble_evt_t const* p_ble_evt, void* p_context) {
    switch (p_ble_evt->header.evt_id) {
        case BLE_GAP_EVT_CONNECTED:
            bsp_board_led_off(BSP_BOARD_LED_2);
            bsp_board_led_on(BSP_BOARD_LED_3);
            m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            nrf_ble_qwr_conn_handle_assign(&m_qwr, m_conn_handle);
            break;
        case BLE_GAP_EVT_DISCONNECTED:
            bsp_board_led_off(BSP_BOARD_LED_3);
            m_conn_handle = BLE_CONN_HANDLE_INVALID;
            advertising_start();
            break;
    }
}


/****************************************************************
 * MAIN
****************************************************************/
int main() {
    // Initializations
    bsp_board_init(BSP_INIT_LEDS);
    app_timer_init();
    nrf_sdh_enable_request();

    static app_button_cfg_t buttons[] = {
        {BSP_BOARD_BUTTON_0, false, BUTTON_PULL, button_handler}
    };
    app_button_init(buttons, ARRAY_SIZE(buttons), APP_TIMER_TICKS(50));
    app_button_enable();

    // Fetch start address of application RAM
    uint32_t ram_start = 0;
    nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    // Enable BLE stack
    nrf_sdh_ble_enable(&ram_start);
    // Register handler for BLE events
    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);

    // Set up for advertising
    gap_params_init();
    nrf_ble_gatt_init(&m_gatt, NULL);
    services_init();
    advertising_init();
    conn_params_init();
    // Begin advertising
    advertising_start();
}