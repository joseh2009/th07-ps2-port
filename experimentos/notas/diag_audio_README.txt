Duas builds de diagnostico do audio, ambas a partir do commit fc68453 (itens A-G).
Mude UMA coisa em relacao a build atual; compare o som.

1) th07-diag-v1-noUVreserve.elf
   Igual ao fc68453, MAS sem a reserva de 36 KB do uvBatch no boot.
   Se o som voltar: o problema e memoria/heap no boot.

2) th07-diag-v2-verbose.elf
   Igual ao fc68453, MAS com -DTH07_PS2_VERBOSE (printf/fflush dos logs).
   Se o som voltar: o problema e a temporizacao do frame (sem o printf o frame fica mais rapido).

Se nenhuma das duas mudar nada: o problema esta em outro lugar; mande as linhas "audio:" do log na tela.
