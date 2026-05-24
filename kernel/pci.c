#include "pci.h"
#include "port_io.h"

/* -------------------------------------------------------------------------
 * Build the 32-bit CONFIG_ADDRESS value for a given (bus, slot, func, offset)
 * ---------------------------------------------------------------------- */
static uint32_t pci_address(uint8_t bus, uint8_t slot,
                             uint8_t func, uint8_t offset)
{
    return (1u << 31)
         | ((uint32_t)bus   << 16)
         | ((uint32_t)slot  << 11)
         | ((uint32_t)func  <<  8)
         | ((uint32_t)(offset & 0xFCu));   /* clear the bottom 2 bits */
}

/* -------------------------------------------------------------------------
 * Read / write helpers
 * ---------------------------------------------------------------------- */

uint32_t pci_read_dword(uint8_t bus, uint8_t slot,
                        uint8_t func, uint8_t offset)
{
    outl(PCI_CONFIG_ADDR, pci_address(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read_word(uint8_t bus, uint8_t slot,
                       uint8_t func, uint8_t offset)
{
    uint32_t dw = pci_read_dword(bus, slot, func, offset);
    return (uint16_t)(dw >> ((offset & 2u) * 8u));
}

uint8_t pci_read_byte(uint8_t bus, uint8_t slot,
                      uint8_t func, uint8_t offset)
{
    uint32_t dw = pci_read_dword(bus, slot, func, offset);
    return (uint8_t)(dw >> ((offset & 3u) * 8u));
}

void pci_write_dword(uint8_t bus, uint8_t slot,
                     uint8_t func, uint8_t offset, uint32_t val)
{
    outl(PCI_CONFIG_ADDR, pci_address(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, val);
}

void pci_write_word(uint8_t bus, uint8_t slot,
                    uint8_t func, uint8_t offset, uint16_t val)
{
    uint32_t dw    = pci_read_dword(bus, slot, func, offset);
    uint8_t  shift = (uint8_t)((offset & 2u) * 8u);
    dw = (dw & ~(0xFFFFu << shift)) | ((uint32_t)val << shift);
    pci_write_dword(bus, slot, func, offset, dw);
}

/* -------------------------------------------------------------------------
 * Bus scan
 * ---------------------------------------------------------------------- */

int pci_find_device(uint16_t vendor, uint16_t device,
                    uint8_t *out_bus, uint8_t *out_slot, uint8_t *out_func)
{
    for (uint16_t bus = 0; bus < 4u; bus++) {
        for (uint8_t slot = 0; slot < 32u; slot++) {
            for (uint8_t func = 0; func < 8u; func++) {
                uint32_t id = pci_read_dword((uint8_t)bus, slot, func,
                                             PCI_VENDOR_ID);
                /* 0xFFFFFFFF means no device at this slot */
                if (id == 0xFFFFFFFFu) {
                    continue;
                }
                if ((uint16_t)(id & 0xFFFFu)  == vendor &&
                    (uint16_t)(id >> 16u)      == device) {
                    *out_bus  = (uint8_t)bus;
                    *out_slot = slot;
                    *out_func = func;
                    return 1;
                }
            }
        }
    }
    return 0;
}
