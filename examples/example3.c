/**
 * @file    example3.c
 * @author  Tiago Henrique dos Santos - Ciência Elétrica
 * @brief   Teste de Estresse: Validação de Overflow e Recuperação de Buffer.
 * 
 * @details 
 * **Objetivos do Teste:**
 * 1. Forçar o preenchimento total do buffer TX (32 bytes) para validar a trava de segurança.
 * 2. Verificar se a flag `tx_overflow` é acionada corretamente ao tentar inserir o 33º byte.
 * 3. Garantir que o sistema não sofra crash (estouro de pilha/RAM) ao escrever além da capacidade.
 * 4. Validar o funcionamento das funções @ref spi_tx_available e @ref spi_tx_has_space.
 * 
 * **Resultado Esperado:**
 * - O terminal serial deve reportar exatamente 13 falhas (45 tentativas - 32 espaços).
 * - A flag `tx_overflow` deve ser exibida como "SIM (Correto)".
 * - O sistema deve processar os 32 bytes aceitos e retornar ao estado vazio (00 bytes) sem travar.
 * 
 * @example example3.c
 */
#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include "spi.h"

/* Protótipos de UART para telemetria */
void uart_init(void);
void uart_puts(const char *s);
void uart_puthex8(uint8_t val);
void uart_putnewline(void);

/**
 * @brief Ponto de entrada do teste de estresse.
 */
int main(void) {
    uart_init();
    uart_puts("--- TESTE DE OVERFLOW SPI (Buffer: 32 bytes) ---\r\n");

    /* 1. Configuração Master em velocidade reduzida para facilitar observação */
    spi_config_t overflow_cfg = {
        .mode           = SPI_MODE_MASTER,
        .clock_div      = SPI_CLOCK_DIV_128, 
        .use_interrupt  = true,
        .rx_callback    = NULL,
        .pins = { .port = &PORTB, .mosi_pin = PB3, .miso_pin = PB4, .sck_pin = PB5, .ss_pin = PB2 }
    };

    if (!spi_init(&overflow_cfg)) {
        uart_puts("Erro Init\r\n");
        while(1);
    }

    spi_register_slave(0, &PORTB, PB2);
    sei();

    while (1) {
        uart_puts("\r\nIniciando burst de 45 bytes...\r\n");

        /* Limpa estados e buffers anteriores para o novo teste */
        spi_tx_flush();     
        spi_rx_flush();

        /** 
         * @brief Simulação de Carga Crítica.
         * @note Bloqueamos a ISR momentaneamente (cli) para encher o buffer 
         *       sem que o hardware SPI consuma os bytes imediatamente.
         */
        cli(); 
        
        uint8_t falhas = 0;
        for (uint8_t i = 0; i < 45; i++) {
            /* Tenta enfileirar. A partir do 33º byte (índice 32), deve retornar false. */
            if (!spi_write_async(0xAA)) {
                falhas++;
            }
        }
        
        /* Captura ocupação antes de liberar a transmissão */
        uint8_t ocupado = spi_tx_available();
        
        sei(); /* Libera a ISR para iniciar o esvaziamento do buffer via hardware */

        /* --- Relatório de Diagnóstico via UART --- */
        uart_puts("Tentativas que falharam: ");
        uart_puthex8(falhas); // Esperado: 0D (13 decimal)
        uart_putnewline();

        uart_puts("Bytes aceitos no buffer: ");
        uart_puthex8(ocupado); // Esperado: 20 (32 decimal)
        uart_putnewline();

        uart_puts("Flag tx_overflow: ");
        uart_puts(spi_had_overflow() ? "SIM (Correto)" : "NAO (Erro)");
        uart_putnewline();

        /* Aguarda o esvaziamento total do buffer em background */
        uart_puts("Transmitindo os dados aceitos...");
        while (spi_is_busy()); 
        uart_puts(" OK.\r\n");

        uart_puts("Status final - Bytes no TX: ");
        uart_puthex8(spi_tx_available());
        uart_putnewline();

        _delay_ms(5000); /* Aguarda 5 segundos para repetir o ciclo */
    }
}

/* ============================================================================
   Funções de Telemetria UART0
   ========================================================================== */
void uart_init(void)
{    
    uint16_t ubrr = 103; // Cálculo do UBRR para 9600 bps, modo normal - UBRR = (F_CPU / (16 * BAUD)) - 1
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)ubrr;    
    UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0); // Habilita transmissão, recepção e interrupção Rx
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); // Formato do frame: 8 bits, 1 stop bit, sem paridade
}

void uart_putc(char c)
{
    while (!(UCSR0A & (1 << UDRE0)));      // espera buffer vazio
    UDR0 = c;
}

void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

void uart_puthex8(uint8_t val)
{
    const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[(val >> 4) & 0x0F]);
    uart_putc(hex[val & 0x0F]);
}

void uart_putnewline(void)
{
    uart_putc('\r');
    uart_putc('\n');
}
