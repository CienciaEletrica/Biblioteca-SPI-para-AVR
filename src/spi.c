/**
 * @file    spi.c
 * @brief   Implementação completa do driver SPI para microcontroladores AVR.
 * @details Este arquivo contém a lógica de manipulação de registros, gerenciamento
 *          de buffers circulares e a máquina de estados para operações assíncronas.
 */
#define F_CPU 16000000UL

#include "spi.h"
#include "spi_internal.h"
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <stddef.h>

/* ============================================================================
   Variáveis globais / Instância Única
   ========================================================================== */

/**
 * @brief Instância global do handle SPI.
 * @details Armazena todo o contexto operacional, incluindo buffers e configurações.
 */
spi_handle_t spi = {
    .config = {0},
    .busy = false,
    .multi_cs_active = false,
    .tx_head = 0, .tx_tail = 0,
    .rx_head = 0, .rx_tail = 0,
    .tx_overflow = false, .rx_overflow = false,
    .current_slave_id = 255u,
    .timeout_default = 250u
};

/**
 * @brief Estado interno da máquina de estados do SPI.
 * @details Controla o fluxo da ISR e a transição entre estados ocioso e ativo.
 */
volatile spi_internal_state_t spi_state = SPI_STATE_IDLE;

/* ============================================================================
   Funções internas de hardware (Implementação)
   ========================================================================== */

/**
 * @brief Configura a direção dos pinos físicos (DDR) baseada no modo Master/Slave.
 * @param cfg Ponteiro para as configurações de pinos.
 * @internal
 */
void spi_hw_init_pins(const spi_config_t *cfg) {
    volatile uint8_t *ddr = cfg->pins.port - 1; // DDR é sempre PORT - 1 em AVR
    
    if (cfg->mode == SPI_MODE_MASTER) {
        // Master: SCK, MOSI e SS como saída. MISO como entrada.
        *ddr |= (1 << cfg->pins.mosi_pin) | (1 << cfg->pins.sck_pin) | (1 << cfg->pins.ss_pin);
        *ddr &= ~(1 << cfg->pins.miso_pin);
        *(cfg->pins.port) |= (1 << cfg->pins.ss_pin); // Pull-up / High no SS
    } else {
        *ddr &= ~((1 << cfg->pins.mosi_pin) | (1 << cfg->pins.sck_pin) | (1 << cfg->pins.ss_pin));
        *ddr |= (1 << cfg->pins.miso_pin);
        *(cfg->pins.port) |= (1 << cfg->pins.ss_pin); // Pull-up no SS entrada
    }
}

/**
 * @brief Sincroniza as configurações da struct com os registradores SPCR e SPSR.
 * @internal
 */
void spi_hw_apply_config(void) {
    uint8_t spcr_val = (1 << SPE); // Habilita o hardware SPI

    if (spi.config.mode == SPI_MODE_MASTER) spcr_val |= (1 << MSTR);
    if (spi.config.bit_order == SPI_ORDER_LSB_FIRST) spcr_val |= (1 << DORD);
    if (spi.config.cpol) spcr_val |= (1 << CPOL);
    if (spi.config.cpha) spcr_val |= (1 << CPHA);

    uint8_t spr = 0;
    bool spi2x = false;

    // Mapeamento do divisor de clock para os bits SPR0, SPR1 e SPI2X
    switch (spi.config.clock_div) {
        case SPI_CLOCK_DIV_2:   spi2x = true; spr = 0; break;
        case SPI_CLOCK_DIV_4:   spr = 0; break;
        case SPI_CLOCK_DIV_16:  spr = 1; break;
        case SPI_CLOCK_DIV_64:  spr = 2; break;
        case SPI_CLOCK_DIV_128: spr = 3; break;
    }

    spcr_val |= (spr << SPR0);
    SPCR = spcr_val;

    if (spi2x) SPSR |= (1 << SPI2X);
    else SPSR &= ~(1 << SPI2X);

    if (spi.config.use_interrupt) SPCR |= (1 << SPIE);
}

/**
 * @brief Baixa o pino de Chip Select do escravo especificado.
 * @param id ID do escravo (0 a SPI_MAX_SLAVES-1).
 */
void spi_hw_select_slave(uint8_t id) {
    if (id >= SPI_MAX_SLAVES || spi.slaves[id].port == NULL) return;
    *(spi.slaves[id].port) &= ~(spi.slaves[id].pin_mask); // CS = LOW
    spi.current_slave_id = id;
}

/**
 * @brief Sobe o pino de Chip Select do escravo atual.
 */
