#include "ch32fun.h"
#include "rv003usb.h"
#include "ch32v003_GPIO_branchless.h"

#define REPORT_SIZE 4
#define REPORT_QUEUE_SIZE 8

static uint8_t usb_report[REPORT_SIZE];
static uint8_t control_report[REPORT_SIZE];
static uint8_t report_queue[REPORT_QUEUE_SIZE][REPORT_SIZE];
static uint8_t report_head;
static uint8_t report_count;

static const uint32_t gpio_table[] =
{
    GPIOv_from_PORT_PIN(GPIO_port_A, 1),
    GPIOv_from_PORT_PIN(GPIO_port_A, 2),
};

/**
 * @brief Retire acknowledged reports and retain unacknowledged data for retransmission.
 * @param e Input endpoint.
 */
static void retire_report(struct usb_endpoint* e)
{
    if (e->count == 0)
    {
        return;
    }
    e->count = 0;
    if (report_count == 0)
    {
        return;
    }
    report_head = (report_head + 1) % REPORT_QUEUE_SIZE;
    report_count--;
}

/**
 * @brief Initialize relay outputs and start USB.
 * @return Does not return during normal operation.
 */
int main(void)
{
    SystemInit();
    Delay_Ms(1); // Allow the host to detect USB disconnection after reset.
    GPIO_port_enable(GPIO_port_A);
    for (uint32_t i = 0; i < sizeof(gpio_table) / sizeof(gpio_table[0]); i++)
    {
        GPIO_digitalWrite_0(gpio_table[i]);
        GPIO_pinMode(gpio_table[i], GPIO_pinMode_O_pushPull, GPIO_Speed_2MHz);
    }
    usb_setup();

    while (1)
    {
    }
}

/**
 * @brief Send queued relay status through the interrupt IN endpoint.
 * @param e Current endpoint.
 * @param scratchpad USB stack scratch buffer.
 * @param endp Endpoint number.
 * @param sendtok Data packet token.
 * @param ist USB stack state.
 */
void usb_handle_user_in_request(struct usb_endpoint* e, uint8_t* scratchpad, int endp, uint32_t sendtok,
                                struct rv003usb_internal* ist)
{
    if (endp != 1)
    {
        usb_send_empty(sendtok);
        return;
    }
    retire_report(e);
    if (report_count == 0)
    {
        usb_send_data(NULL, 0, 2, 0x5A); // Send NAK when no report is pending.
        return;
    }
    usb_send_data(report_queue[report_head], REPORT_SIZE, 0, sendtok);
}

/**
 * @brief Validate and execute a four-byte command, queuing feedback when requested.
 * @param e Current endpoint.
 * @param current_endpoint Receiving endpoint number.
 * @param data Command data.
 * @param len Data length in bytes.
 * @param ist USB stack state.
 */
void usb_handle_user_data(struct usb_endpoint* e, int current_endpoint, uint8_t* data, int len,
                          struct rv003usb_internal* ist)
{
    if (current_endpoint != 0 && current_endpoint != 2)
    {
        return;
    }
    if (current_endpoint == 0 && e->max_len != REPORT_SIZE)
    {
        return;
    }
    if (len != REPORT_SIZE || data[0] != 0xA0)
    {
        return;
    }
    if (data[3] != (uint8_t)(data[0] + data[1] + data[2]))
    {
        return;
    }
    if (data[1] == 0 || data[1] > sizeof(gpio_table) / sizeof(gpio_table[0]) || data[2] > 0x05)
    {
        return;
    }

    retire_report(&ist->eps[1]);
    if (data[2] >= 0x02 && report_count == REPORT_QUEUE_SIZE)
    {
        return; // Ignore feedback commands when full; the host should read each response before sending another.
    }
    uint32_t pin = gpio_table[data[1] - 1];
    switch (data[2])
    {
    case 0x00:
    case 0x02:
        GPIO_digitalWrite_0(pin);
        break;
    case 0x01:
    case 0x03:
        GPIO_digitalWrite_1(pin);
        break;
    case 0x04:
        if (GPIO_digitalRead(pin) != 0)
        {
            GPIO_digitalWrite_0(pin);
        }
        else
        {
            GPIO_digitalWrite_1(pin);
        }
        break;
    case 0x05:
        break;
    }

    usb_report[0] = 0xA0;
    usb_report[1] = data[1];
    usb_report[2] = (uint8_t)GPIO_digitalRead(pin);
    usb_report[3] = (uint8_t)(usb_report[0] + usb_report[1] + usb_report[2]);
    if (data[2] < 0x02)
    {
        return;
    }
    uint8_t tail = (report_head + report_count) % REPORT_QUEUE_SIZE;
    for (uint32_t i = 0; i < REPORT_SIZE; i++)
    {
        report_queue[tail][i] = usb_report[i];
    }
    report_count++;
}

/**
 * @brief Return a bounded status snapshot of the most recent valid command.
 * @param e Control endpoint.
 * @param reqLen Number of bytes requested by the host.
 * @param lValueLSBIndexMSB Report type, report ID, and interface number.
 */
void usb_handle_hid_get_report_start(struct usb_endpoint* e, int reqLen, uint32_t lValueLSBIndexMSB)
{
    e->max_len = 0;
    if (lValueLSBIndexMSB != 0x0100 && lValueLSBIndexMSB != 0x0300)
    {
        return;
    }
    for (uint32_t i = 0; i < REPORT_SIZE; i++)
    {
        control_report[i] = usb_report[i];
    }
    e->opaque = control_report;
    if (reqLen > REPORT_SIZE)
    {
        reqLen = REPORT_SIZE;
    }
    e->max_len = reqLen;
}

/**
 * @brief Accept Output or Feature reports sent through the control endpoint.
 * @param e Control endpoint.
 * @param reqLen Number of bytes sent by the host.
 * @param lValueLSBIndexMSB Report type, report ID, and interface number.
 */
void usb_handle_hid_set_report_start(struct usb_endpoint* e, int reqLen, uint32_t lValueLSBIndexMSB)
{
    e->max_len = 0;
    if (lValueLSBIndexMSB != 0x0200 && lValueLSBIndexMSB != 0x0300)
    {
        return;
    }
    if (reqLen != REPORT_SIZE)
    {
        return;
    }
    e->max_len = REPORT_SIZE;
}
