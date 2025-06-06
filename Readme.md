# Dashboard Digital ESP32 com Display TFT

## Visão Geral

Este projeto implementa um dashboard digital multifuncional para ESP32, utilizando um display TFT (controlado pela biblioteca `TFT_eSPI`) para exibir informações em tempo real como RPM, velocidade, marcha engatada e status de sistemas como ABS e Controle de Tração (TC). O sistema é projetado para receber dados via comunicação serial, tornando-o ideal para uso com simuladores de corrida, projetos de telemetria veicular ou qualquer aplicação que necessite de uma interface gráfica para dados dinâmicos.

O projeto utiliza FreeRTOS para gerenciar tarefas concorrentes: uma para a leitura e processamento de dados seriais (executando no Core 0) e outra para a atualização do display (executando no Core 1), garantindo uma interface responsiva e processamento de dados eficiente.

## Recursos Principais

*   **Visualização em Tempo Real:** Exibe RPM, velocidade, marcha, status do ABS e TC.
*   **Barra de RPM Dinâmica:**
    *   Gradiente de cores (verde, amarelo, vermelho) acompanhando o RPM.
    *   Transição suave entre as cores.
    *   **Indicação de RPM Máximo:** A barra inteira fica vermelha sólida quando o RPM atinge ou excede o limite máximo configurado.
*   **Indicadores Luminosos:**
    *   TC (Controle de Tração) e ABS com indicação de status:
        *   Cinza escuro: Desconhecido / Não disponível.
        *   Branco: Disponível / Não ativo.
        *   Vermelho Piscante: Ativo.
*   **Configuração Dinâmica:** Limites de RPM (máximo, início da faixa amarela, início da faixa vermelha) podem ser configurados em tempo real via comando serial.
*   **Timeout de Dados:** Se nenhum dado novo for recebido pela porta serial por 3 segundos, as informações no display são zeradas (RPM, velocidade, marcha para Neutro, indicadores para "desconhecido").
*   **Multitarefa com FreeRTOS:** Processamento de dados e atualização de display em cores separados para melhor desempenho e responsividade.
*   **Layout Otimizado:** Elementos posicionados para clara legibilidade em um display de 240x240 pixels.
*   **Fontes Customizadas:** Utiliza fontes GFX para uma aparência profissional.

## Requisitos de Hardware

1.  **Microcontrolador ESP32:** Qualquer placa de desenvolvimento ESP32 (ex: ESP32 DevKitC, NodeMCU-32S).
2.  **Display TFT:**
    *   Interface SPI.
    *   Resolução de 240x240 pixels (o código está configurado para esta resolução).
    *   Controlador compatível com a biblioteca `TFT_eSPI` (ex: ST7789).
3.  **Fios de Conexão (Jumpers):** Para conectar o display ao ESP32.
4.  **Protoboard (Opcional):** Para facilitar as conexões.
5.  **Fonte de Dados Serial:** Um computador executando um simulador que envie dados via serial, outro microcontrolador, ou qualquer dispositivo capaz de enviar os comandos seriais formatados.
6.  **Cabo USB:** Para programação do ESP32 e, opcionalmente, para comunicação serial com o PC.

## Requisitos de Software

1.  **Arduino IDE:** Configurado para desenvolvimento com ESP32.
2.  **Plataforma ESP32 para Arduino IDE:** Instalada através do Gerenciador de Placas.
3.  **Bibliotecas Arduino:**
    *   **`TFT_eSPI` por Bodmer:** Essencial para controle do display TFT. Deve ser instalada via Gerenciador de Bibliotecas do Arduino IDE.
        *   **IMPORTANTE:** É crucial configurar corretamente o arquivo `User_Setup.h` (ou `User_Setup_Select.h` e um setup específico) dentro da pasta da biblioteca `TFT_eSPI` de acordo com o seu display e as conexões de pinos do ESP32.
    *   **`SPI.h`:** Geralmente incluída com a plataforma ESP32.
