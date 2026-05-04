/**
 * @file    spi.h
 * @author  Seu Nome / Equipe
 * @brief   Interface pública (API) da biblioteca SPI para microcontroladores AVR.
 * @version 1.0
 * @date    2024
 *
 * @details Esta biblioteca fornece uma camada de abstração (HAL) para o periférico SPI
 *          dos AVRs (foco ATmega328P). Suporta modos Master/Slave, operação síncrona 
 *          (polling) e assíncrona (interrupção com buffers circulares).
 *
 * @note    Esta é a camada de abstração que o usuário deve utilizar.
 *          Não inclua spi_internal.h nem acesse diretamente registradores do SPI.
 *
 * @warning Não chame funções SPI dentro de ISRs de outros periféricos sem cuidado
 *          (risco de deadlock ou corrupção de estado).
 * 
 * @copyright Licença MIT (ou a sua preferência)
 */

#ifndef SPI_H
#define SPI_H

#include <stdint.h>
#include <stdbool.h>
#include "spi_types.h"      // tipos, enums, spi_handle_t e macros

#ifdef __cplusplus
extern "C" {
#endif
    
/**
 * @defgroup SPI_INIT Inicialização e Configuração
 * @{
 */

/**
 * @brief Inicializa o módulo SPI.
 * 
 * Configura o hardware SPI de acordo com a estrutura fornecida. No ATmega328P, 
 * configura automaticamente PB5 (SCK), PB3 (MOSI), PB4 (MISO) e PB2 (SS).
 * 
 * @param[in] cfg Ponteiro para a estrutura @ref spi_config_t com os parâmetros desejados.
 * @return true se a inicialização foi bem-sucedida.
 * @return false se houver parâmetros inválidos ou erro de hardware.
 * 
 * @note Habilita pull-up no pino SS para evitar entrada acidental em modo slave.
 */
bool spi_init(const spi_config_t *cfg);


/**
 * @brief Altera dinamicamente o divisor de clock do SPI.
 * 
 * @param[in] new_div Novo divisor de clock (baseado em @ref spi_clock_div_t).
 * @note Esta função aguarda qualquer transmissão em andamento terminar antes de aplicar.
 */
void spi_set_clock(spi_clock_div_t new_div);

/** @} */

/**
 * @defgroup SPI_MASTER Gerenciamento de Dispositivos (Master)
 * @{
 */

/**
 * @brief Registra um pino de Chip Select (CS) para um dispositivo escravo.
 * 
 * @param[in] id    Identificador do escravo (0 até @ref SPI_MAX_SLAVES - 1).
 * @param[in] port  Ponteiro para o registrador PORT (ex: &PORTB).
 * @param[in] pin   Número do pino no porto (0 a 7).
 * 
 * @note O pino deve ser configurado como saída manualmente pelo usuário.
 */
void spi_register_slave(uint8_t id, volatile uint8_t *port, uint8_t pin);

/**
 * @brief Seleciona um dispositivo escravo (coloca CS em nível baixo).
 * 
 * @param[in] id Identificador do escravo previamente registrado.
 * @note Se houver um dispositivo selecionado anteriormente, ele será desativado primeiro.
 */
void spi_select_device(uint8_t id);


/**
 * @brief Desativa o dispositivo escravo atual (coloca CS em nível alto).
 * 
 * @note Chamado automaticamente ao fim de transações multi-byte se `keep_cs_low` for false.
 */
void spi_deselect_device(void);

/** @} */

/**
 * @defgroup SPI_BLOCKING Transferência Síncrona (Blocking)
 * @{
 */

/**
 * @brief Transfere um byte de forma síncrona via polling.
 * 
 * @param[in] data Byte a ser enviado.
 * @return uint8_t Byte recebido durante a troca ou 0xFF em caso de timeout.
 * 
 * @note Utiliza um contador simples para timeout definido em @ref spi_config_t.
 */
uint8_t spi_transfer_blocking(uint8_t data);

/** @} */

/**
 * @defgroup SPI_ASYNC Transferência Assíncrona (Interrupt-driven)
 * @{
 */

/**
 * @brief Enfileira um byte para transmissão assíncrona.
 * 
 * @param[in] data Byte a ser transmitido.
 * @return true se o byte foi enfileirado no buffer TX.
 * @return false se o buffer TX estiver cheio.
 */
bool spi_write_async(uint8_t data);

/**
 * @brief Inicia uma transação multi-byte assíncrona.
 * 
 * @param[in] buf         Ponteiro para o array de dados.
 * @param[in] len         Tamanho dos dados em bytes.
 * @param[in] keep_cs_low Se true, mantém o pino CS baixo após o término.
 * 
 * @return true se todos os bytes foram enfileirados com sucesso.
 * @return false se não houver espaço suficiente no buffer ou módulo ocupado.
 */
bool spi_write_multi(const uint8_t *buf, uint8_t len, bool keep_cs_low);

/**
 * @brief Verifica o número de bytes disponíveis para leitura no buffer RX.
 * @return uint8_t Quantidade de bytes prontos.
 */
uint8_t spi_rx_available(void);

/**
 * @brief Lê um byte do buffer de recepção.
 * 
 * @param[out] byte Ponteiro para armazenar o valor lido.
 * @return true se um byte foi extraído com sucesso.
 * @return false se o buffer RX estiver vazio.
 */
bool spi_rx_read(uint8_t *byte);

/**
 * @brief Limpa o buffer de recepção (descarta dados).
 */
void spi_rx_flush(void);

/**
 * @brief Verifica o espaço pendente no buffer de transmissão.
 * @return uint8_t Quantidade de bytes ainda na fila TX.
 */
uint8_t spi_tx_available(void);

/**
 * @brief Limpa o buffer de transmissão e interrompe envios pendentes.
 */
void spi_tx_flush(void);

/**
 * @brief Verifica se o módulo está processando alguma transmissão.
 * @return true se houver transmissão ativa ou fila TX pendente.
 */
bool spi_is_busy(void);

/**
 * @brief Checa se ocorreu erro de overflow nos buffers.
 * @return true se houve perda de dados por buffer cheio desde o último reset/flush.
 */
bool spi_had_overflow(void);

/** @} */

/**
 * @defgroup SPI_LOWLEVEL Funções de Baixo Nível / Utilitárias
 * @{
 */

/**
 * @brief Pré-carrega o registrador de dados (SPDR) no modo Slave.
 * 
 * @param[in] dummy Valor a ser enviado quando o Master iniciar o clock.
 * @note Evita o envio de lixo eletrônico no primeiro ciclo de clock do Master.
 */
void spi_slave_preload(uint8_t dummy);

/**
 * @brief Verifica se há espaço para uma quantidade específica de bytes no TX.
 * 
 * @param[in] count Quantidade de bytes a verificar.
 * @return true se há espaço disponível.
 */
bool spi_tx_has_space(uint8_t count);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* SPI_H */