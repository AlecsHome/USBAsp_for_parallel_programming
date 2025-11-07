/*
 * USBasp - USB in-circuit programmer for Atmel AVR controllers
 *
 * Thomas Fischl <tfischl@gmx.de>
 * 2020 fixes and tweaks by Ralph Doncaster (Nerd Ralph)
 * 2021 WCID support by Dimitrios Chr. Ioannidis ( d.ioannidis@nephelae.eu )
 *      ( based on Marius Greuel's https://github.com/mariusgreuel/USBasp )
 * 2022 Composite WCID and HID by Dimitrios Chr. Ioannidis ( d.ioannidis@nephelae.eu )
 * 2023 Serial Number write via HID and
 *      descriptors stored in EEPROM by Dimitrios Chr. Ioannidis ( d.ioannidis@nephelae.eu )
 *
 * Add parallel programming support by Andrej Choo <andrejchukov@yandex.ru>
 *
 * Target.........: ATMega16 at 16 MHz
 * Creation Date..: 2024-02-16
 *
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <util/delay.h>

#include "usbasp.h"
#include "usbdrv.h"
#include "isp.h"
#include "clock.h"
#include "tpi.h"
#include "tpi_defs.h"

// В начале main.c, после включения заголовочных файлов
typedef struct {
    uchar state;
    uchar sck_option;
    uchar address_newmode;
    unsigned long address;
    unsigned int nbytes;
    unsigned int pagesize;
    uchar blockflags;
    uchar pagecounter;
} ProgrammingState;

static ProgrammingState prog = {
    .state = PROG_STATE_IDLE,
    .sck_option = USBASP_ISP_SCK_AUTO,
    .address_newmode = 0,
    .address = 0,
    .nbytes = 0,
    .pagesize = 0,
    .blockflags = 0,
    .pagecounter = 0
};

// Обновляем extern объявления для isp.c
extern unsigned int prog_pagesize;
extern uchar prog_pagecounter;

static uchar replyBuffer[8];

//static uchar prog_state = PROG_STATE_IDLE;
//static uchar prog_sck = USBASP_ISP_SCK_AUTO;

//static uchar prog_address_newmode = 0;
//static unsigned long prog_address;
//static unsigned int prog_nbytes = 0;
//unsigned int prog_pagesize;
//static uchar prog_blockflags;
//uchar prog_pagecounter;

uchar usbFunctionSetup(uchar data[8]) {
    uchar len = 0;

    if (data[1] == USBASP_FUNC_CONNECT) {
        /* set SCK speed */
        if ((SLOW_SCK_PIN & (1 << SLOW_SCK_NUM)) == 0) {
            ispSetSCKOption(USBASP_ISP_SCK_8);
        } else {
            ispSetSCKOption(prog.sck_option);  // Используем структуру
        }

        /* set compatibility mode of address delivering */
        prog.address_newmode = 0;  // Используем структуру

        ledRedOn();
        ispConnect();

    } else if (data[1] == USBASP_FUNC_DISCONNECT) {
        ispDisconnect();
        ledRedOff();

    } else if (data[1] == USBASP_FUNC_READFLASH) {
        if (!prog.address_newmode)  // Используем структуру
            prog.address = (data[3] << 8) | data[2];  // Используем структуру

        prog.nbytes = (data[7] << 8) | data[6];  // Используем структуру
        prog.state = PROG_STATE_READFLASH;  // Используем структуру
        len = 0xff;

    } else if (data[1] == USBASP_FUNC_READEEPROM) {
        if (!prog.address_newmode)  // Используем структуру
            prog.address = (data[3] << 8) | data[2];  // Используем структуру

        prog.nbytes = (data[7] << 8) | data[6];  // Используем структуру
        prog.state = PROG_STATE_READEEPROM;  // Используем структуру
        len = 0xff;

    } else if (data[1] == USBASP_FUNC_ENABLEPROG) {
        replyBuffer[0] = ispEnterProgrammingMode();
        len = 1;

    } else if (data[1] == USBASP_FUNC_WRITEFLASH) {
        if (!prog.address_newmode)  // Используем структуру
            prog.address = (data[3] << 8) | data[2];  // Используем структуру

        prog.pagesize = data[4];  // Используем структуру
        prog.blockflags = data[5] & 0x0F;  // Используем структуру
        prog.pagesize += (((unsigned int) data[5] & 0xF0) << 4);  // Используем структуру
        
        if (prog.blockflags & PROG_BLOCKFLAG_FIRST) {  // Используем структуру
            prog.pagecounter = prog.pagesize;  // Используем структуру
        }
        
        prog.nbytes = (data[7] << 8) | data[6];  // Используем структуру
        prog.state = PROG_STATE_WRITEFLASH;  // Используем структуру
        len = 0xff;

    } else if (data[1] == USBASP_FUNC_WRITEEEPROM) {
        if (!prog.address_newmode)  // Используем структуру
            prog.address = (data[3] << 8) | data[2];  // Используем структуру

        prog.pagesize = 0;  // Используем структуру
        prog.blockflags = 0;  // Используем структуру
        prog.nbytes = (data[7] << 8) | data[6];  // Используем структуру
        prog.state = PROG_STATE_WRITEEEPROM;  // Используем структуру
        len = 0xff;

    } else if (data[1] == USBASP_FUNC_SETLONGADDRESS) {
        prog.address_newmode = 1;  // Используем структуру
        prog.address = *((unsigned long*) &data[2]);  // Используем структуру

    } else if (data[1] == USBASP_FUNC_SETISPSCK) {
        prog.sck_option = data[2];  // Используем структуру
        replyBuffer[0] = 0;
        len = 1;

    } else if (data[1] == USBASP_FUNC_TPI_CONNECT) {
        tpi_dly_cnt = data[2] | (data[3] << 8);
        ISP_OUT |= (1 << ISP_RST);
        ISP_DDR |= (1 << ISP_RST);
        clockWait(3);
        ISP_OUT &= ~(1 << ISP_RST);
        ledRedOn();
        clockWait(16);
        tpi_init();

    } else if (data[1] == USBASP_FUNC_TPI_DISCONNECT) {
        tpi_send_byte(TPI_OP_SSTCS(TPISR));
        tpi_send_byte(0);
        clockWait(10);
        ISP_OUT |= (1 << ISP_RST);
        clockWait(5);
        ISP_OUT &= ~(1 << ISP_RST);
        clockWait(5);
        ISP_DDR &= ~((1 << ISP_RST) | (1 << ISP_SCK) | (1 << ISP_MOSI));
        ISP_OUT &= ~((1 << ISP_RST) | (1 << ISP_SCK) | (1 << ISP_MOSI));
        ledRedOff();

    } else if (data[1] == USBASP_FUNC_TPI_READBLOCK) {
        prog.address = (data[3] << 8) | data[2];  // Используем структуру
        prog.nbytes = (data[7] << 8) | data[6];  // Используем структуру
        prog.state = PROG_STATE_TPI_READ;  // Используем структуру
        len = 0xff;

    } else if (data[1] == USBASP_FUNC_TPI_WRITEBLOCK) {
        prog.address = (data[3] << 8) | data[2];  // Используем структуру
        prog.nbytes = (data[7] << 8) | data[6];  // Используем структуру
        prog.state = PROG_STATE_TPI_WRITE;  // Используем структуру
        len = 0xff;

    } else if (data[1] == USBASP_FUNC_GETCAPABILITIES) {
        replyBuffer[0] = USBASP_CAP_0_TPI;
        replyBuffer[1] = 0;
        replyBuffer[2] = 0;
        replyBuffer[3] = 0;
        len = 4;
    }

    usbMsgPtr = replyBuffer;
    return len;
}

