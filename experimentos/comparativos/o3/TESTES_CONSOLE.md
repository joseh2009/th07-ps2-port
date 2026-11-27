# Teste no console/PCSX2 - ps2-otim-2 (itens 1, 2, 3)

Nada abaixo foi medido ainda. Os resultados precisam vir do seu teste.

## Antes de tudo
- Áudio: deve sair normal desde o início (já corrigido em c1aa0a3).
- Jogo sem queda visível de quadros nem flicker de balas comparado com v26.

## Item 1 - rejeição trivial de triângulos (33bc4f1)
- Desenhar fases com muitos inimigos/efeitos fora da tela (borda da tela).
- Verificar: nenhum objeto some ou aparece cortado de forma errada na borda.
- R3: CALC/DRAW/RENDER devem ficar iguais ou menores.

## Item 2 - contagem de balas visíveis (af1d055)
- Fase com muitas balas (ex.: Stage 5 ou 6, spell cards).
- Verificar: flicker de balas continua ausente quando passa de 256 balas visíveis.
- Risco: a contagem usa o quadro anterior (1 quadro de atraso). Balas podem
  aparecer/sumir um quadro depois do limite. Se notar, reportar.

## Item 3 - -O3 (8111627)
- Risco: float e ordem de operações mudam. Verificar que o replay toca igual
  (mesmo Replay do v26) e que a fase não dessincroniza.
- Verificar: nenhum crash no início de cada fase nem na tela de título.

## Dados para me enviar
- Valores do painel R3 (CALC, DRAW, RENDER, GS, heap) do v26 e desta versão,
  na mesma fase e no mesmo momento.
- Se houver crash: última fase, mensagem e um print.
