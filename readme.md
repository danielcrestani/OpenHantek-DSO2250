# OpenHantek – edição DSO-2250

Versão modificada do [OpenHantek](https://github.com/OpenHantek/openhantek) focada no osciloscópio USB
**Hantek DSO-2250** no Linux (desenvolvida e usada no Pop!_OS), com interface em português
parecida com a de um osciloscópio de bancada.

> Este é um *fork* não oficial. O OpenHantek original não é mais mantido para este modelo e tinha vários
> defeitos com o DSO-2250 (escalas trocadas, travamentos, falta de atenuação de ponteira). Todo o crédito do
> projeto base é dos seus autores — veja [Créditos e licença](#créditos-e-licença).

## O que há de novo

**Funcionamento do DSO-2250**
- Relés de ganho e sequência de ganhos corrigidos (engenharia reversa do `SDK2250.dll` do fabricante);
  todas as escalas de 10 mV/div a 5 V/div funcionam.
- Corrigidos: tamanho de registro, travamento “Sample count too high”, sinais iniciais da aquisição,
  cálculo da taxa de amostragem.
- Rejeição de altas frequências no disparo (filtro de trigger do próprio DSO-2250 + filtro em software).
- Alinhamento do disparo com precisão de subamostra: sinais rápidos ficam parados na tela.
- Interpolação sen(x)/x e desenho com detecção de pico (nenhum pico some ao comprimir o tempo).

**Interface**
- Painel frontal estilo osciloscópio (largura fixa, seções recolhíveis e coloridas): por canal LIGADO,
  DC/AC, INV, V/div, posição, ponteira; Horizontal (tempo/div, pré-disparo, memória); Trigger (AUTO/NORMAL/ÚNICO,
  fonte, borda, REJ. AF, nível, 50 %); FFT.
- RUN/STOP, SINGLE, AUTOSET e FORCE na barra do topo.
- Ponteiras x1/x10/x20/x50/x100/x200/x500/x1000 (inclui sondas diferenciais) e **garras de corrente Hantek CC-65 e CC-650** — a tela, as medições, a FFT e o
  registro passam a mostrar ampères.
- Medições no rodapé, na cor do canal, escolhidas no menu **Medições** (Vpp, máx, mín, média, RMS, RMS AC,
  frequência, período, ciclo ativo, larguras, subida, descida).
- Menu **Cursores**: tempo/frequência, amplitude/nível ou ambos, “Posicionar no sinal”, leitura no rodapé
  (Δt, 1/Δt, ΔV/ΔA, Δf, ΔdB).
- Menus e configurações em português; cores aplicadas na hora.

**Analisador de espectro (FFT)**
- Escala calibrada em dBV (ou dBA com garra), níveis corretos com qualquer janela.
- Janelas Retangular, Hann, Flat-top e Blackman-Harris; média de 4/16/64 aquisições; retenção de pico.
- AUTO FFT, faixa total (0 Hz até Nyquist na tela toda), escalas nos eixos, modo “Só FFT”.
- Marcação na tela da fundamental (F) e dos harmônicos que se destacam do ruído, com lista em dBc e THD.

**Três programas no mesmo projeto**
- **OpenHantek** — osciloscópio e FFT do DSO-2250.
- **PSG9080** — controle completo do gerador de funções Joy-IT / JunTek PSG9080 pela USB, em abas: os dois
  canais (forma de onda, frequência de µHz a 80 MHz, amplitude, offset, duty, fase, saída) com presets;
  modulação (AM, FM, PM, ASK, FSK, PSK, pulso, burst); varredura e VCO do aparelho; frequencímetro e contador da
  entrada Ext.IN; editor de ondas arbitrárias (fórmula, arquivo ou captura do OpenHantek) com envio às 99
  posições do gerador; sequências programadas; sincronismo, memórias e diagnóstico dos registradores.
- **OpenHantekBode** — resposta em frequência: o PSG9080 varre a frequência, o DSO-2250 mede a entrada e a saída
  do circuito e o programa traça ganho (dB) e fase (°) em escala logarítmica. Escalas automáticas, média por
  ponto, calibração com as duas ponteiras no mesmo ponto, visualização ao vivo dos dois canais, CSV e imagem.
  Faixa de cerca de 0,5 Hz a 80 MHz (acima de 50 MHz por subamostragem coerente).

O DSO-2250 e a porta serial do PSG9080 só podem ser usados por um programa de cada vez.

**Arquivos**
- **Exportar → Imagem da tela** (PNG/JPG/BMP) ou copiar para a área de transferência.
- **Registro de dados** por canal (botão ● REG): medições a cada aquisição ou forma de onda, início manual
  ou por trigger, parada por quantidade ou tempo, CSV em português (`;` e vírgula decimal).

Guia completo de uso: **[docs/dso2250-guia.md](docs/dso2250-guia.md)** ·
Lista de modificações: **[MODIFICACOES.md](MODIFICACOES.md)**

## Instalar o pacote pronto (Pop!_OS / Ubuntu 22.04 ou mais novo)

Baixe o `.deb` da [**versão contínua**](https://github.com/danielcrestani/OpenHantek-DSO2250/releases/tag/continuo)
(gerada automaticamente a cada atualização do `main`) e instale:

```sh
sudo apt install ./openhantek-dso2250_*_amd64.deb
```

O apt instala o Qt 6 e as demais bibliotecas. **OpenHantek**, **PSG9080** e **OpenHantek Bode** aparecem no
menu de aplicativos, e a regra do udev já dá acesso ao osciloscópio sem sudo. Para o gerador (porta serial),
o usuário precisa estar no grupo `dialout`: `sudo usermod -aG dialout $USER` e entrar de novo na sessão.
Para atualizar, instale o `.deb` novo do mesmo jeito; para remover: `sudo apt remove openhantek-dso2250`.

## Compilar (Pop!_OS / Ubuntu / Debian)

```sh
sudo apt install g++ cmake qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools \
     libqt6opengl6-dev libqt6serialport6-dev libfftw3-dev binutils-dev libusb-1.0-0-dev \
     mesa-common-dev libgl1-mesa-dev libgles2-mesa-dev
git clone https://github.com/danielcrestani/OpenHantek-DSO2250.git
cd OpenHantek-DSO2250
mkdir build && cd build
cmake .. && make -j$(nproc)
sudo cp ../firmware/60-hantek.rules /lib/udev/rules.d/ && sudo udevadm control --reload-rules
./openhantek/OpenHantek          # osciloscópio e FFT
./openhantek/PSG9080             # gerador
./openhantek/OpenHantekBode      # resposta em frequência
```

Depois de copiar a regra do udev, desconecte e reconecte o DSO-2250. O firmware é enviado pelo próprio
programa na primeira conexão (o aparelho muda de `04b4:2250` para `04b5:2250`).

O projeto usa **Qt 6** (6.2 ou mais novo). A última versão em Qt 5 está na tag `v1.0-qt5`; para
compilar com Qt 5.15 a partir deste código, use `cmake -DOPENHANTEK_QT5=ON ..`

Para gerar o pacote a partir da compilação: `cpack -G DEB` dentro de `build` (o `.deb` fica em `build/packages`).

> **Segurança:** o DSO-2250 **não é isolado** — o terra das ponteiras é o terra do computador. Não meça a rede
> elétrica diretamente; use ponteira diferencial, transformador isolador ou garra de corrente.

## Créditos e licença

- **Projeto original:** OpenHantek — Copyright © 2010, 2011 Oliver Haag; Copyright © 2012–2017 comunidade
  OpenHantek (David Gräff e colaboradores). <https://github.com/OpenHantek/openhantek>
- **Modificações (2026):** Daniel Crestani, com assistência do Claude (Anthropic).

Este programa é software livre, distribuído sob a **GNU General Public License versão 3 ou posterior**
(arquivo [COPYING](COPYING)), sem nenhuma garantia. As alterações em relação ao original estão descritas em
[MODIFICACOES.md](MODIFICACOES.md) e no histórico de commits; o histórico completo do projeto original foi
preservado.

---

<details>
<summary><b>README original do OpenHantek (inglês)</b> — algumas funções descritas ali foram alteradas nesta versão</summary>

# OpenHantek [![Build Status](https://travis-ci.org/OpenHantek/openhantek.svg?branch=master)](https://travis-ci.org/OpenHantek/openhantek) [![Build status](https://ci.appveyor.com/api/projects/status/github/openhantek/openhantek?branch=master&svg=true)](https://ci.appveyor.com/project/openhantek/openhantek/branch/master) [![Stability: Unsupported](https://masterminds.github.io/stability/unsupported.svg)](https://masterminds.github.io/stability/unsupported.html)

OpenHantek is a free software for Hantek and compatible (Voltcraft/Darkwire/Protek/Acetech) USB digital signal oscilloscopes.

<table><tr>
    <td> <img alt="Image of main window on linux" width="100%" src="docs/images/screenshot_mainwindow.png"> </td>
    <td> <img alt="Image of main window on Windows" width="100%" src="docs/images/screenshot_mainwindow_win.png"> </td>
</tr></table>

* Supported operating systems: Linux, MacOSX, Windows¹, Android
* Supported devices: DSO2xxx Series, DSO52xx Series, 6022BE/BL

## Features

* Digital phosphor effect to notice even short spikes
* Voltage and Spectrum view for all device supported chanels
* Math channel with these modes: Ch1+Ch2, Ch1-Ch2
* Freely configurable colors
* Export to CSV, JPG, PNG or print the graphs
* Supports hardware and software triggered devices
* A zoom view with a freely selectable range
* All settings can be saved to a configuration file and loaded again
* Multiple instances with a different device each can be started
* The dock views on the main window can be customized by dragging them around and stacking them.
  This allows a minimum window size of 640*480 for old workstation computers.

## Install prebuilt binary
Navigate to the [Releases](https://github.com/OpenHantek/openhantek/releases) page.
* [Download Windows build](https://ci.appveyor.com/project/openhantek/openhantek/branch/master/artifacts)

## Building OpenHantek from source
You need the following software, to build OpenHantek from source:
* [CMake 3.5+](https://cmake.org/download/)
* [Qt 6.2+](https://www.qt.io/download-open-source) (Qt 5.15 with `-DOPENHANTEK_QT5=ON`)
* [FFTW 3+ (prebuild files will be downloaded on windows)](http://www.fftw.org/)
* libusb 1.x (prebuild files will be used on windows)
* A compiler that supports C++17

We have build instructions available for [Linux](docs/build.md#linux), [Apple MacOSX](docs/build.md#apple) and [Microsoft Windows](docs/build.md#windows).

## Run OpenHantek
You need an OpenGL 3.2+ or OpenGL ES 2.0+ capable graphics hardware for OpenHantek.
OpenGL is prefered, if available. Overwrite this behaviour by starting OpenHantek
from the command line like this: `OpenHantek --useGLES`.

USB access for the device is required:
* As seen on the [Microsoft Windows build instructions](docs/build.md#windows) page, you need a
special driver for Windows systems.
* On Linux, you need to copy the file `firmware/60-hantek.rules` to `/lib/udev/rules.d/` and replug your device.

## Specifications, Features and limitations
Please refer to the [Specifications, Features, Limitations](docs/limitations.md) page.

## Contribute
We welcome any reported Github Issue if you have a problem with this software. Send us a pull request for enhancements and fixes. Some random notes:
   - Read [how to properly contribute to open source projects on GitHub][10].
   - Create a separate branch other than *master* for your changes. It is not possible to directly commit to master on this repository.
   - Write [good commit messages][11].
   - Use the same [coding style and spacing][13]
     (install clang-format. Use make target: `make format` or execute directly from the openhantek directory: `clang-format -style=file src/*`).
   - Open a [pull request][12] with a clear title and description.
   - Read [Add a new device](docs/adddevice.md) if you want to know how to add a device.
   - We recommend QtCreator as IDE on all platforms. It comes with CMake support, a decent compiler, and Qt out of the box.

[10]: http://gun.io/blog/how-to-github-fork-branch-and-pull-request
[11]: http://tbaggery.com/2008/04/19/a-note-about-git-commit-messages.html
[12]: https://help.github.com/articles/using-pull-requests
[13]: http://llvm.org/docs/CodingStandards.html

## Other DSO open source software
* [SigRok](http://www.sigrok.org)
* [Software for the Hantek 6022BE/BL only](http://pididu.com/wordpress/basicscope/)

</details>
