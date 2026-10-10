# Guia de uso – OpenHantek edição DSO-2250

Este guia descreve a versão modificada para o Hantek DSO-2250. Para compilar e instalar, veja o
[readme](../readme.md#compilar-e-instalar-pop_os--ubuntu--debian).

> **Segurança:** o DSO-2250 não é isolado. O terra (jacaré) das ponteiras está ligado ao terra do computador.
> Nunca ligue o jacaré a um ponto que não seja terra, e não meça a rede elétrica diretamente.

## 1. Visão geral da janela

| Área | O que tem |
|---|---|
| Barra do topo | abrir/salvar configuração · **RUN/STOP · SINGLE · AUTOSET · FORCE** · fósforo digital · lupa · cursores · **● REG** (registro de dados) |
| Tela | sinais no tempo, espectro (FFT), cursores, marcação de harmônicos |
| Rodapé | medições de cada canal (na cor do canal) e leitura dos cursores |
| Painel (direita) | CH1, CH2, Horizontal (azul), Trigger (laranja), FFT (violeta) |

Clique no título de uma seção do painel (▾/▸) para recolhê-la; o programa lembra quais estão abertas.

## 2. Aquisição

- **RUN/STOP**: verde = parado, vermelho = adquirindo.
- **SINGLE**: espera um disparo, captura uma vez e para.
- **AUTOSET**: ajusta V/div, tempo/div, nível do trigger e (se ligada) a escala da FFT ao sinal presente.
- **FORCE**: força um disparo.

## 3. Canais (CH1 / CH2)

- **LIGADO**, **DC/AC** (acoplamento), **INV** (inverte).
- **V/div ▼▲** e **Posição ▼▲ 0**.
- **Ponteira**: x1, x10, x20, x50, x100, x200, x500, x1000 — use a mesma relação da chave da ponteira ou da
  sonda diferencial. Com x500/x1000 a escala vai até 5 kV/div (mostrada em kV).
  Para medir na rede elétrica use **sonda diferencial** (o DSO-2250 não é isolado).
- **Garra**: CC65 20A (100 mV/A), CC65 65A (10 mV/A), CC650 60A (10 mV/A), CC650 650A (1 mV/A).
  Com uma garra, o canal mostra ampères em tudo (A/div, Ipp, RMS, FFT em dBA, registro).
  A chave da garra precisa estar na mesma faixa do botão; zere a garra antes de medir DC.

## 4. Horizontal

- **Tempo/div ◀▶**, **Pré-disparo ◀▶** e **50 %**.
- **Memória**: amostras por aquisição. Mais memória = FFT com mais resolução e lupa melhor, porém mais lento.
  **Roll** = rolagem contínua para tempos/div longos.
- Taxa: até 100 MS/s com os dois canais (o fabricante anuncia 250 MS/s com um canal; não foi verificado
  nesta versão). A banda analógica é bem menor: acima de ~30 MHz a amplitude cai e os canais diferem entre si.

## 5. Trigger

- **Modo**: AUTO (dispara sozinho se não houver sinal), NORMAL (só com disparo), ÚNICO.
- **Fonte**: CH1, CH2 ou EXT. **Borda**: subida ou descida.
- **REJ. AF**: rejeição de altas frequências — liga o filtro de disparo do aparelho e ignora ruído acima de
  ~50 kHz no alinhamento. Use em sinais lentos com ruído; desligue para sinais acima de algumas dezenas de kHz.
- **Nível ▼▲** e **50 %** (meio do sinal).

## 6. Medições (rodapé)

Menu **Medições → CH1/CH2**: escolha o que aparece no rodapé: Vpp, máximo, mínimo, média, RMS, RMS AC,
frequência, período, ciclo ativo, largura positiva/negativa, tempo de subida/descida.
Para comparar amplitudes com ruído, prefira **RMS AC** (o Vpp é inflado pelo ruído).

## 7. Cursores (menu Cursores)

1. **Mostrar cursores**.
2. **Canal do cursor**: o que o mouse arrasta (marcadores da lupa, CH1, CH2, MATH ou um espectro).
3. **Tipo**: linhas verticais (tempo/frequência), horizontais (amplitude/nível) ou ambos.
4. Arraste as linhas na tela. A leitura aparece no rodapé: t1, t2, Δt, 1/Δt, V₁, V₂, ΔV
   (A com garra); no espectro f1, f2, Δf, L1, L2, ΔL.
- **Posicionar no sinal**: no canal, do disparo até um período e no máx./mín.; no espectro, na fundamental
  e no maior harmônico. **Centralizar** volta os cursores ao meio da tela.

## 8. FFT (analisador de espectro)

- **SP1 / SP2 / SPM**: liga o espectro de CH1, CH2 ou do canal MATH. Ao ligar, a escala se ajusta sozinha.
- **Janela**: Ret. (resolução máxima), Hann (uso geral), Flat-top (amplitude exata),
  B-Harris (faixa dinâmica, harmônicos pequenos).
- **Escala** (dB/div), **Ref. topo** (nível da linha de cima, em dBV/dBA), **Hz/div**.
- **AUTO FFT**: Hz/div na fundamental (harmônicos caem nas divisões), 10 dB/div e referência acima do pico.
- **Faixa total**: de 0 Hz até metade da taxa de amostragem, na tela toda.
- **Média** (desl./4/16/64), **Ret. pico** (máximo de cada frequência), **Limpar**.
- **Marcar harm.**: marca F e os harmônicos 2…10 que se destacam do ruído; o painel lista os dBc e o THD.
- **Só FFT**: esconde os sinais no tempo (a aquisição e o disparo continuam).

Dicas:
- Resolução em frequência = taxa de amostragem ÷ memória. Para ver harmônicos de 1 kHz, use a memória grande.
- O nível mostrado pelo marcador (dBV RMS da componente) é correto com qualquer janela; o pico do traço
  só é exato com Flat-top.
- Aferição rápida com um gerador: senoide 1 kHz, 1 Vpp → cerca de −9 dBV.

## 9. Exportar imagem

**Exportar → Imagem da tela** (Ctrl+E) grava em PNG, JPG ou BMP exatamente o que está na tela, com rodapé.
**Exportar → Copiar imagem da tela** (Ctrl+Shift+C) copia para a área de transferência.

## 10. Registro de dados (log)

Botão **● REG** na barra (clique = iniciar/parar; seta = configurar) ou **Arquivo → Registro de dados** (Ctrl+L).

- **Canais**: CH1 e/ou CH2, um arquivo CSV para cada: `prefixo_CH1_AAAAMMDD_HHMMSS.csv`.
- **Conteúdo**:
  - *Medições*: uma linha por aquisição com data/hora (ms), Vpp, máx., mín., média, RMS, RMS AC,
    frequência, período e ciclo ativo (colunas em A com garra);
  - *Forma de onda*: as amostras visíveis na tela a cada aquisição.
- **Intervalo mínimo** entre registros (0 = toda aquisição).
- **Início**: manual, ou **por trigger** — o botão fica ◌ ARMADO e a gravação começa quando a fonte do
  trigger cruza o nível na borda escolhida (com EXT, na primeira aquisição disparada; use o modo NORMAL).
- **Fim**: manual, após N registros ou após X minutos.
- Formato: separador `;`, vírgula decimal e números entre aspas (abre direto no LibreOffice/Excel em português).
  No LibreOffice, marque só “Ponto e vírgula” na importação.

## 11. Configurações (Osciloscópio → Configurações)

- **Tela**: contraste da grade, interpolação (sen(x)/x, linear, pontos), persistência do fósforo digital.
- **Cores**: cores da tela e de cada canal/espectro (aplicadas ao clicar em OK/Aplicar).
- **Arquivos**: salvar a configuração ao sair.

**Arquivo → Abrir/Salvar configuração** guarda todas as escalas e opções num arquivo `.ini`.

## 12. Calibrar zero (Osciloscópio → Calibrar zero dos canais)

Com nada ligado às entradas, o traço deve ficar exatamente na linha da sua posição. Cada DSO-2250 tem um
pequeno erro no ajuste de posição, diferente em cada V/div — o traço fica alguns décimos de divisão fora.

1. Desconecte as ponteiras (ou ligue a ponta de cada ponteira ao próprio jacaré).
2. **Osciloscópio → Calibrar zero dos canais...** — leva cerca de meio minuto.
3. Pronto: a correção fica salva e é aplicada sempre (traço, medições, cursores e nível do trigger).

Refaça de tempos em tempos ou se o aparelho estiver bem mais quente/frio. **Apagar calibração de zero** volta
a usar só a calibração de fábrica.

## 13. Programa PSG9080 (gerador de funções)

**PSG9080** (menu de aplicativos, ou `./openhantek/PSG9080`) controla o gerador Joy-IT / JunTek PSG9080 pela
USB (porta serial, normalmente `/dev/ttyUSB0`). Não precisa do osciloscópio. A porta e o botão **Conectar**
ficam no topo; as mensagens, embaixo. Se aparecer “sem permissão”, rode `sudo usermod -aG dialout $USER` e
entre de novo na sessão. Cada aba relê o gerador quando é aberta (os botões do aparelho podem ter mudado algo).

### Básico
Em cada canal: **SAÍDA** liga/desliga; forma de onda (22 de fábrica e arbitrárias 01 a 99); frequência com a
unidade (Hz, kHz, MHz, mHz, µHz — aceita vírgula); amplitude (até 25 Vpp abaixo de 1 MHz), offset (−9,99 a
+12 V), duty e fase. O valor vai ao apertar Enter, ao usar a roda/setas ou ao sair do campo, e o painel relê o
gerador: o que aparece é o que o aparelho aceitou. **Presets:** *Salvar atual…* guarda os dois canais ou um só;
um preset de um canal pode ser aplicado no CH1 ou no CH2. Ficam em `~/.config/psg9080-gui/presets.json`.

### Modulação
Escolha o canal, o **tipo** (AM, FM, PM, ASK, FSK, PSK, Pulso/PWM, Burst) e os parâmetros que aparecem para ele;
cada campo vai na hora. **MODULAÇÃO LIGADA** põe o gerador na tela de modulação do canal (é assim que o aparelho
ativa a modulação); desligar volta à tela normal. A portadora é a forma, frequência e amplitude do canal na aba
Básico (senoidal, quadrada, rampa ou arbitrária). Fonte externa: entrada Ext.IN, 0 a 3 Vpp, até 20 kHz. No
Burst, **Disparar agora** dispara uma rajada quando o disparo é manual.

### Varredura
Varredura feita pelo próprio gerador: canal, o que varrer (frequência, amplitude ou duty), valores inicial e
final, tempo (0,01 a 640 s), sentido (subindo, descendo, ida e volta) e escala linear ou logarítmica. No modo
**VCO**, a tensão de 0 a 5 V na Ext.IN leva o parâmetro do valor “em 0 V” ao valor “em 5 V”. Se o aparelho
varrer outro parâmetro que não o escolhido, escolha-o também na tela dele (tecla FUNC) — o registrador que
seleciona o parâmetro não é documentado. Para medir resposta em frequência use o **OpenHantekBode**.

### Frequencímetro
Mede o sinal da entrada **Ext.IN** (2 a 20 Vpp, 1 Hz a 100 MHz): frequência, período, larguras + e − e duty, ou
conta pulsos (contador). Ajuste o acoplamento (AC/DC), o tempo de porta (0,001 a 10 s; maior = mais dígitos) e a
faixa (baixa abaixo de 2 kHz). **Medir** começa a leitura contínua; mudar de aba ou **Parar** encerra e devolve o
gerador à tela normal.

### Ondas arbitrárias
Cria uma forma de onda de 8192 pontos (14 bits) e grava em uma das **99 posições** do gerador:
- **Fórmula**, com `t` de 0 a 1 no período e `x = 2πt`. Exemplos: `sin(x) + 0.3*sin(3*x)`,
  `exp(-5*t)*sin(20*x)`, `if(t < 0.5, 1, -1)`. Funções: `sin cos tan exp ln sqrt abs sign floor round`,
  `square(x) tri(x) saw(x) pulse(x, duty) sinc(x) gauss(z) noise() min max pow mod if`. Use **ponto** decimal.
  A lista *Exemplos* tem formas prontas (amortecida, sinc, chirp, AM, retificadas, ECG…), inclusive **SPWM**
  como em inversores senoidais: `if(0.9*sin(x) > tri(51*x), 1, -1)` compara a senoide com uma portadora
  triangular de 51 ciclos por período (índice de modulação 0,9). Com o canal em 60 Hz a portadora fica em
  3060 Hz. A modulação “Pulso (PWM)” do aparelho não faz isso: ela só define largura e período fixos.
- **Arquivo**: uma coluna de números, ou CSV com colunas (escolha a coluna). O arquivo de registro de forma de
  onda do OpenHantek (● REG, conteúdo “forma de onda”) é reconhecido: escolha a aquisição — assim uma forma
  capturada pelo DSO-2250 pode ser reproduzida pelo gerador. Arquivos de 8192 valores inteiros (formato do
  PSG9080_ARB, 0–16383, ou de 16 bits do software original) entram sem conversão.
- **Ler do gerador**: traz a onda gravada na posição escolhida.

*Escala*: **Normalizar** estica do mínimo ao máximo (usa toda a resolução; a amplitude se ajusta no canal) ou
**Fixa** (−1 a +1). **Enviar ao gerador** grava a posição (≈5 s, com barra de progresso); **Usar no CH1/CH2**
seleciona *Arbitrária NN* no canal; **Salvar arquivo…** grava 8192 linhas no formato do gerador.

### Sequências
Uma tabela de passos: canal (CH1, CH2 ou ambos), forma, frequência (aceita `1k`, `2,5 MHz`), amplitude, offset,
saída e duração em segundos. Campos vazios ou “(manter)” não mudam o gerador. **Executar** roda os passos em
ordem, **Repetir** N vezes (ou sem parar). **Copiar do gerador** cria um passo com o estado atual do canal. A
tabela fica guardada para a próxima vez; **Salvar…/Abrir…** usam arquivos `.json`. O tempo é contado pelo PC
(~10 ms de precisão).

### Sistema
Modelo, número de série e versões; **sincronismo** (o CH2 acompanha forma, frequência, amplitude, offset e duty
do CH1) e ajuste fino; **memórias** 00 a 99 do aparelho (a 00 é carregada ao ligar): carregar, salvar, apagar;
brilho, bipe, idioma e carregamento de ondas. **Registradores (diagnóstico)** lê os 91 registradores de uma vez
e marca em amarelo o que mudou desde a leitura anterior — útil para descobrir o que um botão do aparelho altera.

## 14. Programa OpenHantekBode (resposta em frequência)

`./openhantek/OpenHantekBode` mede o ganho e a fase de um circuito (filtro, amplificador, malha de
realimentação, filtro de saída de nobreak...) ponto a ponto, com o PSG9080 gerando o sinal e o DSO-2250
medindo. Ele usa o osciloscópio e o gerador sozinho: feche o OpenHantek e o programa PSG9080 antes.

**Ligações:** a saída do gerador escolhida em *Sinal de teste* vai à entrada do circuito; o canal escolhido em
*Entrada do circuito* mede essa entrada; o outro canal mede a saída do circuito. Escolha a ponteira de cada
canal (x1, x10...). A tela pequena embaixo do gráfico mostra os dois canais ao vivo: confira as ligações nela
antes de começar.

**Varredura:** frequência inicial e final, pontos por década e média por ponto. A janela mostra a faixa possível
com a memória escolhida (com 10 k amostras o mínimo fica perto de 0,5 Hz; com 512 k vai mais baixo, mas cada
ponto demora mais; o máximo é o do gerador, 80 MHz). Frequências baixas são lentas: cada ponto captura cerca de
10 períodos.

**Como mede:** para cada frequência o programa ajusta o gerador, escolhe a duração da aquisição, ignora as
aquisições que ainda podem ser da configuração anterior, ajusta a escala dos dois canais e calcula amplitude e
fase só na frequência do gerador — ruído e harmônicos não entram. Acima de 50 MHz (metade da amostragem com dois
canais) o sinal é medido pela frequência rebatida; como a frequência é conhecida, a medida continua válida.

**Calibração:** ligue as duas ponteiras no mesmo ponto (a saída do gerador, sem o circuito) e clique em
**Calibrar...**. A resposta medida (diferenças entre canais, ponteiras e cabos, e a queda de banda do
DSO-2250 acima de ~30 MHz) fica guardada; com **Descontar a calibração** ela é removida das próximas medidas.
Calibre com a mesma faixa, as mesmas ponteiras e o mesmo canal de entrada que vai usar.

**Resultados:** passe o mouse no gráfico para ler frequência, ganho e fase. **Exportar CSV** grava frequência,
ganho (dB e V/V), fase, Vpp de entrada e saída, relação sinal/ruído de cada canal e se o ponto foi medido por
subamostragem (`;` e vírgula decimal). **Salvar imagem** e **Copiar imagem** levam o gráfico.

Dicas:
- Atenuações muito grandes (abaixo de −40 a −50 dB) ficam perto do limite do conversor de 8 bits: aumente
  *Média por ponto* e a amplitude do gerador, se o circuito aceitar.
- Pontos sem medida válida aparecem como lacuna no gráfico e com o motivo no CSV.

## 15. Limitações conhecidas

- A banda analógica do DSO-2250 é bem menor que a taxa de amostragem; acima de ~30 MHz a amplitude cai e os
  dois canais diferem alguns por cento.
- Frequências acima de Nyquist aparecem rebatidas (ex.: 80 MHz a 100 MS/s aparece como 20 MHz).
- O modo XY do OpenHantek original não tem botão no painel nesta versão.
