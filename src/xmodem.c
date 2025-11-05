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
 * \brief Get calculated crc value for xmodem transfer
 *
 * \param ucChar  The CRC original character.
 * \param uwCrc Calculated CRC value.
 * \return Calculated CRC value.
 */
static uint16_t XMODEM_GetCrc(uint8_t ucChar, uint16_t uwCrc)
{

    uint16_t uwCmpt;

    /* Treat input byte as unsigned to avoid sign-extension issues */
    uwCrc = uwCrc ^ ((uint16_t)ucChar << 8);

    for (uwCmpt = 0; uwCmpt < 8; uwCmpt++)
    {
        if (uwCrc & 0x8000)
            uwCrc = uwCrc << 1 ^ CRC16POLY;
        else
            uwCrc = uwCrc << 1;
    }

    return (uwCrc & 0xFFFF);
}

/**
 * \brief Get bytes through xmodem protocol.
 *
 * \param pData  Pointer to the data buffer.
 * \param length Length of data expected.
 * \return Calculated CRC value.
 */
static uint16_t XMODEM_Getbytes(int8_t *pData, uint32_t length)
{
    uint16_t crc = 0;

    while (length--)
    {
        *pData = XMODEM_GetChar();
        /* Cast to uint8_t when computing CRC to avoid sign issues */
        crc = XMODEM_GetCrc((uint8_t)*pData, crc);
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

uint32_t XMODEM_SendFile(int8_t *pBuffer, uint32_t length, void (*processBlock)(int8_t *, uint32_t, uint16_t))
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
            crc = XMODEM_GetCrc(b, crc);
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
                continue;
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
extern uint32_t XMODEM_ReceiveFile(int8_t *pBuffer, void (*processBlock)(int8_t *, uint16_t))
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