4.  **Fontes GFX:** O projeto utiliza as seguintes fontes, que geralmente acompanham a biblioteca `Adafruit_GFX` ou exemplos da `TFT_eSPI`:
    *   `FreeSansBold12pt7b`
    *   `FreeSansBold24pt7b`
    *   `FreeSans24pt7b`
    Certifique-se de que estas fontes estejam acessíveis ao compilador. Se não estiverem, você pode precisar adicioná-las ao seu projeto ou garantir que a biblioteca `TFT_eSPI` esteja configurada para incluí-las.

## Conexões (Exemplo com ST7789)

A configuração exata dos pinos deve ser refletida no arquivo `User_Setup.h` da biblioteca `TFT_eSPI`. Abaixo um exemplo comum:

*   **TFT SCLK (Clock)** -> ESP32 GPIO 18 (HSPI SCK)
*   **TFT MOSI (Data In)** -> ESP32 GPIO 23 (HSPI MOSI)
*   **TFT MISO (Data Out)** -> ESP32 GPIO 19 (HSPI MISO) - *Geralmente não necessário para displays "write-only" como o ST7789, mas pode ser conectado.*
*   **TFT CS (Chip Select)** -> ESP32 GPIO 5 (HSPI CS0)
*   **TFT DC (Data/Command)** -> ESP32 GPIO 2
*   **TFT RST (Reset)** -> ESP32 GPIO 4
*   **TFT BLK/LED (Backlight)** -> ESP32 GPIO (ex: 22) ou diretamente a 3.3V (se não precisar de controle de brilho por software).
*   **TFT VCC** -> ESP32 3.3V
*   **TFT GND** -> ESP32 GND

**Atenção:** Verifique o datasheet do seu display e ajuste os pinos no `User_Setup.h` da `TFT_eSPI`!

## Instalação e Configuração

1.  **Clone ou Baixe o Repositório:** Obtenha os arquivos do projeto.
2.  **Abra no Arduino IDE:** Abra o arquivo `.ino` principal.
3.  **Instale as Bibliotecas:**
    *   Vá em `Ferramentas > Gerenciar Bibliotecas...`
    *   Procure por `TFT_eSPI` e instale a biblioteca de Bodmer.
4.  **Configure a `TFT_eSPI`:**
    *   Navegue até a pasta de bibliotecas do Arduino (geralmente em `Documentos/Arduino/libraries/TFT_eSPI`).
    *   Edite o arquivo `User_Setup.h` (ou crie/selecione um setup específico conforme as instruções da biblioteca).
    *   Descomente (ou defina) o driver correto para o seu display (ex: `#define ST7789_DRIVER`).
    *   Defina os pinos do ESP32 conectados ao seu display (TFT_MOSI, TFT_SCLK, TFT_CS, TFT_DC, TFT_RST).
    *   Defina a largura e altura do display se necessário (já está como 240x240 no código).
    *   Salve o arquivo.
5.  **Verifique as Fontes:** Assegure-se de que as fontes GFX mencionadas (`FreeSansBold...`) estejam disponíveis. Elas são declaradas como `extern const GFXfont ...;`.
6.  **Selecione a Placa e Porta:**
    *   No Arduino IDE, vá em `Ferramentas > Placa` e selecione o modelo correto do seu ESP32.
    *   Em `Ferramentas > Porta`, selecione a porta COM à qual o ESP32 está conectado.
7.  **Compile e Carregue:** Clique no botão "Carregar" (seta para a direita) no Arduino IDE.
8.  **Abra o Monitor Serial:** Configure para a taxa de `115200` baud para ver mensagens de depuração e confirmar a operação.

## Como Usar / Formato dos Dados Seriais

O ESP32 espera receber comandos via comunicação serial na taxa de **115200 baud**. Existem dois tipos de comandos:

