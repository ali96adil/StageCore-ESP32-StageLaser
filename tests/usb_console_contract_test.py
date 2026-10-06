from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class USBConsoleContract(unittest.TestCase):
    def test_native_usb_serial_jtag_is_primary_console(self):
        sdk = (ROOT / "sdkconfig.defaults").read_text()
        self.assertIn("CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y", sdk)
        self.assertIn("CONFIG_ESP_CONSOLE_SECONDARY_NONE=y", sdk)

    def test_first_boot_docs_use_native_usb_monitor(self):
        docs = (ROOT / "docs" / "FIRST_BOOT_QUALIFICATION.md").read_text()
        self.assertIn("/dev/cu.usbmodem*", docs)
        self.assertIn("USB Serial/JTAG", docs)


if __name__ == "__main__":
    unittest.main()
