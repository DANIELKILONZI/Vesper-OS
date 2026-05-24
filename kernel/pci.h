#ifndef PCI_H
#define PCI_H

#include <stdint.h>

/*
 * VESPER OS – PCI configuration-space access
 *
 * Uses the standard I/O-port method (PCI 2.2 §3.2.4):
 *   Port 0xCF8 – CONFIG_ADDRESS: select the register to access
 *   Port 0xCFC – CONFIG_DATA:    read/write the 32-bit register value
 *
 * Address word format (bit positions in the 32-bit value):
 *   bit 31    : Enable bit (must be 1)
 *   bits 23:16: Bus number (0-255)
 *   bits 15:11: Device (slot) number (0-31)
 *   bits 10:8 : Function number (0-7)
 *   bits  7:2 : DWORD-aligned register offset
 *   bits  1:0 : Zero
 */

/* PCI configuration-space I/O ports */
#define PCI_CONFIG_ADDR  0xCF8u
#define PCI_CONFIG_DATA  0xCFCu

/* Standard PCI configuration-space register offsets */
#define PCI_VENDOR_ID      0x00u
#define PCI_DEVICE_ID      0x02u
#define PCI_COMMAND        0x04u
#define PCI_STATUS         0x06u
#define PCI_BAR0           0x10u
#define PCI_INTERRUPT_LINE 0x3Cu

/* PCI command register bits */
#define PCI_CMD_IO   0x0001u   /* I/O space enable  */
#define PCI_CMD_MEM  0x0002u   /* Memory space enable */
#define PCI_CMD_BME  0x0004u   /* Bus master enable */

uint32_t pci_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read_word (uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t  pci_read_byte (uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

void pci_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
void pci_write_word (uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);

/*
 * pci_find_device – scan PCI buses 0–3 for a device with the given vendor
 *                   and device IDs.
 *
 * Returns 1 if found; fills *out_bus, *out_slot, *out_func.
 * Returns 0 if no matching device is present.
 */
int pci_find_device(uint16_t vendor, uint16_t device,
                    uint8_t *out_bus, uint8_t *out_slot, uint8_t *out_func);

#endif /* PCI_H */
