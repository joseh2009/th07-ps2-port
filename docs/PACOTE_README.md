# Touhou 7 PS2 port: estado do branch ps2-otim-2

Este pacote reúne o trabalho de otimização e diagnóstico do port PS2 do Touhou 7 (th07), feito em cima do branch `ps2-port` (commit base `c1aa0a3`, a correção de áudio).

Regras que valeram em todo o trabalho:
- Nada de lógica do jogo: movimento, colisão, RNG, replay, spawn, velocidade, padrões e dificuldade ficam como estavam. Só desenho, buffers e sincronização mudam.
- Cada mudança tem um comentário curto explicando o porquê.
- Incertezas ficam atrás de `#define` com o risco escrito.
- Nada aqui foi verificado em console real. Os testes foram feitos no PCSX2 (Linux) pelo usuário, com capturas do painel R3.

## Conteúdo do pacote

- `th07-ps2-final.elf`: build padrão do HEAD (`6d3454c`), stripado.
- `th07-ps2-final-demo.elf`: build de demonstração (`TECHDEMO=1`), stripado. Sem menu, sem HUD, painel R3 ligado desde o início, roda os replays de demonstração do próprio jogo em sequência.
- `th07-ps2-otim-2.bundle`: repositório git completo com os branches `ps2-port` (189 commits, base `c1aa0a3`) e `ps2-otim-2` (HEAD `6d3454c`). Para clonar: `git clone th07-ps2-otim-2.bundle th07 -b ps2-otim-2`. Os patches em `patches/` também aplicam os 10 commits de otimização sobre `c1aa0a3`.
- `patches/`: um patch por commit desde `c1aa0a3`, para aplicar com `git am`.
- `commits.txt`: lista dos commits.

## O que foi feito, commit por commit

| Commit | Mudança | Tipo | Status |
|---|---|---|---|
| `33bc4f1` | Rejeição trivial de triângulos antes de montar cor e UV (`DrawVertices` em quatro passadas) | Desenho (EE) | Compila; não medido |
| `af1d055` | Contagem de balas visíveis no próprio loop de desenho, em vez de um percurso extra | Desenho (EE) | Compila; não medido; pode atrasar um frame o limite de 256 balas |
| `8111627` | `EE_OPTFLAGS` de `-O2` para `-O3` | Build | Compila; **não é a causa da lentidão** (o teste com `-O2`, a o9, deu o mesmo resultado) |
| `f461ac9` | Inimigos sem trilha, totalmente fora da área de jogo (+64 px e tamanho do sprite), não são enviados | Desenho | Testado: melhorou o spawn de inimigos, segundo o usuário |
| `a7785c9` | Efeitos das camadas 0 e 3 totalmente fora da área de jogo não são enviados | Desenho | Compila; não medido; efeito pesa pouco no painel |
| `6e40d61` | Quads do fundo com caixa projetada totalmente fora da área de jogo não são enviados | Desenho | Testado: não mudou o "aparecer do nada" (ver abaixo) |
| `d658163` | Build de demonstração: começa direto na fase 1, esconde o HUD, painel ligado | Build opt-in | Compila; `TECHDEMO=1` |
| `8758d7e` | Demonstração roda os replays do jogo em sequência (mesmo caminho do menu) | Build opt-in | Testado: rodou pelo menos até a transição para o carregamento |
| `7965047` | Setters do GS (`Enable`, `Disable`, `SetBlendMode`, `SetDepthMask`, `SetDepthFunc`, `SetAlphaTestRef`) e `DisableFog` só descarregam o lote quando o valor muda | Desenho (EE) | Compila; não medido |
| `6d3454c` | `PushSprite` checa o limite do buffer de vértices (8192 sprites); se não couber, descarrega e recomeça do topo | Desenho (EE) | Compila; corrige a hipótese do erro `TLB Miss`, ainda não confirmada |

Mudanças de build que **não** entraram:
- `-O3` ficou no commit, mas não é a causa da lentidão. Se quiser o build mais conservador, reverta `8111627`.

## Diagnóstico de desempenho (painel R3)

Os nomes abaixo vieram de mapear os endereços do painel com os símbolos do ELF da demo. A captura é de uma cena com popups e balas.

