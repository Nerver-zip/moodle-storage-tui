# 1. Visão geral, arquitetura em alto nível e estrutura do repositório

## 1.1 Objetivo do projeto

O Moodle Storage transforma operações normalmente executadas pela interface web do Moodle em um fluxo terminal-first. O binário `mstorage` fornece:

- uma **CLI**, apropriada para scripts e automações;
- uma **TUI**, apropriada para navegação, seleção múltipla e operações interativas;
- autenticação automatizada no SSO Shibboleth da UFPel;
- acesso híbrido às APIs REST e AJAX do Moodle;
- armazenamento seguro de token, cookie e credenciais no Secret Service do Linux;
- histórico local de uploads em SQLite;
- atualização automática a partir de GitHub Releases.

O problema central não é apenas “enviar arquivos”. O Moodle representa a área privada por meio de **draft areas** e exige etapas de preparação, mutação e commit. Além disso, a API REST não cobre adequadamente alguns casos, como remover registros de diretórios vazios e solicitar ZIP server-side. O projeto encapsula esses detalhes e apresenta uma abstração de sistema de arquivos ao usuário.

### Evidências principais

- `README.md`: descreve CLI/TUI, SSO, Keyring, REST+AJAX, uploads recursivos, deleções e ZIP.
- `docs/MOODLE_API.md`: documenta o draft workflow e o fallback AJAX.
- `src/core/application.hpp::Application`: registra todos os comandos e escolhe CLI ou TUI.
- `src/moodle/moodle_client.hpp::MoodleClient`: concentra REST, AJAX, draft e operações de arquivo.

## 1.2 Público-alvo

O público primário é composto por usuários Linux, especialmente estudantes e desenvolvedores que utilizam o Moodle/e-AULA UFPel e preferem trabalhar no terminal. Um público secundário são automações pessoais e scripts que precisam sincronizar a área privada do Moodle sem browser automation externa.

A implementação atual é deliberadamente específica para Linux:

- usa `/proc/self/exe` no updater;
- usa `termios` para leitura de senha;
- usa `libsecret`/D-Bus;
- distribui um artefato `linux-amd64`;
- assume ferramentas Unix como `tar`.

## 1.3 Principais funcionalidades

| Área | Funcionalidade | Caminho principal |
|---|---|---|
| Autenticação | Login SAML/Shibboleth | `ShibbolethAuth::login_web` |
| Autenticação | Extração de token mobile | `ShibbolethAuth::extract_mobile_token` |
| Sessão | Persistência em Keyring | `SessionManager::save/load` |
| Sessão | Reautenticação silenciosa | `ensure_web_session` |
| Arquivos | Listagem hierárquica | `MoodleClient::list_files`, `ListCommand` |
| Arquivos | Upload simples/recursivo | `UploadCommand`, `TuiApplication::perform_upload` |
| Arquivos | Download simples/recursivo | `DownloadCommand`, `perform_download` |
| Arquivos | ZIP server-side | `MoodleClient::zip_folder` |
| Arquivos | Deleção em lote | `DeleteCommand`, `MoodleClient::delete_items` |
| Arquivos | Criação de diretório | `MkdirCommand`, `MoodleClient::create_folder` |
| Observabilidade | Uso de armazenamento | `get_usage`, `StorageUsageCommand` |
| Observabilidade | Histórico local | `HistoryManager`, `HistoryCommand` |
| Interface | TUI com árvore, overlays e temas | `TuiApplication`, `TuiContext`, `src/tui/views/` |
| Distribuição | Auto-update por GitHub Release | `Updater::perform_update` |

## 1.4 Tecnologias utilizadas

### Linguagem e biblioteca padrão

- **C++23**: `std::expected`, `std::filesystem`, `std::format`, comparação three-way e recursos modernos.
- **CMake 3.25+**: build e composição de dependências.
- **ccache**: aceleração incremental quando disponível.

### Dependências externas

| Dependência | Papel | Impacto arquitetural |
|---|---|---|
| CPR 1.10.5 | Transporte HTTP sobre libcurl | Implementa o adapter concreto `CprClient`; também é usado diretamente no SSO. |
| nlohmann/json 3.11.3 | Parsing e geração JSON | Contratos REST/AJAX, `session.json`, release metadata. |
| spdlog 1.12.0 | Logging | Logger global em arquivo e diagnóstico transversal. |
| CLI11 2.4.2 | Parsing da CLI | Estrutura o adapter de entrada `Application`. |
| FTXUI 5.0.0 | Interface terminal | Define componentes, renderização, eventos e screen loop. |
| GoogleTest/GoogleMock 1.14.0 | Testes | Permite mockar a porta HTTP e testar orquestrações. |
| SQLite3 | Persistência local | Histórico de upload em banco embutido. |
| libsecret-1 | Secret Service | Armazena token, cookie e credenciais fora do JSON local. |
| D-Bus/Gnome Keyring | Runtime de segredos | Necessário em desktop e simulado no CI. |

## 1.5 Paradigma arquitetural

A classificação mais fiel é:

> **Monólito modular cliente-side, em camadas, orientado a comandos, com elementos de Ports and Adapters.**

Não é uma implementação rigorosa de Clean Architecture ou Arquitetura Hexagonal. Existem elementos dessas abordagens:

- `network::HttpClient` é uma **porta**;
- `network::CprClient` é um **adapter**;
- CLI e TUI são adapters de entrada;
- `MoodleClient`, `SessionManager` e `HistoryManager` isolam integrações.

Entretanto, as fronteiras não são totalmente invertidas:

