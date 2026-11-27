# Notas verificadas do port enviado (base para integração futura)

Inspecionado no pacote `th07-ps2-tudo (1).zip`:

- `src/AnmManager.hpp`: `ZunImageInfoEmbedded` tem cabeçalho de 0x10 bytes com `magic`, `colorDepth`, `imageType`, `format`, `width`, `height`, `unused_c` e payload flexível `data[]`.
- `src/AnmVm.cpp`: `g_TextureBytesPerPixel` é `{4, 4, 2, 2, 3, 2, 0}`. Os formatos 1..5 lidos pelo carregador ocupam respectivamente 4, 2, 2, 3 e 2 bytes/pixel.
- `src/AnmManager.cpp`: no caminho PS2, `ConvertEmbeddedRows()` expande os cinco formatos conhecidos para RGBA8888. `LoadTextureEmbedded()` cria a textura e chama `SetTextureImage(... PIXEL_RGBA, PIXEL_UNSIGNED_BYTE, ...)` ou `SetTextureSubImage()`.
- `src/graphics/PS2GS.cpp`: `TH07_TEX_PSM` está definido como `GS_PSM_CT32`. O framebuffer, separadamente, pode ser CT16 ou CT32 por `TH07_PS2_CT32`.
- `PS2GS::SetTextureImage()` arredonda largura/altura para potências de dois, calcula tamanho com `gsKit_texture_size_ee(..., TH07_TEX_PSM)`, aloca memória GS e chama `ConvertAndStore()` quando recebe dados.

Consequência: formatos ANM originais 16-bit economizam bytes no arquivo/na leitura, mas o caminho atual do PS2 os expande e aloca como textura CT32. Alterar somente a ferramenta Python não reduz a VRAM. A integração real exigirá uma mudança coordenada no carregador ANM e no backend GS, incluindo formato PSM, layout/tile/swizzle, alinhamento, CLUT para PSMT e atualização correta das operações de upload/subimagem.

Nenhum arquivo do port foi modificado nesta versão 2.0. O pacote de saída contém a ferramenta PC e a análise acima, não uma build PS2.
