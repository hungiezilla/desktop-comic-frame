//Importing standard libraries
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
#include "hardware/timer.h"
#include "hardware/adc.h"

//Defining pins and common elements
#define I2C_PORT i2c1
#define RV3028_ADDR 0x52
#define RV3028_REG_SECONDS 0x00

//Defining all RP2040 GPIO pins
#define PIN_FLASH_CHIP_SELECT 0
#define PIN_EINK_CHIP_SELECT 1
#define PIN_SERIAL_CLOCK 2
#define PIN_MASTER_OUT_SLAVE_IN 3
#define PIN_MASTER_IN_SLAVE_OUT 4
#define PIN_I2C_SERIAL_DATA 6
#define PIN_I2C_SERIAL_CLOCK 7
#define PIN_USER_INPUT 26
#define PIN_MOSFET_SATURATE 27
#define PIN_RTC_INTERRUPT_EINK_RESET 28
#define PIN_EINK_DATA_OR_COMMAND 29

#define SPI_BAUD_RATE 1000000 //Change to 10MHz if slow
#define I2C_BAUD_RATE 100000

//Comic memory spacing
#define RAM_PARTITION_SIZE 13600
#define MASTER_BW_OFFSET 0x000000
#define MASTER_RED_OFFSET 0x003520
#define SLAVE_BW_OFFSET 0x006A40
#define SLAVE_RED_OFFSET 0x009F60

//Quick commands for e-ink
#define EPD_MASTER_RAM_X_PTR  0x4E
#define EPD_MASTER_RAM_Y_PTR  0x4F
#define EPD_MASTER_WRITE_BW   0x24
#define EPD_MASTER_WRITE_RED  0x26

#define EPD_SLAVE_RAM_X_PTR   0xCE
#define EPD_SLAVE_RAM_Y_PTR   0xCF
#define EPD_SLAVE_WRITE_BW    0xA4
#define EPD_SLAVE_WRITE_RED   0xA6

//Comic numbers for UI
#define ERROR_SYNC_FRAME 2455
#define SYNC_FRAME 2454
#define SETUP_FRAME 2453

//Look-up table for greyscale
const uint8_t SSD1683_4GRAY_LUT[233] = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 
0x01, 0x4A, 0x00, 0x00, 0x00, 0x01, 0x00,
0x01, 0x82, 0x42, 0x00, 0x00, 0x10, 0x00,
0x01, 0x8A, 0x00, 0x00, 0x00, 0x01, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 
0x01, 0x41, 0x00, 0x00, 0x00, 0x01, 0x00,
0x01, 0x82, 0x42, 0x00, 0x00, 0x10, 0x00,
0x01, 0x81, 0x00, 0x00, 0x00, 0x01, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
 
0x01, 0x81, 0x00, 0x00, 0x00, 0x01, 0x00,
0x01, 0x82, 0x42, 0x00, 0x00, 0x10, 0x00,
0x01, 0x41, 0x00, 0x00, 0x00, 0x01, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

0x01, 0x8A, 0x00, 0x00, 0x00, 0x01, 0x00,
0x01, 0x82, 0x42, 0x00, 0x00, 0x10, 0x00,
0x01, 0x4A, 0x00, 0x00, 0x00, 0x01, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 

0x02, 0x00, 0x00,    //FR, XON
0x22, 0x17, 0x41, 0xA8, 0x32, 0x40, 
//EOPT  VGH   VSH1  VSH2  VSL   VCOM

};

//Shuts off everything in the system, entering "deep sleep"
void enter_deep_sleep() {
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
    clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX, 
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_ROSC_CLKSRC, 6 * MHZ, 6 * MHZ);
    clock_configure(clk_ref, CLOCKS_CLK_REF_CTRL_SRC_VALUE_ROSC_CLKSRC_PH, 0, 6 * MHZ, 6 * MHZ);

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

//Sends commands to the e-ink display
void epd_send_command(uint8_t cmd) {
    gpio_put(PIN_EINK_DATA_OR_COMMAND, 0); //Tells display this is a command
    gpio_put(PIN_EINK_CHIP_SELECT, 0); //Tells display bus incoming
    sleep_us(1); //Delay for chip select startup time
    spi_write_blocking(spi0, &cmd, 1); //Uses spi0 to send &cmd, which is 1 byte
    sleep_us(1); //Delay for chip select hold time
    gpio_put(PIN_EINK_CHIP_SELECT, 1); //Frees bus
}

