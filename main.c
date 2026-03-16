//Import various libraries needed
#include <stdio.h>
#include <pico/stdlib.h>
#include "hardware/i2c.h"
#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/rosc.h"
#include "hardware/structs/iobank0.h"
#include "hardware/xosc.h"
#include "hardware/spi.h"

//Defining stuff for human readability
#define I2C_PORT i2c1
#define RV3028_ADDR 0x52
#define RV3028_REG_SECONDS 0x00

/*
Function tells RP2040 to wake up to specific code, sets desired clock to
small 6MHz clocks, disables main clocks, fully shuts off RP2040
(waiting for defined wakeup call), then reinitializes all clocks as normal
*/
void enter_dormant_bare_metal() {
    /*
    -iobank0_hw is SDK defined struct pointer pointing to IO Bank 0, which controls GPIO 0-29
    -dormant_wake_irq_ctrl determines what events can restart
    internal oscillators when they have been stoppped
    -"interrupt enable" inte[] array selects LOW, HIGH, HIGH->LOW, LOW->HIGH
    -inte[3] is "edge fall"
    -(1u << 18) places a 1 (1u) in position 18 of the 32-bit register
    -This hardwires the RP2040 to listen to the RV3028 pulling GPIO 28 LOW
    */
    iobank0_hw->dormant_wake_irq_ctrl.inte[3] = (1u << 18);

    /*
    -Need to ensure CPU isn't using these specific
    internal oscillators before turning them off
    -RP2040 has a complex clock tree (many running),
    clk_sys is the main one @125MHz driven by pll_sys
    -These 2 clock_configure() calls are basically setting the
    main clock and reference clock to smaller/slower 6Mhz ROSC clocks
    -This is to reduce power usage
    -Syntax is clock_configure(clock selection
                               chooses between direct clock and auxillary pathway reference
                               chooses WHICH auxillary oscillator (ROSC here)
                               frequency in, frequency out)
    */
    clock_configure(clk_sys,
                    CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_ROSC_CLKSRC,
                    6 * MHZ, 6 * MHZ);
                    
    clock_configure(clk_ref,
                    CLOCKS_CLK_REF_CTRL_SRC_VALUE_ROSC_CLKSRC_PH,
                    0, 6 * MHZ, 6 * MHZ);

    /*
    -The pll_deinit ("de-initialize") functions shut down the PLLs
    which are basically MHz multipliers, turning the 12MHz XOSC into 48MHZ for USB
    and 125MHz for the ARM cores (chip)
    -xosc_disable() shuts down the internal 12MHz crystal on the RP2040
    -This is done to reduce power usage
    */
    pll_deinit(pll_sys);
    pll_deinit(pll_usb);
    xosc_disable();
    /*
    -This is writing a specific password ("com") to the RP2040
    to completely shut off the processor until it receives a wakeup call
    */
    rosc_hw->dormant = 0x636f6d;
        
    /*
    -When detecting a falling edge on GPIO 28, code resumes here
    -This function rebuilds the XOSC and PLLs, restoring 125MHz
    so the while(1) loop can run at full speed
    */
    clocks_init();
}

