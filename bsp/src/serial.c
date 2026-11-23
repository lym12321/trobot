#include "bsp/serial.h"
#include "bsp/uart.h"
#include "cdc.h"

bsp_status_t bsp_serial_send_async(bsp_serial_t port, const uint8_t *data, size_t len) {
    if ((unsigned) port < BSP_UART_DEVICE_COUNT) return bsp_uart_send_async(port, data, len);
    if ((unsigned) port < BSP_SERIAL_DEVICE_COUNT) return bsp_cdc_send(port - E_USB_CDC_0, data, len);
    return BSP_STATUS_ERROR;
}

bsp_status_t bsp_serial_set_callback(bsp_serial_t port, bsp_serial_callback_t callback) {
    if ((unsigned) port < BSP_UART_DEVICE_COUNT) return bsp_uart_set_callback(port, callback);
    if ((unsigned) port < BSP_SERIAL_DEVICE_COUNT) return bsp_cdc_set_callback(port - E_USB_CDC_0, callback);
    return BSP_STATUS_ERROR;
}

uint32_t bsp_serial_session(bsp_serial_t port) {
    if ((unsigned) port >= E_USB_CDC_0 && (unsigned) port < BSP_SERIAL_DEVICE_COUNT)
        return bsp_cdc_session(port - E_USB_CDC_0);
    return 0;
}
