#include "rtl8139.h"
#include "pci.h"
#include "port_io.h"
#include "isr.h"
#include "pic.h"
#include "string.h"
#include "net.h"

/* -------------------------------------------------------------------------
 * RTL8139 register offsets (relative to the I/O base from BAR0)
 * ---------------------------------------------------------------------- */
#define RTL_IDR0     0x00u   /* MAC address bytes 0-5 (read byte-by-byte) */
#define RTL_MAR0     0x08u   /* Multicast filter (8 bytes)                */
#define RTL_TSD0     0x10u   /* TX Status Descriptor 0  (4 bytes each)    */
#define RTL_TSAD0    0x20u   /* TX Start Address 0      (4 bytes each)    */
#define RTL_RBSTART  0x30u   /* RX Buffer Start Address (32-bit phys.)    */
#define RTL_CMD      0x37u   /* Command register (1 byte)                 */
#define RTL_CAPR     0x38u   /* Current Address of Packet Read (16-bit)   */
#define RTL_CBR      0x3Au   /* Current Buffer Address written by NIC     */
#define RTL_IMR      0x3Cu   /* Interrupt Mask Register (16-bit)          */
#define RTL_ISR      0x3Eu   /* Interrupt Status Register (16-bit)        */
#define RTL_TCR      0x40u   /* TX Configuration Register (32-bit)        */
#define RTL_RCR      0x44u   /* RX Configuration Register (32-bit)        */
#define RTL_CONFIG1  0x52u   /* Configuration Register 1 (1 byte)         */

/* CMD register bits */
#define CMD_RST  0x10u   /* Software Reset – self-clears when done */
#define CMD_RE   0x08u   /* Receive Enable                          */
#define CMD_TE   0x04u   /* Transmit Enable                         */
#define CMD_BUFE 0x01u   /* Buffer Empty flag (RX ring empty)       */

/* ISR / IMR bits */
#define ISR_ROK  0x0001u   /* Receive OK     */
#define ISR_RER  0x0002u   /* Receive Error  */
#define ISR_TOK  0x0004u   /* Transmit OK    */
#define ISR_TER  0x0008u   /* Transmit Error */
#define ISR_SERR 0x8000u   /* System Error   */

/* TSD bits */
#define TSD_OWN  (1u << 13)   /* 0 = NIC owns buffer / transmitting */
#define TSD_TOK  (1u << 15)   /* Transmit OK (set by NIC on success) */

/* -------------------------------------------------------------------------
 * Receive ring buffer
 *
 * Size: 8 KB ring + 16-byte header guard + 1500 bytes overflow space.
 * The extra space prevents the NIC from wrapping mid-packet and ensures
 * a complete packet can always be read at a linear address even if the
 * packet starts near the 8 KB boundary.
 * ---------------------------------------------------------------------- */
#define RX_BUF_SIZE   8192u
#define RX_BUF_GUARD  (16u + 1500u)

static uint8_t rx_buf[RX_BUF_SIZE + RX_BUF_GUARD] __attribute__((aligned(4)));

/* -------------------------------------------------------------------------
 * Transmit buffers – four static 1536-byte slots, used round-robin
 * ---------------------------------------------------------------------- */
#define TX_BUF_SIZE  1536u
#define TX_BUF_COUNT 4u

static uint8_t tx_buf[TX_BUF_COUNT][TX_BUF_SIZE] __attribute__((aligned(4)));

/* -------------------------------------------------------------------------
 * Driver state
 * ---------------------------------------------------------------------- */
static uint16_t iobase;          /* I/O port base (from BAR0)          */
static uint8_t  nic_mac[6];      /* NIC hardware MAC address            */
static uint8_t  irq_line;        /* PCI interrupt line                  */
static uint8_t  tx_cur;          /* Next TX descriptor index (0-3)      */
static uint32_t rx_ptr;          /* Software read pointer into rx_buf   */

/* -------------------------------------------------------------------------
 * Register access helpers
 * ---------------------------------------------------------------------- */
static inline uint8_t  rtl_rb(uint8_t  r) { return inb (iobase + r); }
static inline uint16_t rtl_rw(uint8_t  r) { return inw (iobase + r); }
static inline uint32_t rtl_rl(uint8_t  r) { return inl (iobase + r); }
static inline void rtl_wb(uint8_t r, uint8_t  v) { outb(iobase + r, v); }
static inline void rtl_ww(uint8_t r, uint16_t v) { outw(iobase + r, v); }
static inline void rtl_wl(uint8_t r, uint32_t v) { outl(iobase + r, v); }

/* -------------------------------------------------------------------------
 * IRQ handler
 * ---------------------------------------------------------------------- */