void spi_hw_deselect_slave(void) {
    if (spi.current_slave_id >= SPI_MAX_SLAVES || spi.slaves[spi.current_slave_id].port == NULL) return;
    *(spi.slaves[spi.current_slave_id].port) |= spi.slaves[spi.current_slave_id].pin_mask; // CS = HIGH
    spi.current_slave_id = 255u;
}

/**
 * @brief Inicia o pipeline de transmissão carregando o primeiro byte no SPDR.
 * @return true se um byte foi disparado para o hardware.
 */
bool spi_hw_start_tx_pipeline(void) {
    uint8_t byte_to_send;
    if (!spi_tx_dequeue(&byte_to_send)) return false;
    SPI_HW_WRITE_DATA(byte_to_send);
    spi_state = SPI_STATE_TX_ACTIVE;
    spi.busy = true;
    return true;
}

/**
 * @brief Recupera o hardware de uma colisão de escrita (WCOL).
 * @internal
 */
void spi_hw_recover_collision(void) {
    // Limpa flag WCOL lendo SPSR e SPDR
    (void)SPSR;
    (void)SPI_HW_READ_DATA();
    // Pode tentar reiniciar pipeline ou apenas sinalizar erro
    spi_state = SPI_STATE_ERROR_COLLISION;
}

/* ============================================================================
   API Pública
   ========================================================================== */
bool spi_init(const spi_config_t *cfg) {
    if (!cfg) return false;
    spi.config = *cfg;
    // Reset estado e buffers    
    spi_tx_flush();
    spi_rx_flush();
    
    spi_hw_init_pins(cfg);
    spi_hw_apply_config();
    return true;
}

void spi_register_slave(uint8_t id, volatile uint8_t *port, uint8_t pin) {
    if (id >= SPI_MAX_SLAVES) return;
    spi.slaves[id].port = port;
    spi.slaves[id].pin_mask = (1u << pin);
    *port |= (1u << pin);       // CS inicia em HIGH
    *(port - 1) |= (1u << pin); // Configura DDR como saída
}

void spi_select_device(uint8_t id) {
    if (spi.config.mode == SPI_MODE_MASTER) spi_hw_select_slave(id);
}

void spi_deselect_device(void) {
    if (spi.config.mode == SPI_MODE_MASTER) spi_hw_deselect_slave();
}

uint8_t spi_transfer_blocking(uint8_t data) {
    if (spi.config.use_interrupt || spi.busy) return 0xFF;
    uint16_t timeout = spi.timeout_default;

    SPI_HW_WRITE_DATA(data);

    // Polling da flag SPIF com proteção de timeout
    while (!(SPSR & (1 << SPIF)) && timeout--);
    return (timeout == 0) ? 0xFF : SPI_HW_READ_DATA();
}

bool spi_write_async(uint8_t data) {
    if (!spi.config.use_interrupt) return false;

    // Bloco atômico para evitar corrupção dos índices do buffer
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        uint8_t head = spi.tx_head;
        uint8_t tail = spi.tx_tail;
        uint8_t used = (head >= tail) ? (head - tail) : (SPI_TX_BUFFER_SIZE - tail + head);

        if (used >= (SPI_TX_BUFFER_SIZE - 1)) {
            spi.tx_overflow = true;
            return false;
        }
        spi.tx_buffer[spi.tx_head] = data;
        spi.tx_head = spi_buf_next(spi.tx_head, SPI_TX_BUFFER_SIZE);
    }

    // Se o driver estiver parado, inicia a transmissão imediatamente
    if (spi_state == SPI_STATE_IDLE) {
        if (spi.current_slave_id < SPI_MAX_SLAVES) {
            spi_hw_select_slave(spi.current_slave_id);
        }
        spi_hw_start_tx_pipeline();
    }
    return true;
}

bool spi_write_multi(const uint8_t *buf, uint8_t len, bool keep_cs_low) {
    if (!spi.config.use_interrupt || spi.busy || len == 0) return false;
    
    // Salva estado de CS antes de enfileirar
    spi.multi_cs_active = keep_cs_low;

    for (uint8_t i = 0; i < len; i++) {
        if (!spi_write_async(buf[i])) {
            // Se falhar no meio, opcionalmente limpamos tudo
            spi_tx_flush(); 
            return false;
        }
    }
    return true;
}

uint8_t spi_rx_available(void) {
    return (uint8_t)(spi.rx_head - spi.rx_tail);
}

bool spi_rx_read(uint8_t *byte) {
    if (spi.rx_head == spi.rx_tail) return false;
    *byte = spi.rx_buffer[spi.rx_tail];
    spi.rx_tail = spi_buf_next(spi.rx_tail, SPI_RX_BUFFER_SIZE);
    return true;
}

