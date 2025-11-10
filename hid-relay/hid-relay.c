#include "ch32fun.h"
#include <stdio.h>
#include <string.h>
#include "rv003usb.h"
#include "ch32v003_GPIO_branchless.h"

static uint8_t usb_data[4];
static uint8_t usb_report[4];

int main()
{
    SystemInit();
    Delay_Ms(1); // Ensures USB re-enumeration after bootloader or reset; Spec demand >2.5µs ( TDDIS )
    usb_setup();
    GPIO_port_enable(GPIO_port_A);
    GPIO_pinMode(GPIOv_from_PORT_PIN(GPIO_port_A, 1), GPIO_pinMode_O_pushPull, GPIO_Speed_2MHz);
    GPIO_pinMode(GPIOv_from_PORT_PIN(GPIO_port_A, 2), GPIO_pinMode_O_pushPull, GPIO_Speed_2MHz);

    while (1)
    {
    }
}

void usb_handle_user_in_request(struct usb_endpoint* e, uint8_t* scratchpad, int endp, uint32_t sendtok,
                                struct rv003usb_internal* ist)
{
    if (endp == 1)
    {
        usb_send_data(usb_report, sizeof(usb_report), 0, sendtok);
    }
    else
    {
        usb_send_empty(sendtok);
    }
}

void usb_handle_user_data(struct usb_endpoint* e, int current_endpoint, uint8_t* data, int len,
                          struct rv003usb_internal* ist)
{
    if (data[0] != 0xA0)
    {
        return;
    }

    if (data[1] != 0 && data[1] != 1)
    {
        return;
    }

    if (data[2] > 0x05)
    {
        return;
    }

    if (data[3] != (data[0] + data[1] + data[2]) % 0x100)
    {
        return;
    }

    data[1] += 1;

    switch (data[2])
    {
    case 0x00:
        /* Turn off without feedback */
        GPIO_digitalWrite_0(GPIOv_from_PORT_PIN(GPIO_port_A, data[1]));
        return;

    case 0x01:
        /* Turn on without feedback */
        GPIO_digitalWrite_1(GPIOv_from_PORT_PIN(GPIO_port_A, data[1]));
        return;

    case 0x02:
        /* Turn off with feedback */
        GPIO_digitalWrite_0(GPIOv_from_PORT_PIN(GPIO_port_A, data[1]));
        break;

    case 0x03:
        /* Turn on with feedback */
        GPIO_digitalWrite_1(GPIOv_from_PORT_PIN(GPIO_port_A, data[1]));
        break;

    case 0x04:
        /* Toggle Pin */
        if (GPIO_digitalRead(GPIOv_from_PORT_PIN(GPIO_port_A, data[1])))
        {
            GPIO_digitalWrite_0(GPIOv_from_PORT_PIN(GPIO_port_A, data[1]));
        }
        else
        {
            GPIO_digitalWrite_1(GPIOv_from_PORT_PIN(GPIO_port_A, data[1]));
        }
        break;

    case 0x05:
        /* Get Pin status */
        break;

    default:
        return;
    }

    usb_report[0] = 0xA0;
    usb_report[1] = data[1] - 1;
    usb_report[2] = GPIO_digitalRead(GPIOv_from_PORT_PIN(GPIO_port_A, data[1]));
    usb_report[3] = (usb_report[0] + usb_report[1] + usb_report[2]) % 0x100;
}

void usb_handle_hid_get_report_start(struct usb_endpoint* e, int reqLen, uint32_t lValueLSBIndexMSB)
{
    // You can check the lValueLSBIndexMSB word to decide what you want to do here
    // But, whatever you point this at will be returned back to the host PC where
    // it calls hid_get_feature_report.
    //
    // Please note, that on some systems, for this to work, your return length must
    // match the length defined in HID_REPORT_COUNT, in your HID report, in usb_config.h
    e->opaque = usb_data;
    e->max_len = reqLen;
}

void usb_handle_hid_set_report_start(struct usb_endpoint* e, int reqLen, uint32_t lValueLSBIndexMSB)
{
    // Here is where you get an alert when the host PC calls hid_send_feature_report.
    //
    // You can handle the appropriate message here.  Please note that in this
    // example, the data is chunked into groups-of-8-bytes.
    //
    // Note that you may need to make this match HID_REPORT_COUNT, in your HID
    // report, in usb_config.h
    if (reqLen > sizeof(usb_data))
    {
        reqLen = sizeof(usb_data);
    }
    e->max_len = reqLen;
}
