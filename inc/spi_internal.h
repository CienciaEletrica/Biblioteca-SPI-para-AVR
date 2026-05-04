/**
 * @file    spi_internal.h
 * @brief   Definições e funções de uso INTERNO da biblioteca SPI.
 * @internal
 * 
 * @details Este arquivo contém macros para manipulação direta de hardware, 
 *          máquina de estados interna e funções inline de alta performance 
 *          para os buffers circulares.
 *
 * @note    NÃO inclua este arquivo na sua aplicação. Use apenas @ref spi.h.
 * 
 * @attention Referência: ATmega328P datasheet - Seção 19 (SPI).
 */

#ifndef SPI_INTERNAL_H
#define SPI_INTERNAL_H

#include "spi_types.h"
#include <avr/interrupt.h>
#include <stddef.h>

/**
 * @defgroup SPI_HW_MACROS Macros de Hardware
 * @brief Acesso direto aos registradores SPCR, SPSR e SPDR.
 * @{
 */
#define SPI_HW_ENABLE()             do { SPCR |=  (1 << SPE);  } while (0)
#define SPI_HW_DISABLE()            do { SPCR &= ~(1 << SPE);  } while (0)

#define SPI_HW_SET_MASTER()         do { SPCR |=  (1 << MSTR); } while (0)
#define SPI_HW_SET_SLAVE()          do { SPCR &= ~(1 << MSTR); } while (0)

#define SPI_HW_INTERRUPT_ENABLE()   do { SPCR |=  (1 << SPIE); } while (0)
#define SPI_HW_INTERRUPT_DISABLE()  do { SPCR &= ~(1 << SPIE); } while (0)

/** @brief Verifica se a flag de interrupção (SPIF) está setada */
#define SPI_HW_IS_INTERRUPT_FLAG_SET()  (SPSR & (1 << SPIF))
/** @brief Limpa a flag SPIF (requer leitura do SPSR seguida de leitura do SPDR) */
#define SPI_HW_CLEAR_INTERRUPT_FLAG()   do { (void)SPSR; (void)SPDR; } while (0)

/** @brief Detecta colisão de escrita (WCOL) */
#define SPI_HW_WRITE_COLLISION_DETECTED()   (SPSR & (1 << WCOL))

#define SPI_HW_DOUBLE_SPEED_ENABLE()    do { SPSR |=  (1 << SPI2X); } while (0)
#define SPI_HW_DOUBLE_SPEED_DISABLE()   do { SPSR &= ~(1 << SPI2X); } while (0)

#define SPI_HW_WRITE_DATA(data)         do { SPDR = (data); } while (0) /**< Escreve no registrador de dados (Inicia TX) */
#define SPI_HW_READ_DATA()              (SPDR) /**< Lê o registrador de dados recebidos */

/** @brief Máscara para bits de clock SPR0 e SPR1 no registrador SPCR */
#define SPI_HW_CLOCK_BITS(div)  (((div) & 0x03) << SPR0)

/**
 * @brief Aplica divisor de clock e modo Double Speed.
 * @param div Divisor (0..3).
 * @param double_speed Booleano para modo 2x.
 */
#define SPI_HW_APPLY_CLOCK_DIV(div, double_speed)   \
    do {                                            \
        SPCR = (SPCR & ~((1<<SPR1)|(1<<SPR0))) | SPI_HW_CLOCK_BITS(div); \
        if (double_speed) SPI_HW_DOUBLE_SPEED_ENABLE(); else SPI_HW_DOUBLE_SPEED_DISABLE(); \
    } while (0)

/** @brief Configura Polaridade (CPOL) e Fase (CPHA) */
#define SPI_HW_SET_CPOL_CPHA(cpol, cpha) \
    do {                                 \
        SPCR = (SPCR & ~((1<<CPOL)|(1<<CPHA))) | ((cpol)<<CPOL) | ((cpha)<<CPHA); \
    } while (0)

/** @brief Configura ordem dos bits (MSB ou LSB first) */
#define SPI_HW_SET_DATA_ORDER(order) \
    do {                             \
        if (order == SPI_ORDER_LSB_FIRST) SPCR |=  (1 << DORD); \
        else                              SPCR &= ~(1 << DORD); \
    } while (0)