- comandos dependem diretamente de classes concretas como `MoodleClient` e `SessionManager`;
- `MoodleClient` conhece CPR em tipos de parâmetros (`cpr::Payload`, `cpr::Multipart`, `cpr::Cookies`);
- `ShibbolethAuth` usa CPR diretamente, sem a porta `HttpClient`;
- a TUI reimplementa vários casos de uso em `TuiApplication`, em vez de reutilizar os Commands;
- modelos de domínio e DTOs externos estão reunidos em um único header simples.

### Justificativa provável

**Inferência:** a arquitetura foi escolhida para maximizar velocidade de desenvolvimento sem sacrificar testabilidade básica. O projeto precisava compartilhar dependências entre CLI e TUI, mockar rede e acomodar workflows Moodle altamente específicos. O padrão Command e o `mstorage_core` atendem parte desse objetivo com baixo overhead, enquanto a TUI mantém lógica própria para suportar feedback progressivo e callbacks assíncronos.

## 1.6 Arquitetura em alto nível

```text
Usuário / Script
      │
      ├───────────────┐
      ▼               ▼
 CLI11 Application    FTXUI TuiApplication
      │               │
      ├── Commands    ├── TuiContext + Views
      │               └── ações em std::thread
      └───────────────┬───────────────────────┘
                      ▼
       MoodleClient / SessionManager / HistoryManager / Updater
             │              │              │           │
             ▼              ▼              ▼           ▼
        HttpClient      libsecret       SQLite3    GitHub Releases
             │
             ▼
      CprClient / libcurl
             │
       ┌─────┴────────────┐
       ▼                  ▼
 Moodle REST         Moodle Web/AJAX
       │                  │
       └──────── Shibboleth SSO
```

### Relações

1. `main.cpp` delega tudo para `Application`.
2. `Application` é composition root, parser da CLI e dispatcher.
3. Commands encapsulam casos de uso da CLI.
4. A TUI compõe views e executa casos de uso diretamente, com estado compartilhado em `TuiContext`.
5. `MoodleClient` atua como gateway/facade da integração Moodle.
6. `SessionManager` cuida de metadados locais e segredos.
7. `HistoryManager` registra observabilidade local.
8. `HttpClient` permite trocar o transporte em testes.

## 1.7 Estrutura do repositório

```text
.
├── .github/workflows/
│   ├── cmake.yml
│   └── release.yml
├── assets/
├── docs/
│   ├── MOODLE_API.md
│   └── architecture/
├── src/
│   ├── commands/
│   ├── core/
│   ├── models/
│   ├── moodle/
│   ├── network/
│   ├── storage/
│   ├── tui/
│   │   └── views/
│   ├── utils/
│   └── main.cpp
├── tests/
├── CMakeLists.txt
├── README.md
└── LICENSE
```

### `.github/workflows/`

Responsabilidade: automação de integração contínua e releases.

- `cmake.yml`: build e testes em Ubuntu 22.04/GCC 13, com D-Bus e Gnome Keyring.
- `release.yml`: build de tag, empacotamento e GitHub Release.

Consumidores: GitHub Actions e mantenedores. Não deve conter regra de negócio.

### `assets/`

Responsabilidade: imagens e GIFs usados na documentação e divulgação. Não participa do runtime.

### `docs/`

Responsabilidade: documentação de integração e arquitetura.

- `MOODLE_API.md`: conhecimento específico sobre REST, AJAX, drafts e SSO.
- `architecture/`: visão transversal para onboarding e manutenção.

### `src/commands/`

Responsabilidade: casos de uso acionados pela CLI. Cada classe implementa `Command::execute()` e recebe dependências no construtor.

Dependências principais: `MoodleClient`, `SessionManager`, `HistoryManager` e modelos. Consumido por `Application` e testes.

Observação: apesar do objetivo inicial de reutilização, a TUI não chama esses comandos para a maior parte das operações; mantém orquestração própria.

### `src/core/`

Responsabilidade: bootstrap, sessão transversal, versão e atualização.

- `application.hpp`: composition root e dispatcher;
- `session_manager.hpp`: persistência de sessão/segredos;
- `session_helper.hpp`: validação e renovação;
- `updater.*`: self-update;
- `version.hpp`: versão compilada.

### `src/models/`

Responsabilidade: estruturas de transferência internas simples. Não contém comportamento de domínio relevante.

### `src/moodle/`

Responsabilidade: integração específica com Moodle e IdP.

- `MoodleClient`: REST, AJAX, drafts, upload/download/delete;
- `ShibbolethAuth`: automação do SAML e captura de token.

É o módulo de maior concentração de conhecimento externo e maior risco de quebra por mudanças no servidor.

### `src/network/`

Responsabilidade: porta HTTP e implementação CPR. Consumido pelo MoodleClient, updater e testes. O uso de tipos CPR na interface reduz a independência da porta.

### `src/storage/`

Responsabilidade: persistência local em SQLite. Atualmente contém apenas `HistoryManager`.

### `src/tui/`

Responsabilidade: interface terminal e estado interativo.

- `TuiApplication`: lifecycle, threads, ações e composição de views;
- `TuiContext`: estado compartilhado, helpers e callbacks;
- `ThemeManager`: temas;
- `views/`: componentes FTXUI por tela/modal.

### `src/utils/`

Responsabilidade: concerns transversais pequenos. Atualmente inicializa o logger em arquivo.

### `tests/`

Responsabilidade: testes unitários e de orquestração com GTest/GMock. O executável `unit_tests` linka contra `mstorage_core`.

### `CMakeLists.txt`

Responsabilidade: dependências, flags, target estático `mstorage_core`, executável e testes. O uso de `GLOB_RECURSE` inclui novos `.cpp` automaticamente, com o trade-off de menor explicitude do grafo de build.
