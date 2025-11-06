/* ----------------------------------------------------------------------------
 *         ATMEL Microcontroller Software Support
 * ----------------------------------------------------------------------------
 * Copyright (c) 2010, Atmel Corporation

 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaiimer below.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the disclaimer below in the documentation and/or
 * other materials provided with the distribution.
 *
 * Atmel's name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ----------------------------------------------------------------------------
 */

/**
 * \file
 *
 * Implementation of XMODEM transfer protocols
 *
 */

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdint.h>
#include <avr/io.h>
#include <avr/pgmspace.h>

extern volatile uint16_t ticks;

/*----------------------------------------------------------------------------
 *        Local definitions
 *----------------------------------------------------------------------------*/
/** The definitions are followed by the X/Ymodem protocol */
#define XMDM_SOH 0x01 /**< Start of heading */
#define XMDM_STX 0x02 /**< Start of text */
#define XMDM_EOT 0x04 /**< End of text */
#define XMDM_ACK 0x06 /**< Acknowledge  */
#define XMDM_NAK 0x15 /**< negative acknowledge */
#define XMDM_CAN 0x18 /**< Cancel */
#define XMDM_ESC 0x1b /**< Escape */

#define CRC16POLY 0x1021 /**< CRC 16 polynom */

#define UART_IsRxReady() (UCSR0A & (1 << RXC0))

/*----------------------------------------------------------------------------
 *        Local variables
 *----------------------------------------------------------------------------*/
/** Xmodem transfer error indicator */
static uint8_t lastGoodSeq;
static const uint16_t crc16_table[256] PROGMEM = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

/*----------------------------------------------------------------------------
 *        Local functions
 *----------------------------------------------------------------------------*/
/**
 * \brief Transmit the character through xmodem protocol.
 *
 * \param c  Character to be transmitted.
 */
static void XMODEM_PutChar(uint8_t c)
{
    while (!(UCSR0A & _BV(UDRE0)))
        ; /* Wait for empty transmit buffer*/
    UDR0 = c;
}

/**
 * \brief Get the character through xmodem protocol.
 *
 * \return The character received
 */
static uint8_t XMODEM_GetChar(void)
{
    while (!(UCSR0A & (1 << RXC0)))
        ;
    return (UDR0);
}

/**
 * \brief Get bytes through xmodem protocol.
 *
 * \param pData  Pointer to the data buffer.
 * \param length Length of data expected.
 * \return Calculated CRC value.
 */
static uint16_t XMODEM_Getbytes(uint8_t *pData, uint32_t length)
{
    uint16_t crc = 0;

    while (length--)
    {
        *pData = XMODEM_GetChar();
        crc = (crc<<8) ^ pgm_read_word_near(crc16_table + (((crc >> 8) ^ *pData) & 0xff));
        pData++;
    }

    return (crc);
}

/**
 * \brief Get a packet through xmodem protocol
 *
 * \param pData  Pointer to the data buffer.
 * \param ucSno  Sequnce number.
 * \returns
 *      0 for sucess
 *      1 checksum error
 *      2 sequence number error
 *      3 retransmit of previous good packet
 *     -1 other error
 */
static int8_t XMODEM_GetPacket(int8_t *pData, uint8_t ucSno, uint16_t size)
{
    uint8_t cpSeq[2];
    uint16_t uwCrc, uwXcrc;

    /* Read sequence bytes directly (don't include them in data CRC) */
    cpSeq[0] = XMODEM_GetChar();
    cpSeq[1] = XMODEM_GetChar();

    uwXcrc = XMODEM_Getbytes(pData, size);

    /* An "endian independent way to combine the CRC bytes. */
    uwCrc = (uint16_t)XMODEM_GetChar() << 8;
    uwCrc += (uint16_t)XMODEM_GetChar();

    if (uwCrc != uwXcrc)
    {
        return (1);
    }
    else if ((cpSeq[0] != ucSno) || (cpSeq[1] != (uint8_t)((~(uint32_t)ucSno) & 0xff)))
    {
        /* Sequence number mismatch. If the packet matches the previous
           good packet, caller should treat it as a retransmit (code 3).
           Otherwise it's a sequence error (code 2). */
        if ((cpSeq[0] == lastGoodSeq) && (cpSeq[1] == (uint8_t)((~(uint32_t)lastGoodSeq) & 0xff)))
        {
            return (3);
        }
        return (2);
    }

    // Remember good sequence number
    lastGoodSeq = ucSno;

    return (0);
}

