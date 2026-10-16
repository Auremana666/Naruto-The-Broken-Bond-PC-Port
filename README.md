# Naruto: The Broken Bond Recomp

Projeto de recompilação estática da versão Xbox 360 de **Naruto: The Broken Bond**, usando o [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

## Estado atual

Repositório inicializado. Ainda não há código do jogo recompilado, configuração específica do executável ou build executável validado.

- `main`: ponto inicial vazio do projeto.
- `develop`: branch de trabalho para a recompilação.
- `third_party/rexglue`: SDK oficial como submódulo, fixado pelo Git em uma revisão específica.

## Preparação

Depois de clonar este repositório:

```sh
git switch develop
git submodule update --init --recursive
```

As imagens e os arquivos extraídos do jogo ficam apenas no ambiente local. Use `game/` para o conteúdo extraído; imagens, arquivos compactados, executáveis XEX, código em `generated/` e builds em `build/` estão ignorados pelo Git.

## Foco inicial

O objetivo é fazer o jogo funcionar a partir da recompilação. Launcher e desenvolvimento de renderer próprio ficam fora desta etapa.

1. Extrair e identificar o executável do jogo e sua revisão.
2. Preparar a ferramenta ReXGlue e a configuração de recompilação para esse executável.
3. Gerar e compilar o código traduzido com o runtime do SDK.
4. Validar a inicialização e corrigir os problemas necessários para chegar ao gameplay.

As configurações e os comandos específicos de build serão adicionados conforme forem verificados contra o SDK e o executável local.