//Sends data to the e-ink display
void epd_send_data(uint8_t data) {
    gpio_put(PIN_EINK_DATA_OR_COMMAND, 1); //Tells display this is data
    gpio_put(PIN_EINK_CHIP_SELECT, 0); //Tells display bus incoming
    sleep_us(1); //Delay for chip select startup time
    spi_write_blocking(spi0, &data, 1); //Uses spi0 to send &data, which is 1 byte
    sleep_us(1); //Delay for chip select hold time
    gpio_put(PIN_EINK_CHIP_SELECT, 1); //Frees bus
}

//Cuts off all internal physical traces to prevent parasitic backfeeding
void cutoff_flash_eink_high_z () {
    //Initializes all flash / e-ink pins as I/O; sets them to input; disables pull-up resistors
    //This is to prevent parasitic backfeeding draining the battery
    gpio_init(PIN_FLASH_CHIP_SELECT); gpio_set_dir(PIN_FLASH_CHIP_SELECT, GPIO_IN); gpio_disable_pulls(PIN_FLASH_CHIP_SELECT);
    gpio_init(PIN_SERIAL_CLOCK); gpio_set_dir(PIN_SERIAL_CLOCK, GPIO_IN); gpio_disable_pulls(PIN_SERIAL_CLOCK);
    gpio_init(PIN_MASTER_OUT_SLAVE_IN); gpio_set_dir(PIN_MASTER_OUT_SLAVE_IN, GPIO_IN); gpio_disable_pulls(PIN_MASTER_OUT_SLAVE_IN);
    gpio_init(PIN_MASTER_IN_SLAVE_OUT); gpio_set_dir(PIN_MASTER_IN_SLAVE_OUT, GPIO_IN); gpio_disable_pulls(PIN_MASTER_IN_SLAVE_OUT);
    gpio_init(PIN_EINK_CHIP_SELECT); gpio_set_dir(PIN_EINK_CHIP_SELECT, GPIO_IN); gpio_disable_pulls(PIN_EINK_CHIP_SELECT);
    gpio_init(PIN_EINK_DATA_OR_COMMAND); gpio_set_dir(PIN_EINK_DATA_OR_COMMAND, GPIO_IN); gpio_disable_pulls(PIN_EINK_DATA_OR_COMMAND);
}

//Copy selected image from flash to e-ink
void transfer_image_flash_eink(uint32_t flash_addr, uint8_t target_ram_cmd) {
    epd_send_command(target_ram_cmd); //Targets correct address

    //Initializes 5 byte array, right-shifts 32 bit integer by 16 + filtering, right shifts 8 + masks, masks
    uint8_t flash_cmd[5] = {0x13, (flash_addr >> 24) & 0xFF, (flash_addr >> 16) & 0xFF, (flash_addr >> 8) & 0xFF, flash_addr & 0xFF};
    gpio_put(PIN_FLASH_CHIP_SELECT, 0); //Sets flash to READ mode
    sleep_us(1);
    spi_write_blocking(spi0, flash_cmd, 5); //Transmits 5 byte array

    uint8_t pixel_buffer = 0xFF; //Setts buffer variable to white
    uint8_t next_pixel; 
    spi_read_blocking(spi0, 0xFF, &pixel_buffer, 1); //Sends dummy byte while flash memory transmits first real byte

    gpio_put(PIN_EINK_DATA_OR_COMMAND, 1); //Sets e-ink to data mode
    gpio_put(PIN_EINK_CHIP_SELECT, 0); //Asserts CS line, now both e-ink and flash are reading the bus
    sleep_us(1);

    //Core copy loop
    for (uint32_t i = 0; i < RAM_PARTITION_SIZE; i++) {
        spi_write_read_blocking(spi0, &pixel_buffer, &next_pixel, 1); //Writes current byte, reads next
        pixel_buffer = next_pixel; //Sets current byte to next byte before restarting loop
    }

    sleep_us(1);
    gpio_put(PIN_EINK_CHIP_SELECT, 1); //Closes bus to e-ink
    gpio_put(PIN_FLASH_CHIP_SELECT, 1); //Closes bus to flash
}

