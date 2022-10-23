// SPDX-License-Identifier: GPL-2.0+
/*
 * IT8786 Super IO Serial driver
 *
 * Adds support for 128000, 203400, 256000, 460800, and 921600 baud rates.
 * Default serial driver only supports up to 115200.
 * 
 * Timothy Lassiter <tim.lassiter@ruggedscience.com>
 */

//#define DEBUG
#define pr_fmt(fmt) "%s: " fmt, KBUILD_MODNAME

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/serial_8250.h>

#ifndef UART_DIV_MAX
#define UART_DIV_MAX 0xFFFF
#endif

#define IT8786_SERIAL_MAX_UART			6       // IT8786 offers six serial ports

#define	IT8786_SPECIAL_ADDR		        0x002E	//Address of the SuperIO's "address" port. Set this to the value of the register in the SuperIO that you would like to change / read.
#define IT8786_SPECIAL_DATA		        0x002F	//Address of the SuperIO's "data" port. Use the register to read / set the data for whatever value SPECIAL_ADDRESS was set to.
#define IT8786_LDN_REG			        0x07	//SuperIo register that holds the current logical device number in the SuperIO.
#define IT8786_CHIP_ID_REG_H            0x20
#define IT8786_CHIP_ID_REG_L            0x21

#define IT8786_SERIAL_ENABLE_REG        0x30
#define IT8786_SERIAL_BASE_REG_H        0x60
#define IT8786_SERIAL_BASE_REG_L        0x61

#define IT8786_SERIAL_CLOCK_MASK        0b0110
#define IT8786_SERIAL_CLOCK_DIV_13		0b00
#define IT8786_SERIAL_CLOCK_DIV_12 		0b01
#define IT8786_SERIAL_CLOCK_DIV_1		0b10
#define IT8786_SERIAL_CLOCK_DIV_1_625	0b11

#define IT8786_PORT(_ldn) {.ldn = _ldn, .line = -1}

struct it8786_serial_port {
    uint8_t ldn;
    int line;
    struct uart_8250_port up;
};

static struct it8786_serial_port it8786_ports[IT8786_SERIAL_MAX_UART] = {
    IT8786_PORT(0x01),
    IT8786_PORT(0x02),
    IT8786_PORT(0x08),
    IT8786_PORT(0x09),
    IT8786_PORT(0x0B),
    IT8786_PORT(0x0C),
};

static struct resource *sio_resource;

// Special set of writes that must be performed to 
// allow access to the Super IO's config registers.
static int enter_sio(void)
{
    // We already entered config mode... ignore call.
    if (sio_resource) {
        return 0;
    }

    // Ref: https://lwn.net/Articles/338837/
    if (!request_muxed_region(IT8786_SPECIAL_ADDR, 2, "it8786_serial")) {
        return -EBUSY;
    }

    // Exit config mode first to ensure we cleanly enter the SIO.
    outb(0x02, IT8786_SPECIAL_ADDR);
	outb(0x02, IT8786_SPECIAL_DATA);

    // Enter config mode.
	outb(0x87, IT8786_SPECIAL_ADDR);
	outb(0x01, IT8786_SPECIAL_ADDR);
	outb(0x55, IT8786_SPECIAL_ADDR);
	outb(0x55, IT8786_SPECIAL_ADDR);

    return 0;
}

// Special set of writes to exit config mode.
static void exit_sio(void)
{
	outb(0x02, IT8786_SPECIAL_ADDR);
	outb(0x02, IT8786_SPECIAL_DATA);

    release_region(IT8786_SPECIAL_ADDR, 2);
    sio_resource = NULL;
}

// Read the value of a single Super IO config register.
static uint8_t read_sio_reg(uint8_t reg)
{    
    // Must write the register address to "SpecialAddress"
    // then read from "SpecialData" to get it's value.
	outb(reg, IT8786_SPECIAL_ADDR);
	return inb(IT8786_SPECIAL_DATA);
}

// Set the value of a single Super IO config register.
static void write_sio_reg(uint8_t reg, uint8_t data)
{
    // Must write the register address to "SpecialAddress"
    // then write to "SpecialData" to set it's value.
	outb(reg, IT8786_SPECIAL_ADDR);
	outb(data, IT8786_SPECIAL_DATA);
}

// The Super IO uses logical device numbers (LDNs) to multiplex registers.
// Setting this LDN register allows access to different config registers.
// Convenience function for readability only.
static void set_sio_ldn(uint8_t ldn)
{
    write_sio_reg(IT8786_LDN_REG, ldn);
}

static uint16_t get_chip_id(void)
{
	uint16_t id = read_sio_reg(IT8786_CHIP_ID_REG_H) << 8;
	id |= read_sio_reg(IT8786_CHIP_ID_REG_L);
	return id;
}

static bool get_serial_port_enabled(struct it8786_serial_port *port)
{
    uint8_t data;

    set_sio_ldn(port->ldn);
    data = read_sio_reg(IT8786_SERIAL_ENABLE_REG);

    return data & 0b1;
}

static uint16_t get_serial_base_addr(struct it8786_serial_port *port)
{
    uint16_t base;

    set_sio_ldn(port->ldn);

    base = read_sio_reg(IT8786_SERIAL_BASE_REG_H) << 8;
    base |= read_sio_reg(IT8786_SERIAL_BASE_REG_L);
    return base;
}

