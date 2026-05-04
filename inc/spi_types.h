/**
 * @file    spi_types.h
 * @brief   Definições de tipos, enums e estruturas para a biblioteca SPI.
 * @details Projetada para microcontroladores AVR (ATmega48/88/168/328). 
 *          Foca em baixo consumo de recursos e segurança em ambientes com interrupção.
 * 
 * @note    Tamanhos de buffer devem ser potências de 2 para otimização de performance.
 * 
 * @author  Engenheiro Embarcado
 * @date    2025-2026
 */

#ifndef SPI_TYPES_H
#define SPI_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <avr/io.h> 

#ifndef NULL
    /** @brief Definição padrão de NULL caso não esteja presente. */
    #define NULL ((void *)0)
#endif

/**
 * @defgroup SPI_CONFIG_CONSTS Constantes de Configuração
 * @{
 */

/** 
 * @brief Tamanho do buffer de transmissão (TX). 
 * @warning Deve ser obrigatoriamente potência de 2.
 */
#define SPI_TX_BUFFER_SIZE      32u

/** 
 * @brief Tamanho do buffer de recepção (RX). 
 * @warning Deve ser obrigatoriamente potência de 2.
 */
#define SPI_RX_BUFFER_SIZE      32u

/* Verificação estática de potência de 2 */
#if (SPI_TX_BUFFER_SIZE & (SPI_TX_BUFFER_SIZE - 1)) != 0
    #error "SPI_TX_BUFFER_SIZE deve ser potência de 2"
#endif
#if (SPI_RX_BUFFER_SIZE & (SPI_RX_BUFFER_SIZE - 1)) != 0
    #error "SPI_RX_BUFFER_SIZE deve ser potência de 2"
#endif
/** @brief Quantidade máxima de escravos gerenciáveis pelo driver. */
#define SPI_MAX_SLAVES          4u

/** @} */

/**
 * @brief Modos de operação do periférico SPI.
 */
typedef enum {
    SPI_MODE_MASTER = 1,    /**< Mestre: gera o sinal de clock e controla o CS. */
    SPI_MODE_SLAVE  = 0     /**< Escravo: reage ao sinal de clock externo. */
} spi_mode_t;

/**
 * @brief Opções de prescaler para o clock SPI (Modo Master).
 * @details Os valores mapeiam a combinação dos bits SPR1:0 e SPI2X.
 */
typedef enum {
    SPI_CLOCK_DIV_4     = 0b000,    /**< fosc / 4   */
    SPI_CLOCK_DIV_16    = 0b001,    /**< fosc / 16  */
    SPI_CLOCK_DIV_64    = 0b010,    /**< fosc / 64  */
    SPI_CLOCK_DIV_128   = 0b011,    /**< fosc / 128 */
    SPI_CLOCK_DIV_2     = 0b100     /**< fosc / 2 (Modo Double Speed) */
} spi_clock_div_t

/**
 * @brief Ordem de transmissão de dados no barramento.
 */
typedef enum {
    SPI_ORDER_MSB_FIRST = 0,        /**< Bit mais significativo enviado primeiro. */
    SPI_ORDER_LSB_FIRST = 1         /**< Bit menos significativo enviado primeiro. */
} spi_bit_order_t;

/**
 * @brief Códigos de status e erros das operações SPI.
 */
typedef enum {
    SPI_OK              = 0,        /**< Operação concluída com sucesso. */
    SPI_BUSY            = -1,       /**< Recurso ocupado ou transmissão em curso. */
    SPI_TIMEOUT         = -2,       /**< Tempo limite de espera excedido. */
    SPI_OVERFLOW        = -3,       /**< Buffer cheio, dados foram perdidos. */
    SPI_NO_SLAVE        = -4,       /**< ID de escravo não registrado. */
    SPI_INVALID_PARAM   = -5        /**< Parâmetro de configuração inválido. */
} spi_status_t;

/**
 * @brief Mapeamento de hardware para os pinos do barramento.
 */