/* Loads custom LUT to both Master (0x3X) and Slave (0xBX) ICs */
void epd_load_custom_lut() {
    epd_send_command(0x32);
    for(uint16_t i = 0; i < 227; i++) {
        epd_send_data(SSD1683_4GRAY_LUT[i]);
    }
    epd_send_command(0x3f);
    epd_send_data(SSD1683_4GRAY_LUT[227]);

    /* Master power and VCOM configuration */
    epd_send_command(0x03);
    epd_send_data(SSD1683_4GRAY_LUT[228]);
    epd_send_command(0x04);
    epd_send_data(SSD1683_4GRAY_LUT[229]);
    epd_send_data(SSD1683_4GRAY_LUT[230]);
    epd_send_data(SSD1683_4GRAY_LUT[231]);
    epd_send_command(0x2C);
    epd_send_data(SSD1683_4GRAY_LUT[232]);

    /* Slave LUT configuration */
    epd_send_command(0xB2);
    for(uint16_t i = 0; i < 227; i++) {
        epd_send_data(SSD1683_4GRAY_LUT[i]);
    }
    epd_send_command(0xBF);
    epd_send_data(SSD1683_4GRAY_LUT[227]);

    /* Slave power and VCOM configuration */
    epd_send_command(0x83);
    epd_send_data(SSD1683_4GRAY_LUT[228]);
    epd_send_command(0x84);
    epd_send_data(SSD1683_4GRAY_LUT[229]);
    epd_send_data(SSD1683_4GRAY_LUT[230]);
    epd_send_data(SSD1683_4GRAY_LUT[231]);
    epd_send_command(0xAC);
    epd_send_data(SSD1683_4GRAY_LUT[232]);
}

/*
Executes a hardware normalization sequence using the built-in OTP LUT.
Overwrites all Master and Slave RAM partitions with pure white data.
Increases sleep timer to 10 seconds to prevent SPI collisions.
*/
void normalize_display_to_white() {
    epd_send_command(0x24);
    for(uint32_t i = 0; i < RAM_PARTITION_SIZE; i++) {
        epd_send_data(0xFF);
    }
    
    epd_send_command(0x26);
    for(uint32_t i = 0; i < RAM_PARTITION_SIZE; i++) {
        epd_send_data(0x00);
    }

    epd_send_command(0xA4);
    for(uint32_t i = 0; i < RAM_PARTITION_SIZE; i++) {
        epd_send_data(0xFF);
    }

    epd_send_command(0xA6);
    for(uint32_t i = 0; i < RAM_PARTITION_SIZE; i++) {
        epd_send_data(0x00);
    }

    epd_send_command(0x22);
    epd_send_data(0xF7);
    epd_send_command(0x20);
    
    sleep_ms(4000); 
}

