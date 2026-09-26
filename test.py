# ========================================================================== #
#                                                                            #
#    KVMD - The main PiKVM daemon.                                           #
#                                                                            #
#    Copyright (C) 2023-2025  SilentWind <mofeng654321@hotmail.com>         #
#                                                                            #
#    This program is free software: you can redistribute it and/or modify    #
#    it under the terms of the GNU General Public License as published by    #
#    the Free Software Foundation, either version 3 of the License, or       #
#    (at your option) any later version.                                     #
#                                                                            #
#    This program is distributed in the hope that it will be useful,         #
#    but WITHOUT ANY WARRANTY; without even the implied warranty of          #
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           #
#    GNU General Public License for more details.                            #
#                                                                            #
#    You should have received a copy of the GNU General Public License       #
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.  #
#                                                                            #
# ========================================================================== #

import argparse
import hid

VENDOR_ID = 0x5131
PRODUCT_ID = 0x2007
COMMANDS = {"off": 0x00, "on": 0x01, "off-feedback": 0x02,
            "on-feedback": 0x03, "toggle": 0x04, "status": 0x05}


def find_usbrelay():
    for device in hid.enumerate(VENDOR_ID, PRODUCT_ID):
        return device
    return None


def send_command(device_info, channel, command):
    device = hid.device()
    device.open_path(device_info["path"])
    try:
        payload = [0xA0, channel, command, (0xA0 + channel + command) & 0xFF]
        # Prefix unnumbered reports with ID 0 for HIDAPI; the USB payload is four bytes.
        if device.write(bytearray([0] + payload)) != 5:
            raise RuntimeError("Failed to write the complete command")
        if command < 0x02:
            return None
        report = device.read(4, 1000)
        if len(report) != 4:
            raise RuntimeError("Timed out waiting for a four-byte status report")
        if (report[0] != 0xA0 or report[1] != channel or report[2] not in (0, 1)
                or report[3] != (sum(report[:3]) & 0xFF)):
            raise RuntimeError(f"Invalid status report: {report}")
        return report[2]
    finally:
        device.close()


def main():
    parser = argparse.ArgumentParser(description="Control a two-channel USB HID relay")
    parser.add_argument("channel", type=int, choices=(1, 2))
    parser.add_argument("command", choices=COMMANDS)
    args = parser.parse_args()
    device_info = find_usbrelay()
    if device_info is None:
        parser.exit(1, "USB relay not found\n")
    try:
        state = send_command(device_info, args.channel, COMMANDS[args.command])
    except (OSError, RuntimeError) as error:
        parser.exit(1, f"{error}\n")
    if state is None:
        print(f"Sent {args.command} to channel {args.channel}")
    else:
        print(f"Channel {args.channel}: {'ON' if state == 1 else 'OFF'}")


if __name__ == "__main__":
    main()