/** @} */

/**
 * @brief Estados internos da máquina de operação do driver.
 */
typedef enum {
    SPI_STATE_IDLE,                 /**< Módulo ocioso. */
    SPI_STATE_TX_ACTIVE,            /**< Transmissão em curso. */
    SPI_STATE_TX_LAST_BYTE,         /**< Aguardando último ciclo de clock. */
    SPI_STATE_ERROR_COLLISION       /**< Erro de colisão detectado. */
} spi_internal_state_t;

/** @brief Estado atual do driver (definido no spi.c) */
extern volatile spi_internal_state_t spi_state;

/**
 * @defgroup SPI_BUFFER_INTERNAL Funções de Buffer (Inline)
 * @brief Manipulação de buffers circulares otimizada para 8 bits.
 * @{
 */

/**
 * @brief Calcula o próximo índice do buffer circular.
 * @note O tamanho do buffer deve ser obrigatoriamente potência de 2.
 */
static inline uint8_t spi_buf_next(uint8_t idx, uint8_t buf_size) {
    return (uint8_t)((idx + 1u) & (uint8_t)(buf_size - 1u));
}

/**
 * @brief Adiciona um byte à fila de transmissão.
 * @return true se inserido, false se buffer cheio.
 */
static inline bool spi_tx_enqueue(uint8_t byte) {
    uint8_t next_head = spi_buf_next(spi.tx_head, SPI_TX_BUFFER_SIZE);

    if (next_head == spi.tx_tail) {
        spi.tx_overflow = true;
        return false;
    }

    spi.tx_buffer[spi.tx_head] = byte;
    spi.tx_head = next_head;            // escrita at�mica (8 bits)
    return true;
}

/**
 * @brief Remove um byte da fila de transmissão.
 * @param[out] byte Ponteiro para armazenar o dado removido.
 * @return true se havia dado para remover.
 */
static inline bool spi_tx_dequeue(uint8_t *byte) {
    if (spi.tx_head == spi.tx_tail) {
        return false;
    }

    *byte = spi.tx_buffer[spi.tx_tail];
    spi.tx_tail = spi_buf_next(spi.tx_tail, SPI_TX_BUFFER_SIZE);
    return true;
}

/**
 * @brief Adiciona um byte recebido à fila RX (chamado na ISR).
 */
static inline bool spi_rx_enqueue(uint8_t byte) {
    uint8_t next = spi_buf_next(spi.rx_head, SPI_RX_BUFFER_SIZE);

    if (next == spi.rx_tail) {
        spi.rx_overflow = true;
        return false;
    }

    spi.rx_buffer[spi.rx_head] = byte;
    spi.rx_head = next;
    return true;
}

/**
 * @brief Extrai um byte da fila RX para a aplicação.
 */
static inline bool spi_rx_dequeue(uint8_t *byte) {
    if (spi.rx_head == spi.rx_tail) {
        return false;
    }

    *byte = spi.rx_buffer[spi.rx_tail];
    spi.rx_tail = spi_buf_next(spi.rx_tail, SPI_RX_BUFFER_SIZE);
    return true;
}

/** @} */

/**
 * @defgroup SPI_INTERNAL_PROTOS Protótipos Internos
 * @brief Funções implementadas em spi.c para controle de hardware.
 * @{
 */

/** @brief Sincroniza registradores do AVR com @ref spi.config */
void spi_hw_apply_config(void);

/** @brief Configura direção dos pinos MOSI/MISO/SCK/SS */
void spi_hw_init_pins(const spi_config_t *cfg);

/** @brief Controle de hardware para Chip Select */
void spi_hw_select_slave(uint8_t id);
void spi_hw_deselect_slave(void);

/** @brief Inicia o primeiro byte de uma sequência de transmissão */
bool spi_hw_start_tx_pipeline(void);

/** @brief Lógica central executada a cada byte concluído na ISR */
bool spi_hw_handle_transfer_complete(void);

/** @brief Tenta recuperar o estado do driver após erro de colisão */
void spi_hw_recover_collision(void);

/** @brief Carrega dado no SPDR sem disparar clock (Modo Slave) */
void spi_hw_slave_preload(uint8_t dummy);

/** @} */
#endif /* SPI_INTERNAL_H */