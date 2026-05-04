# Biblioteca SPI para AVR

![License](https://img.shields.io/badge/license-MIT-green)
![AVR](https://img.shields.io/badge/MCU-AVR-blue)
![Language](https://img.shields.io/badge/language-C-darkblue)
![Compiler](https://img.shields.io/badge/compiler-XC8-orange)

Uma biblioteca C robusta, leve e documentada para comunicação SPI em microcontroladores AVR. Projetada para suportar desde inicializações simples até sistemas complexos baseados em interrupção.

## Características

*   **Modos:** Master e Slave suportados.
*   **Assíncrona:** Gerenciamento por interrupção com buffers circulares (lock-free).
*   **Síncrona:** Funções bloqueantes com proteção de timeout para inicialização.
*   **Multi-Slave:** Gerenciamento integrado de até 4 dispositivos (expansível).
*   **Otimizada:** Avanço de buffers via operações bitwise (tamanhos potência de 2).
*   **Documentada:** Totalmente compatível com Doxygen e diagramas Graphviz.

## Estrutura do Projeto

*   `spi.h`: Interface pública principal.
*   `spi_types.h`: Definições de estruturas, enums e configurações.
*   `spi_internal.h`: Macros de hardware e funções inline privadas.
*   `spi.c`: Implementação da lógica e Máquina de Estados (ISR).

## Como usar (Exemplo Rápido)

<pre><code class="language-c">
#include "spi.h"

void setup() {
    spi_config_t cfg = {
        .mode = SPI_MODE_MASTER,
        .clock_div = SPI_CLOCK_DIV_16,
        .use_interrupt = true,
        .pins = { &PORTB, PB3, PB4, PB5, PB2 }
    };
    
    spi_init(&cfg);
    spi_register_slave(0, &PORTB, PB1); // Escravo no pino PB1
}

void loop() {
    uint8_t data = 0xAA;
    spi_select_device(0);
    spi_write_async(data);
    // ... o driver cuida do resto via ISR
}
</code></pre>

## Documentação (Doxygen)

Se você tem o **Doxygen** e o **Graphviz** instalados:

1. Execute `doxygen Doxyfile`.
2. Abra `html/index.html`.

A documentação incluirá diagramas de colaboração e dependência de arquivos gerados automaticamente pelo `dot`.

## Licença

Este projeto está licenciado sob a Licença MIT - veja o arquivo [LICENSE](LICENSE) para detalhes.