uchar usbFunctionRead(uchar *data, uchar len) {
    uchar i;

    /* check if programmer is in correct read state */
    if ((prog.state != PROG_STATE_READFLASH) && 
        (prog.state != PROG_STATE_READEEPROM) && 
        (prog.state != PROG_STATE_TPI_READ)) {
        return 0xff;
    }

    /* fill packet TPI mode */
    if(prog.state == PROG_STATE_TPI_READ) {
        tpi_read_block(prog.address, data, len);
        prog.address += len;
        return len;
    }

    /* fill packet ISP mode */
    for (i = 0; i < len; i++) {
        if (prog.state == PROG_STATE_READFLASH) {
            data[i] = ispReadFlash(prog.address);
        } else {
            data[i] = ispReadEEPROM(prog.address);
        }
        prog.address++;
    }

    /* last packet? */
    if (len < 8) {
        prog.state = PROG_STATE_IDLE;
    }

    return len;
}

uchar usbFunctionWrite(uchar *data, uchar len) {
    uchar retVal = 0;
    uchar i;

    /* check if programmer is in correct write state */
    if ((prog.state != PROG_STATE_WRITEFLASH) && 
        (prog.state != PROG_STATE_WRITEEEPROM) && 
        (prog.state != PROG_STATE_TPI_WRITE)) {
        return 0xff;
    }

    if (prog.state == PROG_STATE_TPI_WRITE) {
        tpi_write_block(prog.address, data, len);
        prog.address += len;
        prog.nbytes -= len;
        if(prog.nbytes <= 0) {
            prog.state = PROG_STATE_IDLE;
            return 1;
        }
        return 0;
    }

    for (i = 0; i < len; i++) {
        if (prog.state == PROG_STATE_WRITEFLASH) {
            /* Flash */
            if (prog.pagesize == 0) {
                /* not paged */
                ispWriteFlash(prog.address, data[i], 1);
            } else {
                /* paged */
                ispWriteFlash(prog.address, data[i], 0);
                prog.pagecounter--;
                if (prog.pagecounter == 0) {
                    ispFlushPage(prog.address, data[i]);
                    prog.pagecounter = prog.pagesize;
                }
            }
        } else {
            /* EEPROM */
            ispWriteEEPROM(prog.address, data[i]);
        }

        prog.nbytes--;

        if (prog.nbytes == 0) {
            prog.state = PROG_STATE_IDLE;
            if ((prog.blockflags & PROG_BLOCKFLAG_LAST) && 
                (prog.pagecounter != prog.pagesize)) {
                /* last block and page flush pending, so flush it now */
                ispFlushPage(prog.address, data[i]);
            }
            retVal = 1;
        }

        prog.address++;
    }

    return retVal;
}

void hardwareInit(void) {

	uchar i;

	PORTB &= ~((1 << 0) | (1 << 1)); /* LEDs off */

	usbDeviceDisconnect();  /* enforce re-enumeration, do this while interrupts are disabled! */
	i = 0;
	while(--i){             /* fake USB disconnect for > 250 ms */
		wdt_reset();
		_delay_ms(1);
	}
	usbDeviceConnect();

	/* all inputs except PC0, PC1 */
	DDRB |= 0x03;

	/* enable pull up on jumper */
	SLOW_SCK_PORT |= (1 << SLOW_SCK_NUM);
}

void usbHadReset() {
	ledGreenOff();
}

void usbAddressAssigned() {
	ledGreenOn();
}

int main(void) {
	usbInit();

	/* init ports */
	hardwareInit();

	/* init timer */
	clockInit();

	/* start interrupts for USB */
	sei();

	/* main loop */
	for (;;) {
		usbPoll();
	}

	return 0;
}
