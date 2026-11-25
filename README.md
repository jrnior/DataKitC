# DataKit C

Uma ferramenta de linha de comando em C para explorar arquivos CSV sem depender de bibliotecas externas. O projeto foi pensado como um exemplo pratico de leitura de arquivos, alocacao dinamica, validacao de dados e estatistica descritiva.

## Funcionalidades

- Leitura de CSV com cabecalho, campos entre aspas, virgulas dentro de campos, aspas escapadas e quebras de linha em campos citados.
- Relatorio de quantidade de registros, colunas, valores vazios e tipo inferido.
- Estatisticas para colunas numericas: minimo, maximo, media, mediana e desvio padrao populacional.
- Frequencia de valores para colunas categoricas, ordenada da maior para a menor contagem.
- Validacao de linhas com numero inconsistente de colunas e mensagens de erro uteis.
- Suporte a arquivos UTF-8 com BOM.

## Compilar

Requer um compilador C11 (GCC, Clang ou MSVC).

### Linux / macOS

```sh
make
```

### Windows (GCC/MinGW)

```powershell
gcc -std=c11 -Wall -Wextra -Wpedantic datakit.c -o datakit.exe -lm
```

No Windows, o `-lm` pode ser omitido dependendo da distribuicao do GCC.

## Uso

```sh
./datakit resumo data/vendas.csv
./datakit estatisticas data/vendas.csv quantidade
./datakit frequencia data/vendas.csv cidade
./datakit frequencia data/vendas.csv produto 3
```

No PowerShell, substitua `./datakit` por `.\datakit.exe`.

Os nomes das colunas devem ser informados exatamente como aparecem no cabecalho. Estatisticas ignoram campos vazios e exigem que todos os demais valores da coluna sejam numericos. A frequencia trata campos vazios como uma categoria separada.

## Exemplo

Com o arquivo `data/vendas.csv`, o comando:

```sh
./datakit estatisticas data/vendas.csv quantidade
```

gera um relatorio com contagem de valores validos e vazios, minimo, maximo, media, mediana e desvio padrao.

## Limites e formato

- O delimitador e a virgula, conforme o formato CSV comum.
- A primeira linha e sempre o cabecalho.
- Cada registro deve ter a mesma quantidade de campos que o cabecalho.
- Campos ausentes sao representados por campos vazios (`a,,c`).
- O tipo inferido e informativo; os dados originais nao sao convertidos nem alterados.

## Estrutura

```text
datakit.c          Implementacao da ferramenta
data/vendas.csv    Conjunto de dados de exemplo
Makefile           Compilacao e limpeza
```

## Licenca

Este projeto esta sob a licenca MIT. Consulte `LICENSE`.