int main () {
    stdio_init_all(); //Initializes all I/O pins
    i2c_init(I2C_PORT, I2C_BAUD_RATE); //Initializes i2c1 at 100kHz
    adc_init(); //Enables RP2040's SAR ADC
    adc_gpio_init(PIN_USER_INPUT); //Sets that ADC to GPIO 26

    gpio_set_function(PIN_I2C_SERIAL_DATA, GPIO_FUNC_I2C); //Configures GPIO 6+7 to be controlled by i2c1
    gpio_set_function(PIN_I2C_SERIAL_CLOCK, GPIO_FUNC_I2C);
    gpio_disable_pulls(PIN_I2C_SERIAL_DATA); //Disables the internal pull-up resistors
    gpio_disable_pulls(PIN_I2C_SERIAL_CLOCK); //since the RTC has its own

    gpio_init(PIN_RTC_INTERRUPT_EINK_RESET); //Initializes GPIO 28 as I/O
    gpio_set_dir(PIN_RTC_INTERRUPT_EINK_RESET, GPIO_IN); //Sets GPIO 28 to an input pin
    gpio_disable_pulls(PIN_RTC_INTERRUPT_EINK_RESET); //Disables pull-up resistor to prevent the MCU from interacting

    gpio_init(PIN_MOSFET_SATURATE); //Initializes GPIO 27 to I/O
    gpio_set_dir(PIN_MOSFET_SATURATE, GPIO_OUT); //Sets GPIO 27 to an output pin
    gpio_put(PIN_MOSFET_SATURATE, 0); //Drives GPIO 27 LOW, disabling the MOSFET by default

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
    const uint8_t status_clear_payload[2] = {0x0F, 0x00}; //Clears timer interrupt flags
    i2c_write_blocking(I2C_PORT,  RV3028_ADDR, status_clear_payload, 2, false);

    const uint8_t reset_timer_payload[2] = {0x0E, 0x00}; //Stops interrupt generation
    i2c_write_blocking(I2C_PORT, RV3028_ADDR, reset_timer_payload, 2, false);

    const uint8_t change_route_payload[2] = {0x10, 0x10}; //Sets RV3028 logic to use INT pin
    i2c_write_blocking(I2C_PORT, RV3028_ADDR, change_route_payload, 2, false);

    
    //Sets the countdown value, a 12 bit integer across 2 registers
    const uint8_t timer_lsb_payload[2] = {0x0A, 0x3C}; //Timer A is 8 bits (least significant byte)
    i2c_write_blocking(I2C_PORT, RV3028_ADDR, timer_lsb_payload, 2, false);

    const uint8_t timer_msb_payload[2] = {0x0B, 0x00}; //Timer B is 4 bits (most significant byte)
    i2c_write_blocking(I2C_PORT, RV3028_ADDR, timer_msb_payload, 2, false);

    /*
    -Writes 10001010 to 0x0F;
    -Bit 7 sets timer to repeat mode
    -Bit 3 enables the timer
    -Bits 0+1 at "10" set the frequency scaler to 1Hz (1 a second)
    */
    const uint8_t start_payload[2] = {0x0F, 0x8A};
    i2c_write_blocking(I2C_PORT, RV3028_ADDR, start_payload, 2, false);

    const uint8_t reg = RV3028_REG_SECONDS; //Preloading memory address pointer for 

    uint32_t comic_number = 2458; //Starts at 0, 2452 is last comic, 2455 last entry
    while(1) {
        /*
        -Sets variable reg to RV3028 register and data received will be rxdata
        -Reading data from an I2C slave (RV3028 in this case) requires 2 parts
        -Step 1: Write, sending just 0x00, leaving bus OPEN (true)
        -Step 2 Set bus to read mode, setting rxdata to the data at &reg
        making sure to end with "false" to end transmission
        */
        uint8_t rxdata;
        i2c_write_blocking(I2C_PORT, RV3028_ADDR, &reg, 1, true);
        i2c_read_blocking(I2C_PORT, RV3028_ADDR, &rxdata, 1, false);

        //Set flash and eink pins to high-z to stop parasitic backfeeding
        cutoff_flash_eink_high_z();

        gpio_put(PIN_MOSFET_SATURATE, 1); //Drives GPIO 27 HIGH, saturating MOSFET
        sleep_ms(50); //Delays to allow current to settle in e-ink and flash capacitors

        i2c_write_blocking(I2C_PORT, RV3028_ADDR, reset_timer_payload, 2, false); //Uses previous reset timer payload
        sleep_ms(200); //Allows e-ink display time to setup

        //Sets pins to output; Drives them HIGH to default idle state immediately
        gpio_set_dir(PIN_FLASH_CHIP_SELECT, GPIO_OUT); gpio_put(PIN_FLASH_CHIP_SELECT, 1);
        gpio_set_dir(PIN_EINK_CHIP_SELECT, GPIO_OUT); gpio_put(PIN_EINK_CHIP_SELECT, 1);
        gpio_set_dir(PIN_EINK_DATA_OR_COMMAND, GPIO_OUT); gpio_put(PIN_EINK_DATA_OR_COMMAND, 1);

        spi_init(spi0, SPI_BAUD_RATE); //Initializes spi0 to 1MHz
        //Formats SPI to 8 bits, clock polarity -> default, clock phase -> data in = leading, data out = trailing, MSB first
        spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST); 
        gpio_set_function(PIN_SERIAL_CLOCK, GPIO_FUNC_SPI); //Sets SPIO 2+3+4 to SPI mode
        gpio_set_function(PIN_MASTER_OUT_SLAVE_IN, GPIO_FUNC_SPI);
        gpio_set_function(PIN_MASTER_IN_SLAVE_OUT, GPIO_FUNC_SPI);
        sleep_ms(10); //Sleep 10ms to guarantee bus stability

        epd_send_command(0x12); //Software reset command
        sleep_ms(200); //Allows time for e-ink to clear

        uint8_t flash_id_cmd = 0x9F; //Read identification (JEDEC ID) command
        uint8_t flash_id_rx[3] = {0, 0, 0};
        gpio_put(PIN_FLASH_CHIP_SELECT, 0);
        sleep_us(1);
        spi_write_blocking(spi0, &flash_id_cmd, 1);
        spi_read_blocking(spi0, 0, flash_id_rx, 3);
        sleep_us(1);
        gpio_put(PIN_FLASH_CHIP_SELECT, 1);

        epd_send_command(0x0C); //Sets up timing and  current limits of e-ink boost converter
        epd_send_data(0x8B);
        epd_send_data(0x9C);
        epd_send_data(0xA6);
        epd_send_data(0x0F);
        epd_send_command(0x3C); //Sets up the border behavior (all white to prevent edge artifacting)
        epd_send_data(0x81); 
        epd_send_command(0x11); 
        epd_send_data(0x01); 

        epd_send_command(0x44); //Sets start and end boundaries for X axis
        epd_send_data(0x00);
        epd_send_data(0x31);
        epd_send_command(0x45); //Sets start and end boundaries for Y axis
        epd_send_data(0x0F);
        epd_send_data(0x01);
        epd_send_data(0x00);
        epd_send_data(0x00);

        epd_send_command(0x4E); //Sets X axis address to origin
        epd_send_data(0x00);
        epd_send_command(0x4F); //Sets Y axis address to origin
        epd_send_data(0x0F);
        epd_send_data(0x01);

        epd_send_command(0x91); 
        epd_send_data(0x00); 
        
        epd_send_command(0xC4); 
        epd_send_data(0x31);
        epd_send_data(0x00);
        epd_send_command(0xC5); 
        epd_send_data(0x0F);
        epd_send_data(0x01);
        epd_send_data(0x00);
        epd_send_data(0x00);
        epd_send_command(0xCE); 
        epd_send_data(0x31);
        epd_send_command(0xCF); 
        epd_send_data(0x0F);
        epd_send_data(0x01);
        
        /* MAIN DATA TRANSFER */
        normalize_display_to_white();

        epd_load_custom_lut();

        uint32_t base_address = RAM_PARTITION_SIZE * 4 * comic_number;

        epd_send_command(0x4E); 
        epd_send_data(0x00);
        epd_send_command(0x4F); 
        epd_send_data(0x0F);
        epd_send_data(0x01);

        transfer_image_flash_eink(base_address + MASTER_BW_OFFSET, EPD_MASTER_WRITE_BW);
        transfer_image_flash_eink(base_address + MASTER_RED_OFFSET, EPD_MASTER_WRITE_RED);

        epd_send_command(0xCE); //Sets X pointer to start value
        epd_send_data(0x31);
        epd_send_command(0xCF); //Sets Y pointer to start value
        epd_send_data(0x0F);
        epd_send_data(0x01);
        
        transfer_image_flash_eink(base_address + SLAVE_BW_OFFSET, EPD_SLAVE_WRITE_BW);
        transfer_image_flash_eink(base_address + SLAVE_RED_OFFSET, EPD_SLAVE_WRITE_RED);

        epd_send_command(0x22); //Configures the master refresh
        epd_send_data(0xCF); 
        epd_send_command(0x20); //Refreshes the whole screen!!
        sleep_ms(10000); //Allows the screen time to refresh even in worse-case scenario
        //Set to 4s in final

        comic_number--;
        //Set flash and eink pins to high-z to stop parasitic backfeeding
        cutoff_flash_eink_high_z();
        
        gpio_put(PIN_MOSFET_SATURATE, 0); //Drives MOSFET to 0V, severing ground on e-ink and flash
        enter_deep_sleep(); //Sends RP2040 to sleep until wakeup call
    }
}