int main () {
    //RP2040 initial programming

    /*
    -stdio_init_all() initializes standard I/O systems (USB, TinyUSB)
    -i2c_init starts i2c1 at 100,000Hz (100KHz), the standard for I2C communication
    */
    stdio_init_all();
    i2c_init(I2C_PORT, 100 * 1000);

    /*
    -By default, most pins are controlled by software allowing 
    the user to manually turn them on/off
    -This makes pins 6+7 controlled by i2c1, not the user
    */
    gpio_set_function(6, GPIO_FUNC_I2C);
    gpio_set_function(7, GPIO_FUNC_I2C);

    /*
    -The RP2040 has internal resistors in each pin, but the RV3028 ALSO
    has resistors in the SDA and SCL lines
    -This disables the ones on the RP2040 to avoid messing with the equivalent resistance
    */
    gpio_disable_pulls(6);
    gpio_disable_pulls(7);

    /*
    -Connects pin 28 to SIO (Software I/O)
    -Sets direction of pin to be receiving voltage and reading it, not outputting
    -RV3028 has resistor for INT pin, so disables resistor in pin 28
    */
    gpio_init(28);
    gpio_set_dir(28, GPIO_IN);
    gpio_disable_pulls(28);

    /*
    -Initializes pin 27 as an OUTPUT pin unlike pin 28
    -This will drive the MOSFET
    -Immediately drive pin LOW to ensure e-ink and flash are unpowered on boot
    */
    gpio_init(27);
    gpio_set_dir(27, GPIO_OUT);
    gpio_put(27, 0);

    //RV3028 initial programming

    /*
    -RV3028 expects write transactions in pairs:
    [0] the memory register address being modified
    [1] the 8-bit data to be written to that address
    -The syntax is as follows:
    i2c_write_blocking(what i2c to use,
                       what device to wake for transmission
                       pointer + data transmitted
                       number of bytes to transmit
                       false = tells RP2040 to generate the "end of transmission" flag)
    */
   
    //Clears timer configuration
    const uint8_t status_clear_payload[2] = {0x0F, 0x00};
    i2c_write_blocking(I2C_PORT,  RV3028_ADDR, status_clear_payload, 2, false);

    /*
    -Clears timer and alarm flag
    -If timer bit is 1, INT pin pulls to 0V (falling edge for GPIO 28)
    -This clears INT pin forcing it to 3.3V since system is on right now
    */
    const uint8_t reset_timer_payload[2] = {0x0E, 0x00};
    i2c_write_blocking(I2C_PORT, RV3028_ADDR, reset_timer_payload, 2, false);

    /*
    -Sets RV3028 logic to use INT pin instead of internal memory flag
    -Without this GPIO 28 wouldn't actually be triggered
    */
    const uint8_t change_route_payload[2] = {0x10, 0x10};
    i2c_write_blocking(I2C_PORT, RV3028_ADDR, change_route_payload, 2, false);

    /*
    -This sets the countdown value, a 12 bit integer across 2 registers
    -Timer A is 8 bits (smaller/least significant byte)
    -Timer B is 4 bits (larger/most significant byte a.k.a. leftmost binary bits)
    */
    const uint8_t timer_lsb_payload[2] = {0x0A, 0x05}; //5 seconds rn
    i2c_write_blocking(I2C_PORT, RV3028_ADDR, timer_lsb_payload, 2, false);

    const uint8_t timer_msb_payload[2] = {0x0B, 0x00};
    i2c_write_blocking(I2C_PORT, RV3028_ADDR, timer_msb_payload, 2, false);

    /*
    -Writes 10001010 to 0x0F;
    -Bit 7 sets timer to repeat mode
    -Bit 3 enables the timer
    -Bits 0+1 at "10" set the frequency scaler to 1Hz (1 a second)
    */
    const uint8_t start_payload[2] = {0x0F, 0x8A};
    i2c_write_blocking(I2C_PORT, RV3028_ADDR, start_payload, 2, false);

    //Infinite loop of system governing daily operation
    //For use in first section of while(1) loop, defining out of loop is better
    const uint8_t reg = RV3028_REG_SECONDS;
    while(1) {
        /*
        -sets variable reg to RV3028 register and data received will be rxdata
        -Reading data from an I2C slave (RV3028 in this case) requires 2 parts
        -Step 1: Write, sending just 0x00, leaving bus OPEN (true)
        -Step 2 Set bus to read mode, setting rxdata to the data at &reg
        making sure to end with "false" to end transmission
        */
        uint8_t rxdata;

        i2c_write_blocking(I2C_PORT, RV3028_ADDR, &reg, 1, true);
        i2c_read_blocking(I2C_PORT, RV3028_ADDR, &rxdata, 1, false);

        /*
        Setting up flash memory now to be able to shut it off later
        */
        spi_init(spi0, 1000 * 1000);
        spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

        gpio_set_function(2, GPIO_FUNC_SPI);
        gpio_set_function(3, GPIO_FUNC_SPI);
        gpio_set_function(4, GPIO_FUNC_SPI);

        gpio_init(0);
        gpio_set_dir(0, GPIO_OUT);
        gpio_put(0, 1);

        //Sets GPIO to 3.3V, saturating the MOSFET and powering e-ink and flash
        gpio_put(27, 1);
        sleep_ms(5); //NEW

        //Flash testing, data transfer
        uint8_t tx_payload = 0x9F;
        uint8_t rx_payload[3] = {0};

        gpio_put(0, 0);
        spi_write_blocking(spi0, &tx_payload, 1);
        spi_read_blocking(spi0, 0x00, rx_payload, 3);
        gpio_put(0, 1);

        printf("JEDEC ID: %02X %02X %02X\n", rx_payload[0], rx_payload[1], rx_payload[2]);

        //SPI Flash and E-Ink logic

        /*
        Reconfiguring these as GPIO pins
        */
        gpio_init(0);
        gpio_init(2);
        gpio_init(3);
        gpio_init(4);

        gpio_set_dir(0, GPIO_IN);
        gpio_set_dir(2, GPIO_IN);
        gpio_set_dir(3, GPIO_IN);
        gpio_set_dir(4, GPIO_IN);

        gpio_disable_pulls(0);
        gpio_disable_pulls(2);
        gpio_disable_pulls(3);
        gpio_disable_pulls(4);

        //Sets GPIO 27 to 0V, shutting off power to e-ink and flash
        gpio_put(27, 0);

        sleep_ms(3000);

        //Enter deep sleep until wake
        ////enter_dormant_bare_metal();

        /*
        -RP2040 starts here after waking and initializing clocks
        -Clears the INT pin by resetting timer flag, just like earlier use of payload
        */
        i2c_write_blocking(I2C_PORT, RV3028_ADDR, reset_timer_payload, 2, false);
    }
}