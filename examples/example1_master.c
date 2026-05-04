/**
 * @file    example1_master.c
 * @author  Tiago Henrique dos Santos - Ciência Elétrica
 * @brief   Exemplo SPI Master: Controle de fluxo e telemetria via UART.
 * 
 * @details 
 * **Objetivos do Teste:**
 * 1. Validar o Baud Rate de 125 kHz (Fosc/128).
 * 2. Demonstrar o envio assíncrono (non-blocking) de comandos via interrupção.
 * 3. Validar a recepção Full-Duplex (receber resposta do Slave enquanto envia).
 * 
 * **Resultado Esperado:**
 * - O terminal serial deve exibir "Enviando: XX | Recebido: YY".
 * - O primeiro byte recebido será tipicamente 0x00 (inicialização).
 * - Os bytes subsequentes refletirão a resposta processada pelo Slave no ciclo anterior.
 * 
 * @example example1_master.c
 */
#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include "spi.h"

/** 
 * @brief Flag de erro para sinalizar overflow detectado na ISR.
 */
volatile bool flag_overflow_isr = false; // Global para teste

/* Protótipos das funções auxiliares de UART */
void uart_init(void);
void uart_puts(const char *s);
void uart_puthex8(uint8_t val);

/**
 * @brief Ponto de entrada do exemplo Master.
 */
int main(void) {
    uart_init();
    uart_puts("--- SPI MASTER: 125kHz Mode ---\r\n");

    /** 
     * @note Configuração SPI Master:
     * - Modo Assíncrono habilitado (use_interrupt = true).
     * - Clock de 125 kHz (ideal para longas distâncias ou periféricos lentos).
     */
    spi_config_t master_cfg = {
        .mode           = SPI_MODE_MASTER,
        .clock_div      = SPI_CLOCK_DIV_128, // 125 kHz @ 16MHz
        .cpol           = 0,                 // Mode 0
        .cpha           = 0,
        .bit_order      = SPI_ORDER_MSB_FIRST,
        .use_interrupt  = true,              // Motor assíncrono habilitado
        .rx_callback    = 0,	             // Leitura manual via buffer RX
        .pins = {
            .port = &PORTB,
            .mosi_pin = PB3,
            .miso_pin = PB4,
            .sck_pin = PB5,
            .ss_pin = PB2
        }
    };

    /* Inicializa o driver */
    if (!spi_init(&master_cfg)) {
        uart_puts("Erro na inicializacao SPI\r\n");
        while(1);
    }

    /* Registra o escravo 0 no pino SS (PB2) */
    spi_register_slave(0, &PORTB, PB2);
    
    /* Habilita interrupções globais */
    sei();

    uint8_t tx_data = 0xA5;
    uint8_t rx_data = 0;

    while (1) {
        uart_puts("Enviando: "); uart_puthex8(tx_data);

        /* Seleciona o dispositivo escravo (CS em nível baixo) */
        spi_select_device(0); // Seleciona o device

        /* Envio assíncrono: O byte vai para o buffer e a ISR gerencia a transmissão */
        spi_write_async(tx_data);

        /* Aguarda a conclusão da transmissão antes de ler o buffer RX */
        while (spi_is_busy());

        /* Verifica se houve resposta do Slave no buffer de recepção */
        if (spi_rx_read(&rx_data)) {
            uart_puts(" | Recebido do Slave: ");
            uart_puthex8(rx_data);
            uart_puts("\r\n");
        }

        tx_data++;
        _delay_ms(500); // Delay para facilitar a leitura no monitor serial
    }
}

/* ============================================================================
   Implementação das Funções UART (Telemetria)
   ========================================================================== */

/** @brief Inicializa a UART0 a 9600 bps. */
void uart_init(void)
{    
    uint16_t ubrr = 103;// Cálculo do UBRR para 9600 bps, modo normal - UBRR = (F_CPU / (16 * BAUD)) - 1
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)ubrr;    
    UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0); // Habilita transmissãoo, recepção e interrupção Rx
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); // Formato do frame: 8 bits, 1 stop bit, sem paridade
}

/** @brief Envia um caractere via UART. */
void uart_putc(char c)
{
    while (!(UCSR0A & (1 << UDRE0)));      // espera buffer vazio
    UDR0 = c;
}

/** @brief Envia uma string via UART. */
void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

/** @brief Envia um valor de 8 bits em formato hexadecimal. */
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

/**
 * @brief Exemplo de função de callback (Opcional).
 * @details Executada dentro da ISR SPI sempre que um byte novo chega.
 */
void rx_callback(uint8_t received_byte, bool overflow_detected)
{
    if (overflow_detected) {
        flag_overflow_isr = true;
    }
}
