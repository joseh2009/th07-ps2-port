# Touhou Texture PS2 Tool 2.0 — Touhou 7

Ferramenta Python/Tkinter para orquestrar o THTK (`thdat` e `thanm`), otimizar imagens comuns e analisar formatos de textura do PlayStation 2. Esta versão preserva o fluxo existente da 1.1 e adiciona uma primeira etapa conservadora de análise de formatos/estimativa de VRAM.

## O que está implementado

- Extrai arquivos `.dat` reconhecidos pelo THTK usando `thdat -x 7`.
- Lista e extrai imagens de `.anm` usando `thanm -l 7` e `thanm -x 7`.
- Guarda manifestos e caminho de origem dos ANM.
- Otimiza imagens extraídas como PNG/JPEG com Pillow (isso reduz armazenamento no PC, não garante redução de VRAM).
- Faz backup do ANM antes de substituir um membro via `thanm -r 7`.
- Reconstrói DAT com `thdat -c 7`.
- Analisa imagens e gera `reports/ps2_texture_analysis.csv`, com recomendação conservadora entre RGBA32, PSMCT16, PSMT8 e PSMT4 e estimativas de armazenamento de textura.

## Ainda NÃO implementado

- O módulo `ps2_texture_encoder.py` nesta versão faz **análise e estimativas**, não cria arquivos de textura GS prontos.
- Não há ainda swizzle/tiling GS validado, upload direto PSMCT16/PSMT8/PSMT4, gestão de CLUT validada ou formato personalizado dentro de ANM.
- A análise usa dimensões arredondadas para potência de dois, conforme a alocação atual observada no backend PS2. As estimativas PSMT incluem uma CLUT de referência, mas precisam ser confirmadas no caminho final de upload.
- A reinserção atual continua usando `thanm -r` e os formatos de imagem aceitos pelo THTK. Ela não insere dados PSM nativos.

## Requisitos

- Linux (MX Linux), Python 3.10+
- Pillow: `python3 -m pip install -r requirements.txt`
- THTK compilado/instalado, com `thdat` e `thanm` no `PATH`.

## Executar

```bash
python3 touhou_texture_tool.py
```

Selecione uma cópia da pasta do Touhou 7 e uma pasta de trabalho separada. Fluxo recomendado: extrair DAT, extrair ANM, otimizar imagens se desejar, analisar formatos PS2, testar a reinserção em cópias e reconstruir DAT.

## Backup e segurança

Use cópias dos arquivos originais. A reinserção cria backup do ANM selecionado. A ferramenta não substitui automaticamente os DAT originais do jogo.

## Próxima etapa técnica

Inspecionar a estrutura exata de `ZunImageInfoEmbedded`/ANM e o backend `PS2GS` do port. Só depois definir a representação binária, o swizzle e a CLUT; em seguida implementar um teste isolado com `player00` e validar a leitura no PCSX2. Não tratar as estimativas desta versão como evidência de redução real de VRAM.