1.  **Comando de Configuração (`SETUP`):**
    *   Formato: `SETUP:max_rpm,rpm_amarelo_inicio,rpm_vermelho_inicio\n`
    *   Exemplo: `SETUP:8000,4000,6000\n`
    *   Descrição:
        *   `max_rpm`: O valor máximo de RPM que o motor/simulação atinge. Usado para escalar a barra de RPM e para a condição de barra totalmente vermelha.
        *   `rpm_amarelo_inicio`: O valor de RPM onde a barra começa a transicionar para a cor amarela.
        *   `rpm_vermelho_inicio`: O valor de RPM onde a barra começa a transicionar para a cor vermelha (e se torna totalmente vermelha ao atingir `max_rpm`).
    *   Este comando atualiza os parâmetros da barra de RPM e força um redesenho de elementos dependentes.

2.  **Comando de Dados (`DATA`):**
    *   Formato: `DATA:rpm,velocidade,marcha,estado_abs,estado_tc\n`
    *   Exemplo: `DATA:3500,120,3,0,1\n`
    *   Descrição:
        *   `rpm`: Valor atual do RPM (inteiro).
        *   `velocidade`: Velocidade atual (inteiro, ex: km/h ou mph).
        *   `marcha`: Marcha atual (inteiro):
            *   `-1`: Ré (R)
            *   `0`: Neutro (N)
            *   `1` a `9`: Marchas à frente.
        *   `estado_abs`: Estado do sistema ABS (inteiro):
            *   `-1`: Desconhecido / Sistema desligado / Não aplicável (mostra "ABS" em cinza).
            *   `0`: Sistema ABS disponível, mas não atuando (mostra "ABS" em branco).
            *   `1`: Sistema ABS atuando (mostra "ABS" piscando em vermelho).
        *   `estado_tc`: Estado do Controle de Tração (inteiro):
            *   `-1`: Desconhecido / Sistema desligado / Não aplicável (mostra "TC" em cinza).
            *   `0`: Sistema TC disponível, mas não atuando (mostra "TC" em branco).
            *   `1`: Sistema TC atuando (mostra "TC" piscando em vermelho).
    *   Este comando atualiza as informações exibidas no dashboard. Se nenhum comando `DATA:` for recebido por 3 segundos, os valores no display são zerados.

**Observação:** Todos os comandos devem terminar com um caractere de nova linha (`\n`). O caractere de retorno de carro (`\r`) é ignorado.

## Estrutura do Código (Visão Geral)

*   **`setup()`:**
    *   Inicializa a comunicação serial.
    *   Cria o mutex (`xDataMutex`) para sincronização de dados entre as tasks.
    *   Inicializa o `lastDataReceiveTime` para o timeout.
    *   Cria e pina as tasks `displayTask` (Core 1, prioridade 2) e `serialTask` (Core 0, prioridade 1).
*   **`loop()`:**
    *   Praticamente vazia, pois a lógica principal é gerenciada pelas tasks do FreeRTOS. Contém apenas um `vTaskDelay` para ceder tempo.
*   **`serialTask(void *pvParameters)`:**
    *   Executa no Core 0.
    *   Lê caracteres da porta serial.
    *   Monta as linhas de comando.
    *   Faz o parsing dos comandos `SETUP:` e `DATA:`.
    *   Atualiza as variáveis globais voláteis (`currentRPM`, `vRpmMax`, etc.) protegidas pelo mutex.
    *   Atualiza `lastDataReceiveTime` ao receber dados válidos.
*   **`displayTask(void *pvParameters)`:**
    *   Executa no Core 1.
    *   Inicializa o display TFT (`tft.init()`, `tft.setRotation()`).
    *   Loop principal de renderização:
        *   Gerencia o estado de piscar (`blinkState`) para os indicadores.
        *   Verifica o timeout de dados: se `millis() - lastDataReceiveTime > DATA_TIMEOUT_MS`, zera as variáveis de dados (`currentRPM`, `currentSpeed`, etc.).
        *   Copia os dados voláteis para variáveis locais (protegido por mutex).
        *   Verifica se os parâmetros de RPM (`vRpmMax`, etc.) foram alterados e chama `updateRpmDerivedParametersDisplay()` se necessário.
        *   Chama `updateAndDrawBackgroundDisplay()` se o fundo precisar ser redesenhado.
        *   Chama as funções de desenho específicas para cada elemento da UI (`drawRpmTextDisplay`, `drawHorizontalRpmBar`, etc.), passando os valores atuais e anteriores para otimizar o redesenho.
        *   Atualiza as variáveis `prev...` com os valores atuais.
        *   Controla a taxa de atualização do frame (aproximadamente 60 FPS).
