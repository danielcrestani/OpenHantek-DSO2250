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

## 12. Limitações conhecidas

- A banda analógica do DSO-2250 é bem menor que a taxa de amostragem; acima de ~30 MHz a amplitude cai e os
  dois canais diferem alguns por cento.
- Frequências acima de Nyquist aparecem rebatidas (ex.: 80 MHz a 100 MS/s aparece como 20 MHz).
- O modo XY do OpenHantek original não tem botão no painel nesta versão.