- **DRAW (maior custo):** `AsciiManager::OnDrawPopups` (~2.5 ms). Os popups são os textos de pontuação de morte de inimigo e de captura de spellcard. Cada frame com popup troca a função de profundidade, desliga a névoa e desenha os dígitos um a um. Isso descarregava o lote de GS várias vezes por frame. O commit `7965047` reduz as descargas repetidas, mas o trio de troca de profundidade ida e volta continua.
- **CALC (atualização):** `BulletManager::OnUpdate` (~1.8 ms). Os efeitos (`EffectManager::OnUpdate`) pesam só ~0.2 ms, então o lag de morte vem mais dos popups e das balas do que dos efeitos.
- **Áudio:** na captura comparada com a v26, `aud` aparece com ~2.75 ms na build atual e 0.00 na v26. A v26 estava sem áudio (relato do usuário), então parte da diferença de EE é o áudio, que a v26 não fazia.
- **GS:** 8 a 14 ms por frame em cena com spellcard. É carga de rasterização e blend, não de CPU. Reduzir isso sem mudar o visual é difícil.
- **Fundo:** `Stage::OnDrawLowPrio` chama `RenderObjects`, que processa todos os quads do cenário a cada frame. O corte do commit `6e40d61` só descarta quads totalmente fora da área, não reduz o trabalho dos que estão dentro.

## Erros que continuam abertos

1. **Travamento com `TLB Miss, pc=0x10ca40 ... store` (PushSprite):** apareceu na demo (o11, o12) depois de ~30 s de execução. A hipótese de estouro do buffer de vértices está no commit `6d3454c`, mas não foi confirmada. Se voltar a aparecer, a causa não é o estouro, e o próximo passo é encontrar qual desenho roda antes do primeiro `ResetVertexBuffer` do frame. Também vale checar se `GameWindow` chama o reset só quando `curFrame == 0`.
2. **Aviso do PCSX2 "Possible incorrect Q value used in COP2" (vdiv Q em 0x32afc0 e vizinhos):** aparece nas builds. Não sei se o código é nosso ou do gsKit/jogo; precisa conferir no v26 para ver se é anterior.
3. **Pop-in de balas no meio da tela:** o usuário ainda relata objetos que aparecem do nada. Descartados: corte do fundo (`o7`), limite de balas (`o8`). Não descartados: cortes de efeito e inimigo na borda, e a rejeição trivial (`33bc4f1`), que pode estar descartando triângulos de bala. Esse último é o próximo teste sugerido.
4. **Desempenho na morte e no spellcard:** ainda pior que a v26. Os cortes e as descargas reduziram parte do custo, mas não há medição de cada commit no mesmo frame. Não tenho uma comparação do tipo "commit a commit" com o mesmo replay.
5. **Item 4 da lista original (strip de primitivas no gsKit):** não feito. O layout de primitiva não está documentado e conflita com o recorte.
6. **Item 5 (mistura de áudio no EE):** não feito. Precisa de medição com áudio ligado e desligado.
7. **Item 6 (CT8 com CLUT):** não feito. O suporte a CLUT no `TexManager` do gsKit não foi verificado.
8. **Cache de texto do HUD:** não feito. `AddFormatText` monta a string a cada frame e cada caractere vira um sprite; cachear isso exige guardar a lista de vértices, o que muda o caminho de envio.
9. **Script para reduzir o `th07.dat`:** não feito. O arquivo não está no repositório e não sei o formato dele. Preciso do arquivo ou de uma descrição do formato para escrever a redução sem quebrar o jogo.
10. **Push para o GitHub:** bloqueado (repositório fora da sessão). Os commits estão só localmente; o bundle traz os 10 commits de otimização.

## Como compilar

Variáveis de ambiente usadas:

```
export PS2DEV=/opt/ps2dev-x/ps2dev
export PS2SDK=$PS2DEV/ps2sdk
export GSKIT=$PS2DEV/gsKit
export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin
```

Build padrão:
```
make -f Makefile.ps2 clean
make -f Makefile.ps2 -j8
```

Build de demonstração:
```
make -f Makefile.ps2 clean
make -f Makefile.ps2 -j8 TECHDEMO=1
```

O Makefile não tem dependência de cabeçalho: depois de mudar um `.hpp`, use `clean`.

## Roteiro de teste no console (PCSX2)

1. Demonstração (`TECHDEMO=1`): deixe rodar até passar pelos três replays e voltar ao primeiro. Veja se aparece `TLB Miss ... store` no log e se o FPS cai a zero.
2. Mesma cena nas builds (v26, `6d3454c` e a anterior): compare FPS, EE, DRAW e GS no mesmo frame do replay.
3. Pop-in: na fase com muitas balas, veja se algum objeto aparece do nada no meio da tela. Anote se é bala, inimigo ou efeito.
4. Morte de inimigo e spellcard: anote os valores do painel nesses momentos.
