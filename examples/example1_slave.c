/**
 * @file    example1_slave.c
 * @author  Tiago Henrique dos Santos - Ciência Elétrica
 * @brief   Exemplo SPI Slave: Resposta dinâmica baseada em processamento.
 * 
 * @details 
 * **Funcionamento:**
 * 1. O Slave aguarda o sinal de clock do Master.
 * 2. Ao receber um byte, a função @ref slave_callback é executada.
 * 3. O Slave prepara o dado para a *próxima* transmissão usando @ref spi_slave_preload.
 * 
 * **Nota sobre Full-Duplex:**
 * No protocolo SPI, o dado que o Slave carrega no registrador agora só será 
 * deslocado para o Master na transferência seguinte. Por isso, usamos o callback
 * para manter o pipeline de dados sempre atualizado.
 * 
 * @example example1_slave.c
 */

#include "spi.h"
#include <avr/io.h>
#include <avr/interrupt.h>

/** 
 * @brief Registrador de status simulado.
 */
volatile uint8_t status_reg = 0x10;

/**
 * @brief Callback de recepção para o modo Slave.
 * @details Esta função é executada dentro da ISR logo após o término de um byte.
 * 
 * @param[in] received_byte   Byte que o Master acabou de enviar.
 * @param[in] overflow        Indica se o buffer interno falhou em armazenar o dado.
 * 
 * @note No modo Slave, usamos o callback para carregar a resposta que o Master 
 *       lerá no próximo ciclo de clock.
 */
void slave_callback(uint8_t received_byte, bool overflow) {
    /* Exemplo: O Slave responde com o byte recebido incrementado de 1 */
    spi_slave_preload(received_byte + 1); 
}

/**
 * @brief Ponto de entrada do exemplo Slave.
 */
int main(void) {
    /** 
     * @note Configuração SPI Slave:
     * - O pino SS deve ser configurado como entrada (feito automaticamente por @ref spi_init).
     * - O clock_div é ignorado, pois o Slave segue o clock do Master.
     */
    spi_config_t slave_cfg = {
        .mode           = SPI_MODE_SLAVE,
        .clock_div      = SPI_CLOCK_DIV_4, // Ignorado no modo Slave (usa clock externo)
        .cpol           = 0, .cpha = 0,
        .bit_order      = SPI_ORDER_MSB_FIRST,
        .use_interrupt  = true,
        .rx_callback    = slave_callback,
        .pins = {
            .port     = &PORTB,
            .mosi_pin = PB3,
            .miso_pin = PB4,
            .sck_pin  = PB5,
            .ss_pin   = PB2   // No Slave, SS deve ser entrada para o hardware SPI ativar
        }
    };

    /* Inicializa o módulo SPI */
    spi_init(&slave_cfg);
    
    /** 
     * @brief Primeiro byte de resposta.
     * @details Prepara o SPDR com um valor inicial (ex: 0x00) para que o Master
     *          não receba lixo eletrônico na primeiríssima leitura.
     */
    spi_slave_preload(0x00); 

    /* Habilita interrupções globais */
    sei();

    while(1) {
        /**
         * O Slave permanece em loop infinito. Toda a lógica de resposta 
         * é tratada de forma assíncrona pelo driver e pelo callback.
         */
    }
}