*   **Funções de Desenho (`draw...`)**:
    *   `drawHorizontalRpmBar()`: Lógica complexa para desenhar a barra de RPM, incluindo o gradiente e a condição de vermelho sólido no RPM máximo.
    *   Outras funções (`drawRpmTextDisplay`, `drawSpeedDisplay`, `drawGearDisplay`, `drawIndicatorDisplay`): Responsáveis por desenhar texto e indicadores, limpando apenas a área necessária antes de redesenhar para evitar flickering.
*   **Funções Auxiliares:**
    *   `updateRpmDerivedParametersDisplay()`: Chamada quando os parâmetros de RPM mudam, força o redesenho.
    *   `updateAndDrawBackgroundDisplay()`: Limpa e redesenha o fundo da tela.
    *   `interpolateColor()`: Calcula uma cor intermediária entre duas cores, usado para o gradiente da barra de RPM.
*   **Variáveis Globais e Mutex:**
    *   Variáveis `volatile` (ex: `currentRPM`, `vRpmMax`) são usadas para compartilhar dados entre as tasks.
    *   `xDataMutex` (SemaphoreHandle_t) é usado para proteger o acesso a essas variáveis compartilhadas, evitando condições de corrida.

## Solução de Problemas Comuns

*   **Display em Branco ou Cores Estranhas:**
    *   Verifique as conexões de hardware entre o ESP32 e o display.
    *   **A causa mais comum:** Configuração incorreta do `User_Setup.h` na biblioteca `TFT_eSPI`. Certifique-se de que o driver do display e os pinos SPI, CS, DC, RST estão corretos.
    *   Verifique a alimentação do display (3.3V).
*   **Nada Acontece / Sem Saída Serial:**
    *   Verifique se a placa e a porta COM corretas estão selecionadas no Arduino IDE.
    *   Verifique se o ESP32 está recebendo alimentação adequada.
    *   O Monitor Serial do Arduino IDE deve estar configurado para `115200` baud.
*   **Dados Não Atualizam no Display:**
    *   Verifique se a fonte de dados está enviando os comandos seriais corretamente formatados (incluindo `\n` no final).
    *   Confirme se a taxa de baud da fonte de dados e do ESP32 (115200) coincidem.
    *   Verifique as mensagens no Monitor Serial do ESP32 para erros de parsing (`Erro parse DATA` ou `Erro parse SETUP`).
*   **Display Piscando Excessivamente (Flickering):**
    *   O código tenta minimizar o flickering redesenhando apenas as áreas alteradas. Se o flickering for intenso, pode haver chamadas de `tft.fillScreen()` desnecessárias ou áreas de limpeza (`fillRect` com a cor de fundo) maiores que o necessário.

## Possíveis Melhorias Futuras

*   **Múltiplos Layouts:** Permitir alternar entre diferentes estilos de dashboard.
*   **Configuração via Wi-Fi/Bluetooth:** Usar uma interface web ou app para configurar limites de RPM e outras opções.
*   **Log de Dados:** Salvar dados recebidos no cartão SD ou memória flash interna.
*   **Shift Light Mais Elaborado:** Implementar um padrão de LEDs externos ou animações no display para indicar o momento ideal de troca de marcha.
*   **Suporte a Outros Protocolos:** Adicionar suporte para CAN bus para integração direta com veículos reais.
*   **Interface Gráfica de Configuração:** Um pequeno menu no próprio display para ajustes básicos.

## Licença

Este projeto é distribuído sob a licença MIT. Veja o arquivo `LICENSE` para mais detalhes (se aplicável, ou adicione uma seção de licença conforme sua preferência).