uint32_t XMODEM_SendFile(uint8_t *pBuffer, uint32_t length, void (*processBlock)(uint8_t *, uint32_t, uint16_t))
{
    uint8_t seqNo = 1;
    uint32_t bytesSent = 0;
    uint8_t c;
    uint16_t crc;
    uint16_t timeout;

    // Wait for receiver to request transfer
    while (1)
    {
        c = XMODEM_GetChar();
        if (c == 'C')
            break;
    }

    /* Begin sending data in 1K blocks. Handle the final partial block by
       asking the caller to fill only the remaining bytes and padding the
       rest with 0x1A (SUB) per convention. */
    while (bytesSent < length)
    {
        uint16_t chunkSize = (length - bytesSent) >= 1024 ? 1024 : (uint16_t)(length - bytesSent);

        if (processBlock != NULL)
            processBlock(pBuffer, bytesSent, chunkSize);
resend:
        XMODEM_PutChar(XMDM_STX); // Start of 1K block

        // Send sequence number and its complement
        XMODEM_PutChar(seqNo);
        XMODEM_PutChar((uint8_t)(~seqNo));

        /* Send data bytes (pad with 0x1A for the remainder of the block) */
        crc = 0;
        for (uint16_t i = 0; i < 1024; i++)
        {
            uint8_t b;
            if (i < chunkSize)
                b = (uint8_t)pBuffer[i];
            else
                b = 0x1A; /* PAD */

            XMODEM_PutChar(b);
            crc = (crc<<8) ^ pgm_read_word_near(crc16_table + (((crc >> 8) ^ b) & 0xff));
        }

        /* Send CRC */
        XMODEM_PutChar((crc >> 8) & 0xFF);
        XMODEM_PutChar(crc & 0xFF);

        /* Wait for ACK/NAK */
        timeout = ticks + 300; // 3 seconds timeout
        while ((UART_IsRxReady() == 0) && (ticks != timeout))
            ;

        if (UART_IsRxReady())
        {
            c = XMODEM_GetChar();
            if (c == XMDM_ACK)
            {
                /* Packet acknowledged */
                bytesSent += chunkSize;
                seqNo++;
            }
            else if (c == XMDM_NAK)
            {
                /* Retransmit the same packet */
                goto resend;
            }
            else
            {
                /* Unexpected response, abort */
                printf("Unexpected response: 0x%02X\n", c);
                break;
            }
        }
        else
        {
            /* Timeout waiting for response, abort */
            printf("Timeout waiting for ACK/NAK\n");
            break;
        }
    }

    /* Send EOT and wait for ACK (with timeout). */
    XMODEM_PutChar(XMDM_EOT);
    timeout = ticks + 300;
    while ((UART_IsRxReady() == 0) && (ticks != timeout))
        ;

    if (UART_IsRxReady())
    {
        c = XMODEM_GetChar();
        if (c != XMDM_ACK)
            printf("No ACK for EOT, transfer may be incomplete\n");
    }

    c = XMODEM_GetChar();

    return bytesSent;
}

/*----------------------------------------------------------------------------
 *        Exported functions
 *----------------------------------------------------------------------------*/
/**
 * \brief Receive the files through xmodem protocol
 *
 * \param pBuffer  Pointer to received buffers
 * \return 0 for sucess and other value for xmodem error
 */
uint32_t XMODEM_ReceiveFile(uint8_t *pBuffer, void (*processBlock)(uint8_t *, uint16_t))
{
    uint16_t timeout;
    uint8_t c;
    int8_t done = 0;
    uint8_t seqNo = 1;
    uint32_t size = 0;
    uint16_t pktSize;

    /* Wait and put 'C' till start xmodem transfer */
    while (1)
    {
        XMODEM_PutChar('C');

        timeout = ticks + 300; // 3 seconds timeout

        while ((UART_IsRxReady() == 0) && (ticks != timeout))
            ;

        if (UART_IsRxReady())
            break;
    }

    /* Begin to receive the data */
    lastGoodSeq = 0;
    while (done >= 0)
    {
        c = XMODEM_GetChar();

        switch (c)
        {
        /* Start of transfer */
        case XMDM_SOH:
        case XMDM_STX:
            if (c == XMDM_SOH)
                pktSize = 128;
            else
                pktSize = 1024;

            done = XMODEM_GetPacket(pBuffer, seqNo, pktSize);

            if (done == 0)
            {
                // Call the process block function if provided
                if (processBlock != NULL)
                    processBlock(pBuffer, pktSize);

                seqNo++;
                size += pktSize;
                XMODEM_PutChar(XMDM_ACK);
            }
            else
            {
                if (done == 3)
                {
                    // Retransmit of previous good packet
                    XMODEM_PutChar(XMDM_ACK);
                }
                else
                    XMODEM_PutChar(XMDM_NAK);
            }
            break;

        /* End of transfer */
        case XMDM_EOT:
            XMODEM_PutChar(XMDM_ACK);
            done = -1;
            break;

        case XMDM_CAN:
        case XMDM_ESC:
        default:
            done = -1;
            break;
        }
    }
    c = XMODEM_GetChar();
    return (size);
}
