#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>

/*
 * VESPER OS – Realtek RTL8139 Fast Ethernet NIC driver
 *
 * The RTL8139 (PCI vendor 0x10EC, device 0x8139) is the default NIC
 * emulated by QEMU with:
 *   -netdev user,id=net0 -device rtl8139,netdev=net0
 * or the legacy shorthand:
 *   -net nic,model=rtl8139 -net user
 *
 * Features implemented:
 *   - PCI discovery, I/O-BAR base extraction, bus-master enable
 *   - Hardware reset and MAC readout
 *   - 8 KB receive ring buffer (interrupt-driven, IRQ from PCI config)
 *   - Four static 1536-byte transmit buffers (round-robin, polled TX-OK)
 *   - Received frames forwarded to net_rx()
 */

/* PCI IDs for the RTL8139 */
#define RTL8139_VENDOR  0x10ECu
#define RTL8139_DEVICE  0x8139u

/*
 * rtl8139_init – probe PCI bus, reset and configure the card.
 * Returns 1 if the card was found and initialised, 0 otherwise.
 */
int rtl8139_init(void);

/*
 * rtl8139_send – transmit a complete Ethernet frame.
 *
 * @data: pointer to the frame (Ethernet header + payload, no CRC)
 * @len : frame length in bytes (must be ≤ 1514)
 *
 * The frame is padded to the 60-byte minimum if necessary.
 * Returns 0 on success, -1 on error.
 */
int rtl8139_send(const uint8_t *data, uint16_t len);

/*
 * rtl8139_get_mac – copy the 6-byte hardware MAC address into @mac.
 */
void rtl8139_get_mac(uint8_t mac[6]);

#endif /* RTL8139_H */