void spi_rx_flush(void) {
    spi.rx_head = spi.rx_tail = 0;
    spi.rx_overflow = false;
}

uint8_t spi_tx_available(void) {
    return (uint8_t)(spi.tx_head - spi.tx_tail);
}

void spi_tx_flush(void) {
    spi.tx_head = spi.tx_tail = 0;
    spi.tx_overflow = false;
    spi_state = SPI_STATE_IDLE;
    spi.busy = false;
}

bool spi_is_busy(void) {
    return spi.busy || spi_state != SPI_STATE_IDLE || spi_tx_available() > 0;
}

bool spi_had_overflow(void) {
    return spi.tx_overflow || spi.rx_overflow;
}

void spi_slave_preload(uint8_t dummy) {
    if (spi.config.mode == SPI_MODE_SLAVE) SPI_HW_WRITE_DATA(dummy);
}

bool spi_tx_has_space(uint8_t count) {
    uint8_t avail = SPI_TX_BUFFER_SIZE - 1 - spi_tx_available();
    return avail >= count;
}

/**
 * @brief Altera dinamicamente o divisor de clock do SPI com segurança atômica.
 * 
 * @details Esta função garante que a mudança de velocidade não ocorra durante 
 *          uma transmissão ativa, o que causaria corrupção de dados ou 
 *          comportamento errático no escravo.
 * 
 * @param[in] new_div Novo divisor de clock baseado em @ref spi_clock_div_t.
 */
void spi_set_clock(spi_clock_div_t new_div) {
    // 1. Segurança: Aguarda qualquer transmissão em background terminar.
    while (spi_is_busy());

    // 2. Cálculo dos bits conforme o enum definido em spi_types.h
    uint8_t spr_bits = 0;
    bool double_speed = false;

    switch (new_div) {
        case SPI_CLOCK_DIV_2:   double_speed = true; spr_bits = 0; break;
        case SPI_CLOCK_DIV_4:   spr_bits = 0; break;
        case SPI_CLOCK_DIV_16:  spr_bits = 1; break;
        case SPI_CLOCK_DIV_64:  spr_bits = 2; break;
        case SPI_CLOCK_DIV_128: spr_bits = 3; break;
    }

    // 3. SEÇÃO CRÍTICA: Aplicação atômica nos registradores de hardware
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        // Atualiza a cópia na struct global para manter consistência
        spi.config.clock_div = new_div;

        // Limpa apenas os bits SPR1 e SPR0 (bits 1 e 0 do SPCR) e aplica os novos
        SPCR = (SPCR & ~((1 << SPR1) | (1 << SPR0))) | (spr_bits & 0x03);

        // Ajusta o bit SPI2X no SPSR (bit 0)
        if (double_speed) {
            SPSR |= (1 << SPI2X);
        } else {
            SPSR &= ~(1 << SPI2X);
        }
    }
}

/* ============================================================================
   ISR (Motor da Biblioteca)
   ========================================================================== */

/**
 * @brief Interrupt Service Routine para o término de transferência SPI.
 * 
 * @details Este é o motor da biblioteca no modo assíncrono. A cada byte 
 *          concluído, a ISR:
 *          1. Lê o byte recebido e o enfileira no buffer RX.
 *          2. Executa o callback do usuário (se registrado).
 *          3. Verifica se há mais bytes no buffer TX para enviar.
 *          4. Gerencia o pino de Chip Select (CS) caso a transação termine.
 *          5. Monitora e recupera colisões de escrita (WCOL).
 */
ISR(SPI_STC_vect) {
    // 1. Captura imediata do dado recebido
    uint8_t received = SPI_HW_READ_DATA();
    bool rx_ok = spi_rx_enqueue(received);
    
    // 2. Notificação da aplicação via callback
    if (spi.config.rx_callback) spi.config.rx_callback(received, !rx_ok);

    // 3. Gerenciamento do fluxo de transmissão (Pipeline)
    uint8_t next_byte;
    if (spi_tx_dequeue(&next_byte)) {
        // Envia o próximo byte da fila
        SPI_HW_WRITE_DATA(next_byte);
        spi_state = SPI_STATE_TX_ACTIVE;
    } else {
        // Fila vazia: encerra a transação
        spi_state = SPI_STATE_IDLE;
        spi.busy = false;
        // Desativa o escravo apenas se não for uma transação multi-byte mantida
        if (!spi.multi_cs_active) spi_hw_deselect_slave();
    }
    // 4. Tratamento de erros de colisão de hardware
    if (SPI_HW_WRITE_COLLISION_DETECTED()) spi_hw_recover_collision();
}