static void set_serial_clock_div(struct it8786_serial_port *port, uint8_t divisor)
{
    uint8_t config;

    set_sio_ldn(port->ldn);
    config = read_sio_reg(0xF0);
   
    // Clear current clock settings
    config &= ~IT8786_SERIAL_CLOCK_MASK;
    // Apply the new clock settings
    config |= (divisor << 1);

    write_sio_reg(0xF0, config);
}

static void it8786_serial_set_termios(struct uart_port *port, struct ktermios *termios, struct ktermios *old)
{
    unsigned int baud;
    struct it8786_serial_port *ip;

    // Get the requested baud.
    baud = tty_termios_baud_rate(termios);
    // Get closest match to standard bauds.
    tty_termios_encode_baud_rate(termios, baud, baud);
    // Clamps the baud to the abilities of the chip
    baud = uart_get_baud_rate(port, termios, old,
				  port->uartclk / 16 / UART_DIV_MAX,
				  port->uartclk);

    ip = port->private_data;

    if (enter_sio() == 0) {
        if (baud <= 115200) {
            set_serial_clock_div(ip, IT8786_SERIAL_CLOCK_DIV_13);
            // SIO Clock (24MHz) / Clock Div (13) = 1,846,153 or close enough to standard clock of 1.8432MHz.
            port->uartclk = 1843200;
        } else {
            // In order to acheive above 115200 baud we need to
            // lower the internal clock divisor to speed up the clock.
            set_serial_clock_div(ip, IT8786_SERIAL_CLOCK_DIV_1_625);
            // SIO Clock (24MHz) / Clock Div (1.625) = 14.769230MHz.
            port->uartclk = 14769230;
        }
        exit_sio();
        pr_debug("Setting baud to %i and clock to %i\n", baud, port->uartclk);
    } else {
        pr_warn("Unable to enter Super IO config mode... Can't update clock div\n");
    }

    // We updated the uartclock value so now the 8250 driver
    // can work it's magic and handle everything else.
    serial8250_do_set_termios(port, termios, old);
}

static void __init it8786_register_ports(void)
{
    int ret, i;
    uint16_t iobase;

    for (i = 0; i < IT8786_SERIAL_MAX_UART; i++) {
        struct it8786_serial_port *ip;

        ip = &it8786_ports[i];

        ret = enter_sio();
        if (ret) {
            pr_debug("Unable to enter Super IO config mode... skipping port %d\n", i);
            continue;
        }

        if (!get_serial_port_enabled(ip)) {
            pr_info("Skipping disabled port %d at ldn 0x%x\n", i, ip->ldn);
            exit_sio();
            continue;
        }

        iobase = get_serial_base_addr(ip);

        // We want to enter and exit SIO immediately.
        // Lower lever drivers (8250_fintek) use the same address
        // that we reserve so if we don't exit before registering the port, 
        // the module hangs during initialization.
        exit_sio();

        ip->up.port.private_data = ip;
        ip->up.port.iotype = UPIO_PORT;
        ip->up.port.type = PORT_16550A;
        ip->up.port.uartclk = 1843200;
        ip->up.port.iobase = iobase;
        ip->up.port.set_termios = it8786_serial_set_termios;
        ret = serial8250_register_8250_port(&ip->up);

		if (ret < 0) {
            ip->up.port.iobase = 0;
            pr_warn("failed to register port at index %d with error %d\n", i, ret);
            continue;
		}

        if (enter_sio() == 0) {
            // Always start with the clock in the normal state.
            set_serial_clock_div(ip, IT8786_SERIAL_CLOCK_DIV_13);
            exit_sio();
        }

        pr_info("Registerd port %d at base addres 0x%lx\n", i, ip->up.port.iobase);
        ip->line = ret;
    }
}

static int __init it8786_serial_init(void)
{
	int ret = 0;
    uint16_t chip_id;

    pr_debug("Initializing module\n");

    ret = enter_sio();
    if (ret) {
        return ret;
    }

    chip_id = get_chip_id();
    exit_sio();

    if (chip_id != 0x8786) {
        pr_warn("Found invlid chip id of 0x%x\n", chip_id);
        ret = -ENODEV;
    } else {
        it8786_register_ports();
    }

	return ret;
}

static void __exit it8786_serial_exit(void)
{
    int i;

    pr_debug("Exiting module\n");

    for (i = 0; i < IT8786_SERIAL_MAX_UART; i++) {
        struct it8786_serial_port *ip = &it8786_ports[i];
        if (ip->line >= 0) {
            
            // Always reset the clock back to default so the ports
            // operate normally up to 115200 baud without the module.
            if (enter_sio() == 0) {
                set_serial_clock_div(ip, IT8786_SERIAL_CLOCK_DIV_13);
                exit_sio();
            }

            serial8250_unregister_port(ip->line);
        }
    }
}

module_init(it8786_serial_init);
module_exit(it8786_serial_exit);

MODULE_AUTHOR("Timothy Lassiter <tim.lassiter@ruggedscience.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IT8786 Super IO serial driver");