typedef struct {
    volatile uint8_t *port;     /**< Endereço do registrador PORT (ex: &PORTB). */
    uint8_t           mosi_pin; /**< Número do pino MOSI. */
    uint8_t           miso_pin; /**< Número do pino MISO. */
    uint8_t           sck_pin;  /**< Número do pino SCK.  */
    uint8_t           ss_pin;   /**< Número do pino SS (Hardware Slave Select). */
} spi_pins_t;

/**
 * @brief Estrutura de configuração para inicialização do SPI.
 */
typedef struct {
    spi_mode_t       mode;              /**< Modo Master ou Slave. */
    spi_clock_div_t  clock_div;         /**< Divisor de clock (relevante apenas em Master). */
    uint8_t          cpol       : 1;    /**< Polaridade: 0=Repouso Baixo, 1=Repouso Alto. */
    uint8_t          cpha       : 1;    /**< Fase: 0=Amostra na subida, 1=Amostra na descida. */
    spi_bit_order_t  bit_order;         /**< Ordem dos bits (MSB/LSB). */
    bool             use_interrupt;     /**< true para modo assíncrono (ISR), false para polling. */
    
    spi_pins_t       pins;              /**< Definição física dos pinos. */
    
    /**
     * @brief Função de callback para eventos de recepção.
     * @param received_byte Byte que acabou de chegar.
     * @param overflow_detected Indica se o buffer RX falhou em armazenar o byte.
     */
    void             (*rx_callback)(uint8_t received_byte, bool overflow_detected);
} spi_config_t;

/**
 * @brief Estrutura de controle (Handle) do driver SPI.
 * @internal Armazena o estado em tempo de execução e buffers.
 */
typedef struct {
    spi_config_t     config;                    /**< Cópia da configuração ativa. */

    volatile bool    busy;                      /**< Indica operação em progresso. */
    volatile bool    multi_cs_active;           /**< Mantém CS baixo entre transferências. */

    uint8_t          tx_buffer[SPI_TX_BUFFER_SIZE]; /**< Buffer circular de saída. */
    uint8_t          rx_buffer[SPI_RX_BUFFER_SIZE]; /**< Buffer circular de entrada. */

    volatile uint8_t tx_head;                   /**< Índice de inserção no TX. */
    volatile uint8_t tx_tail;                   /**< Índice de remoção no TX. */
    volatile uint8_t rx_head;                   /**< Índice de inserção no RX. */
    volatile uint8_t rx_tail;                   /**< Índice de remoção no RX. */

    volatile bool    tx_overflow;               /**< Flag de erro de estouro no TX. */
    volatile bool    rx_overflow;               /**< Flag de erro de estouro no RX. */

    /** @brief Tabela de pinos CS para múltiplos escravos. */
    struct {
        volatile uint8_t *port;                 /**< Porta do pino CS. */
        uint8_t           pin_mask;             /**< Máscara de bit do pino CS. */
    } slaves[SPI_MAX_SLAVES];

    uint8_t          current_slave_id;          /**< ID do escravo atualmente selecionado. */
    uint16_t         timeout_default;           /**< Timeout padrão para operações bloqueantes. */
} spi_handle_t;

/**
 * @brief Instância única do driver SPI.
 */
extern spi_handle_t spi;

/**
 * @name Macros de Status
 * @{
 */

/** @brief Máscara para cálculo de índice circular. */
#define SPI_BUFFER_MASK(size)   ((size) - 1u)

/** @brief Retorna a quantidade de bytes ocupados no buffer TX. */
#define SPI_TX_AVAILABLE()      ((uint8_t)(spi.tx_head - spi.tx_tail))

/** @brief Retorna a quantidade de bytes prontos para leitura no buffer RX. */
#define SPI_RX_AVAILABLE()      ((uint8_t)(spi.rx_head - spi.rx_tail))

/** @} */
#endif /* SPI_TYPES_H */