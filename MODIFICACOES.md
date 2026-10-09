# Modificações em relação ao OpenHantek original

Este repositório é uma **versão modificada** do OpenHantek
(<https://github.com/OpenHantek/openhantek>, commit `836cd98`, “Update build.md (#310)”).

- Modificado por: **Daniel Crestani**, com assistência do Claude (Anthropic)
- Período das modificações: **outubro de 2026**
- Licença: GNU GPL versão 3 ou posterior (veja [COPYING](COPYING)), a mesma do projeto original.

Cada alteração está num commit separado, a partir de `836cd98`. Resumo por área:

## Suporte ao Hantek DSO-2250
- Índice do tamanho de registro corrigido; sinais iniciais da aquisição reativados.
- Corrigido o travamento “Sample count too high” e o cálculo do *downsampler* ao mudar o tempo/div.
- Relés de ganho na sequência correta (obtida por engenharia reversa do `SDK2250.dll` do fabricante):
  todas as escalas de 10 mV/div a 5 V/div funcionam.
- Atenuação de ponteira tratada no núcleo: o ganho do hardware é dividido pela atenuação e as amostras,
  o nível de disparo e as medições ficam na ponta da ponteira.
- Calibração de zero dos canais (erro do DAC de posição por V/div e posição), salva e aplicada às amostras e ao
  nível do trigger.
- Filtro de disparo do DSO-2250 (comando `0x00 0x0f`, bit do trigger — igual ao `dsoSetFilt` do SDK) para a
  rejeição de altas frequências.

## Tela e processamento
- Alinhamento do disparo com precisão de subamostra.
- Desenho só da janela visível, com detecção de pico (mín./máx.) quando há mais amostras que pixels;
  interpolação sen(x)/x (Lanczos).
- Analisador de espectro reescrito: módulo correto da FFT (o original desenhava partes reais e imaginárias
  misturadas), escala em dBV corrigida pelo ganho da janela, nível independente da janela, média de potência,
  retenção de pico, preenchimento com zeros, marcador da fundamental, harmônicos e THD; janelas Gauss e
  Bartlett-Hann corrigidas.
- Marcação dos harmônicos e escalas dos eixos desenhadas sobre a tela do espectro.

## Interface
- Painel frontal (dock) estilo osciloscópio, com seções recolhíveis; RUN/STOP, SINGLE, AUTOSET e FORCE na
  barra de ferramentas.
- Ponteiras/sondas diferenciais x20, x200, x500 e x1000; escalas em kV.
- Garras de corrente Hantek CC-65 (100 mV/A e 10 mV/A) e CC-650 (10 mV/A e 1 mV/A) com unidade em ampères.
- Rodapé de medições por canal, menu Medições, menu Cursores com leitura no rodapé.
- Menus, configurações e mensagens em português; cores aplicadas sem reiniciar; seletor de cor do Qt
  (o seletor nativo travava a janela de configurações).
- Janelas antigas (Horizontal, Trigger, Voltage, Spectrum) escondidas: as funções estão no painel.
- Páginas “Analysis” e “Scope” das configurações substituídas pela página “Tela”; cores de impressão ocultas.

## Qt 6
- Projeto migrado para **Qt 6** (6.2 ou mais novo); Qt 5.15 continua suportado com `-DOPENHANTEK_QT5=ON`.
  A última versão só em Qt 5 está na tag `v1.0-qt5`.
- CMake: módulo `OpenGLWidgets`, C++17 e funções do Qt sem número de versão (traduções e recursos).
- Código: `QButtonGroup::idClicked`/`idPressed` no lugar dos sinais removidos `buttonClicked(int)` e
  `buttonPressed(int)`; `QPalette::Window`; `Qt::SkipEmptyParts`; `QString::asprintf`; atalhos com `|`;
  `QLibraryInfo::path` e `QTextStream::setEncoding` no Qt 6; `Qt::WindowFlags()` como padrão.
- Sem avisos de funções obsoletas no Qt 6 (posição do mouse com `position()`, atributos de High-DPI
  aplicados só no Qt 5).
- Ao fechar, o programa não termina mais com “Abortado (imagem do núcleo gravada)”: na libusb 1.0.25
  (Ubuntu/Pop!_OS 22.04) o `libusb_exit()` dispara uma asserção interna e deixa de ser chamado nessa versão.
- Repositório renomeado de `OpenHantek-Fork-DSO2250` para `OpenHantek-DSO2250`.

## Três programas: OpenHantek, PSG9080 e OpenHantekBode
- Núcleo do osciloscópio (USB, driver do DSO-2250, protocolo, seleção do aparelho, firmwares) separado na
  biblioteca `openhantek_core`, usada pelo OpenHantek e pelo OpenHantekBode; correções no driver valem para os
  dois. O OpenHantek continua só osciloscópio e FFT.
- Protocolo do PSG9080 em C++ sem Qt (`src/generator/psg9080protocol.*`), com as escalas verificadas no
  aparelho pelo driver pypsgctrl (inclusive as unidades mHz/µHz); comunicação com `QSerialPort`
  (`src/generator/psg9080.*`), descartando bytes antigos antes de cada comando; painel dos canais
  (`GeneratorPanel`) e presets compatíveis com o psg-gui em Python (`psg9080presets.*`).
- **PSG9080** (`src/apps/psg9080*`): programa do gerador em Qt/C++.
- **OpenHantekBode** (`src/apps/bode_main.cpp`, `src/bode/*`): varredura logarítmica, duração da aquisição e
  escalas automáticas, aquisições numeradas (`SampleTap`) para ignorar as antigas com exatidão, média por
  ponto, DFT de uma frequência com janela de Hann e refinamento de frequência, subamostragem coerente acima de
  Nyquist, calibração com as duas ponteiras no mesmo ponto, visualização ao vivo, gráfico ganho/fase, CSV e
  imagem.
- Testes de unidade das partes sem Qt (`openhantek/tests`, `-DOPENHANTEK_TESTS=ON`) e compilação automática no
  GitHub Actions (Ubuntu 22.04, Qt 6.2).
- Nova dependência: Qt SerialPort (`libqt6serialport6-dev`).

## Arquivos
- Removidos os exportadores CSV, Impressão e Imagem/PDF antigos (`src/exporting/*`, exceto
  `exportsettings.h`).
- Novo: Exportar → Imagem da tela (PNG/JPG/BMP) e cópia para a área de transferência.
- Novo: registro de dados por canal (`src/datalogger.*`) com início manual ou por trigger.
- Diálogo “Sobre” com os créditos do projeto original, os autores das modificações e o aviso da licença.
