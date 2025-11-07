/*
 * clock.c - part of USBasp
 *
 * Autor..........: Thomas Fischl <tfischl@gmx.de>
 * Description....: Provides functions for timing/waiting
 * Licence........: GNU GPL v2 (see Readme.txt)
 * Creation Date..: 2005-02-23
 * Last change....: 2005-04-20
 */

#include <inttypes.h>
#include <avr/io.h>
#include "clock.h"

/* wait time * 320 us */
void clockWait(uint8_t time) {
    do {
        uint8_t starttime = TCNT0;
        // Более точный расчет с учетом F_CPU = 16MHz и предделителя 8
        while ((uint8_t)(TCNT0 - starttime) < (F_CPU / 8000000) * 320); 
    } while (--time);
}

