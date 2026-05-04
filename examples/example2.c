/**
 * @file    example2.c
 * @author  Tiago Henrique dos Santos - Ciência Elétrica
 * @brief   Exemplo: Interface com Conversor D/A MCP4921 (12 bits) via SPI.
 * 
 * @details 
 * **Objetivos do Teste:**
 * 1. Validar a transmissão multi-byte (16 bits) com controle manual de Chip Select.
 * 2. Demonstrar a injeção de hardware (pins struct) para periféricos externos.
 * 3. Gerar uma rampa de tensão analógica (0V a Vref) de forma assíncrona.
 * 
 * **Conexões Sugeridas:**
 * - AVR MOSI (PB3) -> MCP4921 SDI
 * - AVR SCK  (PB5) -> MCP4921 SCK
 * - AVR SS   (PB2) -> MCP4921 CS
 * - LDAC (MCP4921) -> Conectar ao GND (para atualização imediata da saída)
 * 
 * **Resultado Esperado:**
 * - Observar uma rampa de tensão subindo degrau a degrau a cada 500ms no pino Vout do DAC.
 * - O sinal de CS (PB2) deve permanecer em nível baixo durante o envio de ambos os bytes.
 * 
 * @example example2.c
 */
#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include "spi.h"

/** 
 * @brief Variável estática para armazenar o valor digital do DAC (0 a 4095).
 */
static uint16_t dac_value = 0;

/**
 * @brief Ponto de entrada do exemplo DAC.
 */
int main(void) {
    /** 
     * @note Configuração SPI Master:
     * - Clock de 125 kHz para garantir integridade em protoboards.
     * - Modo SPI 0,0 (Polaridade 0, Fase 0) conforme datasheet do MCP4921.
     */
    spi_config_t dac_cfg = {
        .mode           = SPI_MODE_MASTER,
        .clock_div      = SPI_CLOCK_DIV_128, // 125 kHz (Seguro para prototipagem)
        .cpol           = 0,                 // MCP4921 utiliza Modo 0,0
        .cpha           = 0,
        .bit_order      = SPI_ORDER_MSB_FIRST,
        .use_interrupt  = true,              // Uso de buffers TX/RX
        .rx_callback    = NULL,
        .pins = {
            .port     = &PORTB,
            .mosi_pin = PB3,
            .miso_pin = PB4,
            .sck_pin  = PB5,
            .ss_pin   = PB2
        }
    };

    /* Inicializa o driver SPI */
    if (!spi_init(&dac_cfg)) {
        while(1);
    }

    /* Registra o DAC como escravo ID 0 no pino PB2 */
    spi_register_slave(0, &PORTB, PB2);
    
    /* Habilita interrupções para o motor assíncrono */
    sei();

    while (1) {
        /**
         * @brief Formatação do Frame de 16 bits do MCP4921:
         * - bit 15: 0 (Escrever no registrador)
         * - bit 14: 1 (Vref Bufferizado)
         * - bit 13: 1 (Ganho 1x)
         * - bit 12: 1 (Saída Ativa)
         * - bits 11-0: Dados (12 bits)
         */
        uint16_t command = 0x3000 | (dac_value & 0x0FFF);

        uint8_t buffer[2];
        buffer[0] = (uint8_t)(command >> 8);   /* Byte mais significativo (MSB) */
        buffer[1] = (uint8_t)(command & 0xFF); //* Byte menos significativo (LSB) */

        /* --- Início da Transação --- */
        spi_select_device(0); /* Abaixa o pino CS */

        /** 
         * @brief Envio Multi-byte.
         * @param keep_cs_low Definido como true para evitar que o CS suba entre os bytes.
         */
        spi_write_multi(buffer, 2, true);

        /* Aguarda o driver terminar o envio físico dos buffers */
        while (spi_is_busy());

        spi_deselect_device(); /* Levanta o pino CS para o DAC processar o dado */
        /* --- Fim da Transação --- */

        /* Incrementa a rampa (degraus de 64). Retorna a 0 ao atingir 4096. */
        dac_value = (dac_value + 64) & 0x0FFF;

        _delay_ms(500);
    }
}