static void rtl8139_irq_handler(registers_t *regs)
{
    (void)regs;

    uint16_t isr = rtl_rw(RTL_ISR);
    rtl_ww(RTL_ISR, isr);           /* acknowledge all pending bits */

    if (!(isr & (ISR_ROK | ISR_RER))) {
        return;
    }

    /*
     * Drain the RX ring while the Buffer-Empty flag is clear.
     * Each packet in the ring is preceded by a 4-byte header:
     *   [0-1] RX status word
     *   [2-3] packet length (including the 4-byte Ethernet CRC)
     */
    while (!(rtl_rb(RTL_CMD) & CMD_BUFE)) {
        uint32_t off = rx_ptr % RX_BUF_SIZE;

        uint16_t pkt_status = *(const uint16_t *)(rx_buf + off);
        uint16_t pkt_len    = *(const uint16_t *)(rx_buf + off + 2u);

        /* Sanity check – ROK must be set in the packet status word */
        if (!(pkt_status & 0x01u) || pkt_len < 4u || pkt_len > 1518u) {
            /* Reset the receiver to recover from a bad state */
            rtl_wb(RTL_CMD, CMD_TE);
            rtl_ww(RTL_CAPR, 0u);
            rx_ptr = 0u;
            rtl_wb(RTL_CMD, CMD_RE | CMD_TE);
            break;
        }

        /* Frame data follows the 4-byte header; CRC is last 4 bytes */
        uint16_t data_len = pkt_len - 4u;
        if (data_len > 0u) {
            net_rx(rx_buf + off + 4u, data_len);
        }

        /* Advance the read pointer (must stay DWORD-aligned) */
        rx_ptr += (uint32_t)pkt_len + 4u + 3u;
        rx_ptr &= ~3u;

        /* Keep rx_ptr within the ring for the offset calculation */
        if (rx_ptr >= RX_BUF_SIZE) {
            rx_ptr -= RX_BUF_SIZE;
        }

        /*
         * Update CAPR: the RTL8139 spec requires writing the address of
         * the last byte we have consumed, minus 16 bytes.  The subtraction
         * wraps safely in uint16_t arithmetic if rx_ptr < 16.
         */
        uint16_t capr = (rx_ptr >= 16u)
                        ? (uint16_t)(rx_ptr - 16u)
                        : (uint16_t)(RX_BUF_SIZE + rx_ptr - 16u);
        rtl_ww(RTL_CAPR, capr);
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

int rtl8139_init(void)
{
    uint8_t bus, slot, func;

    if (!pci_find_device(RTL8139_VENDOR, RTL8139_DEVICE, &bus, &slot, &func)) {
        return 0;
    }

    /* BAR0 is the I/O-mapped base; bit 0 is the I/O-space indicator */
    uint32_t bar0 = pci_read_dword(bus, slot, func, PCI_BAR0);
    iobase = (uint16_t)(bar0 & ~0x3u);

    irq_line = pci_read_byte(bus, slot, func, PCI_INTERRUPT_LINE);

    /* Enable I/O space, memory space, and bus mastering */
    uint16_t pci_cmd = pci_read_word(bus, slot, func, PCI_COMMAND);
    pci_write_word(bus, slot, func, PCI_COMMAND,
                   (uint16_t)(pci_cmd | PCI_CMD_IO | PCI_CMD_MEM | PCI_CMD_BME));

    /* Power on (CONFIG1 = 0x00 clears the power-down bit) */
    rtl_wb(RTL_CONFIG1, 0x00u);

    /* Software reset – wait for the RST bit to self-clear */
    rtl_wb(RTL_CMD, CMD_RST);
    {
        uint32_t timeout = 100000u;
        while ((rtl_rb(RTL_CMD) & CMD_RST) && timeout--) {}
    }

    /* Read hardware MAC address from IDR0-5 */
    for (uint8_t i = 0u; i < 6u; i++) {
        nic_mac[i] = rtl_rb((uint8_t)(RTL_IDR0 + i));
    }

    /* Set up the receive ring buffer */
    rtl_wl(RTL_RBSTART, (uint32_t)(uintptr_t)rx_buf);
    rx_ptr = 0u;

    /* Set TX buffer physical addresses */
    for (uint8_t i = 0u; i < TX_BUF_COUNT; i++) {
        rtl_wl((uint8_t)(RTL_TSAD0 + i * 4u),
               (uint32_t)(uintptr_t)tx_buf[i]);
    }
    tx_cur = 0u;

    /* Acknowledge any stale interrupts, then unmask ROK + TOK */
    rtl_ww(RTL_ISR, 0xFFFFu);
    rtl_ww(RTL_IMR, ISR_ROK | ISR_TOK);

    /*
     * RX configuration:
     *   AB  = accept broadcast packets
     *   AM  = accept multicast  packets
     *   APM = accept packets matching our MAC
     *   AAP = accept all        packets (promiscuous)
     *   WRAP= 1 → NIC does not wrap mid-packet in the ring
     *   Max DMA burst = 1024 bytes (bits 11:8 = 0110)
     */
    rtl_wl(RTL_RCR, 0x0000F00Fu | (1u << 7));

    /* Enable RX and TX */
    rtl_wb(RTL_CMD, CMD_RE | CMD_TE);

    /* Register IRQ handler on the line reported by PCI config */
    if (irq_line < 16u) {
        irq_register_handler(irq_line, rtl8139_irq_handler);
        pic_unmask_irq(irq_line);
    }

    return 1;
}

int rtl8139_send(const uint8_t *data, uint16_t len)
{
    if (len == 0u || len > TX_BUF_SIZE) {
        return -1;
    }

    uint8_t idx = tx_cur;
    tx_cur = (uint8_t)((tx_cur + 1u) % TX_BUF_COUNT);

    memcpy(tx_buf[idx], data, len);

    /* Pad short frames to the 60-byte Ethernet minimum (excluding CRC) */
    if (len < 60u) {
        memset(tx_buf[idx] + len, 0, (uint32_t)(60u - len));
        len = 60u;
    }

    /*
     * Writing the length to TSD with OWN=0 hands ownership to the NIC
     * and starts transmission.  We poll for TOK (bit 15) to confirm.
     */
    rtl_wl((uint8_t)(RTL_TSD0 + idx * 4u), (uint32_t)len);

    uint32_t timeout = 500000u;
    while (timeout--) {
        uint32_t tsd = rtl_rl((uint8_t)(RTL_TSD0 + idx * 4u));
        if (tsd & TSD_TOK) {
            return 0;
        }
    }
    return -1;
}

void rtl8139_get_mac(uint8_t mac[6])
{
    memcpy(mac, nic_mac, 6u);
}
