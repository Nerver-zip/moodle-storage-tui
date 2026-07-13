# 5. Sistema de build, testes e CI/CD

## 5.1 Build local

O arquivo raiz `CMakeLists.txt` exige CMake 3.25 e C++23.

Fluxo típico:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

### Targets

#### `mstorage_core`

Biblioteca estática que contém todos os `.cpp` de `src/`, exceto `main.cpp`. Ela é linkada pelo executável principal e pelo executável de testes. Essa separação evita compilar a aplicação inteira duas vezes e torna os componentes acessíveis aos testes.

#### `moodle-storage`

Executável cujo único source direto é `src/main.cpp`. O artefato é renomeado para `mstorage` durante packaging.

#### `unit_tests`

Executável GTest/GMock definido em `tests/CMakeLists.txt` e descoberto por `gtest_discover_tests`.

## 5.2 Descoberta de sources

O build usa `file(GLOB_RECURSE SOURCES "src/*.cpp")`.

Vantagem: novos arquivos de view entram automaticamente.

Trade-offs:

- mudanças no conjunto de arquivos podem não provocar reconfigure em algumas ferramentas;
- o grafo de target fica menos explícito;
- sources de módulos distintos não são representados por targets próprios.

Uma arquitetura futura pode ter libraries por módulo (`mstorage_network`, `mstorage_moodle`, `mstorage_tui`) com dependências explícitas.

## 5.3 Gerenciamento de dependências

CPM.cmake 0.38.2 é baixado durante configure. CPR, JSON, spdlog, CLI11, FTXUI e GTest vêm de GitHub tags. SQLite3 e libsecret são encontrados no sistema.

Impactos:

- configure requer rede quando cache vazio;
- tags reduzem, mas não eliminam, risco de supply chain;
- não há lockfile com hashes;
- dependências vendorizadas podem aumentar o tempo do primeiro build;
- linking estático é priorizado para bibliotecas CPM, enquanto libsecret/SQLite dependem do ambiente.

## 5.4 Flags e tooling

- `-Wall -Wextra -Wpedantic` globais;
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON`;
- cópia de `compile_commands.json` para a raiz após build;
- ccache detectado automaticamente;
- `BUILD_SHARED_LIBS=OFF` forçado;
- opções de curl/CPR ajustadas para linking estático.

Não há no CMake atual:

- sanitizers;
- clang-tidy;
- coverage;
- LTO explícito;
- warnings-as-errors;
- presets;
- install target;
- package metadata CPack;
- reproducible build controls.

## 5.5 Organização dos testes

`tests/CMakeLists.txt` inclui:

- `test_moodle_client.cpp`;
- `test_session_manager.cpp`;
- `test_history_manager.cpp`;
- `test_commands.cpp`;
- `test_tui.cpp`;
- `test_updater.cpp`.

### Tipos de teste

#### Unitários

- parsing de versões;
- parsing de draft info;
- persistência SQLite;
- sessão.

#### Orquestração com mocks

Commands são exercitados com `MockHttpClient`. Expectations validam endpoints e sequência geral.

#### TUI headless

Views e componentes são renderizados/inspecionados sem terminal real, favorecido pela exposição de `get_root_component` e amizade com fixture.

### Mocks

A rede é mockável porque `MoodleClient` depende de `HttpClient`. `NiceMock` é usado em Commands para evitar matchers complexos de `cpr::Payload`.

Consequência: testes confirmam caminhos felizes, mas podem não detectar parâmetros ausentes ou incorretos no payload.

### Isolamento

Fixtures sobrescrevem `HOME` para usar diretórios temporários. O CI inicia uma sessão D-Bus e um Gnome Keyring. TearDown limpa credenciais.

### Lacunas de teste

- TLS e status HTTP;
- erros parciais de download;
- commit falhando após upload;
- JSON inválido em ZIP;
- paths maliciosos/path traversal;
- archive malicioso no updater;
- concorrência/data races da TUI;
- SSO real e variações HTML;
- quota diferente de 100 MiB;
- diretórios vazios e symlinks no upload local;
- arquivos grandes e streaming;
- testes de integração contra Moodle de sandbox;
- cobertura de todos os comandos negativos.

## 5.6 CI: `.github/workflows/cmake.yml`

### Triggers

Push e pull request em `main`/`master`, mas somente quando mudam `src/**`, `tests/**`, `CMakeLists.txt` ou o próprio workflow. Logo, mudanças apenas em documentação, assets ou outros arquivos não executam CI.

### Ambiente

- Ubuntu 22.04;
- GCC 13 instalado via PPA;
- ccache action;
- dependências de build;
- DBus e Gnome Keyring.

### Etapas

1. checkout;
2. ccache;
3. apt dependencies;
4. configure Release;
5. build paralelo;
6. `dbus-run-session`, unlock do keyring e `ctest`.

### Pontos positivos

- compilador compatível com C++23 fixado;
- testes exercitam libsecret em ambiente próximo ao runtime;
- output-on-failure;
- ccache.

### Limitações

- ações usam majors antigos (`checkout@v3`, por exemplo);
- nenhuma matrix de compilador/distribuição;
- apenas Release;
- sem sanitizers;
- sem cache CPM explicitamente gerenciado;
- sem análise estática, SBOM ou dependency scanning;
- sem verificação de formatação;
- docs-only PR fica sem check obrigatório.

## 5.7 CD: `.github/workflows/release.yml`

### Trigger

Push de tags `v*` com filtro de paths para source/tests/CMake/workflow.

**Risco:** filtros de paths em workflows disparados por tag podem impedir uma release quando o commit tagueado não apresenta mudança em um path listado em relação ao GitHub event. O comportamento deve ser validado e, preferencialmente, o filtro removido para tags.

### Pipeline

1. checkout com histórico;
2. ccache;
3. GCC 13 e deps;
4. configure Release estático;
5. build;
6. copia binário para `staging/mstorage`;
7. cria `moodle-storage-linux-amd64.tar.gz`;
8. publica via `softprops/action-gh-release` com release notes automáticas.

### Limitações de distribuição

- apenas Linux amd64;
- não há checksum publicado;
- não há assinatura Sigstore/GPG;
- não há provenance/attestation;
- não há SBOM;
- não executa testes explicitamente antes de publicar;
- não verifica que a versão compilada corresponde à tag;
- não cria pacotes Arch/Deb/RPM;
- não valida runtime dependency de libsecret no artefato.

## 5.8 Estratégia de release

A versão é constante compilada em `src/core/version.hpp`. Releases são dirigidas por tag. Não existe automação para atualizar a constante, validar SemVer ou impedir tag/versão divergentes.

Recomendação:

- derivar versão do tag no build;
- gerar header via CMake;
- testar artefato empacotado;
- publicar SHA-256 e assinatura;
- usar release environment protegido;
- adicionar attestations do GitHub Actions.

## 5.9 Deployment

Não há servidor para deploy. O deployment é distribuição do binário. O usuário instala em PATH e a aplicação mantém estado local.

## 5.10 Observabilidade do pipeline

O projeto depende do status do GitHub Actions, mas não possui upload de test reports, coverage ou artefatos de diagnóstico. Em falhas de testes de Keyring, logs ficam apenas no job.
