# 7. Padrões de projeto, convenções e decisões arquiteturais

## 7.1 Padrões identificados

### Command

**Onde:** `src/commands/`.

Cada operação CLI é um objeto com `execute()`. Benefícios: encapsula parâmetros e dependências, padroniza retorno, facilita testes de orquestração e evita switch de negócio dentro de `Application`.

Limitação: a TUI não reutiliza Commands, então a lógica existe em duas formas.

### Adapter / Port

**Onde:** `HttpClient` e `CprClient`.

`HttpClient` é a porta consumida por `MoodleClient`; `CprClient` é o adapter real; `MockHttpClient` é o adapter de teste. A limitação é o vazamento de tipos CPR pelo contrato.

### Facade / Gateway

**Onde:** `MoodleClient`.

Oferece uma API única para vários endpoints e protocolos Moodle. Esconde payloads, drafts e fallback.

### Strategy implícita

**Onde:** branches em `MoodleClient` baseados em `DraftInfo::sesskey == "REST_TOKEN"`, disponibilidade de cookie e `use_wstoken_`.

REST e AJAX são strategies, mas representadas por condicionais e strings sentinela, não classes intercambiáveis.

### Repository-like

**Onde:** `HistoryManager`.

Encapsula SQL e retorna objetos C++. Não existe interface de repository nem entidade separada.

### Composition Root

**Onde:** `Application`.

Cria infraestrutura concreta e conecta adapters/casos de uso.

### Observer/Callback

**Onde:** callbacks em `TuiContext` e `PostEvent` da FTXUI.

Views sinalizam intenções sem chamar diretamente operações. A screen é notificada quando worker produz mudanças.

### State implícito

**Onde:** `active_tab` e `Container::Tab`.

As telas formam uma máquina de estados, mas valores são inteiros e transições estão distribuídas.

### Factory Method simples

**Onde:** `TuiContext::get_client()`.

Cria um `MoodleClient` configurado com a sessão atual. Não há factory abstrata.

### RAII parcial

- `HistoryManager` fecha SQLite no destrutor;
- `TuiApplication` faz join das threads;
- streams fecham automaticamente.

A C API do SQLite e libsecret ainda usa recursos manuais em pontos importantes.

## 7.2 Padrões não presentes

- MVC formal;
- DI container;
- event sourcing;
- CQRS;
- microservices;
- pub/sub;
- plugin architecture;
- ORM;
- middleware pipeline;
- unit of work explícita;
- domain events.

## 7.3 Convenções de código

### Namespaces

`mstorage::<módulo>`, por exemplo `mstorage::core`, `commands`, `moodle`, `network`, `storage`, `tui` e `models`.

### Nomenclatura

- classes/structs: PascalCase;
- métodos/variáveis: snake_case;
- membros privados: sufixo `_`;
- headers: snake_case `.hpp`;
- Commands: `<Verb>Command`;
- funções de view: `Create<X>View`.

### Tratamento de erro

`std::expected<T, std::error_code>` é o padrão. Exceções externas são capturadas em alguns boundaries, mas não há proibição absoluta de exceções.

### Organização

Muitas classes pequenas são header-only. Implementações maiores da TUI/updater usam `.cpp`. Esse padrão reduz arquivos, mas aumenta recompilação e acoplamento de includes.

### Paths

Paths locais usam `std::filesystem`; paths remotos usam `std::string` com concatenação e normalização ad hoc.

### Logging e output

- spdlog para diagnóstico interno;
- `std::cout`/`std::cerr` para UX CLI;
- strings no `TuiContext` para UX TUI.

Não há camada de internacionalização; mensagens misturam inglês e português.

## 7.4 Convenções implícitas de módulo

- `commands` não deve conhecer FTXUI;
- `views` não deve acessar rede diretamente;
- `MoodleClient` é a única facade para operações remotas após login;
- segredos devem passar por `SessionManager`;
- persistência de histórico deve passar por `HistoryManager`;
- `main.cpp` deve permanecer mínimo;
- novas dependências C++ entram via CPM; dependências de sistema via `find_package/pkg-config`.

## 7.5 Decisão: CLI e TUI no mesmo binário

### Vantagens

- distribuição simples;
- mesmo conjunto de credenciais e estado;
- reaproveitamento do core;
- escolha automática por presença de argumentos.

### Limitações

- binário inclui FTXUI mesmo para uso CLI;
- composition root conhece ambas interfaces;
- testes e linking ficam maiores;
- lógica duplicada entre Commands e TUI.

## 7.6 Decisão: arquitetura híbrida REST/AJAX

### Motivo confirmado

A API REST não remove adequadamente diretórios e não oferece o mesmo ZIP da UI web. AJAX resolve esses casos.

### Vantagens

- funcionalidade completa;
- token estável para operações comuns;
- ZIP eficiente;
- fallback preserva disponibilidade.

### Limitações

- duas credenciais;
- duas semânticas de erro;
- dependência de HTML/endpoints internos;
- maior superfície de teste e segurança;
- reautenticação mais complexa.

## 7.7 Decisão: Keyring + JSON

### Vantagens

- segredos não ficam em texto claro no config;
- integração nativa com desktop Linux;
- logout centralizado.

### Limitações

- dependência D-Bus/daemon;
- complicação no CI/headless;
- apenas um profile;
- inconsistência possível entre JSON e Keyring;
- portabilidade reduzida.

## 7.8 Decisão: `std::expected`

### Vantagens

- erro explícito sem exceções para fluxo esperado;
- composição previsível;
- bom encaixe em Commands.

### Limitações atuais

- erros são genéricos demais;
- mensagens específicas ficam apenas no log;
- não existe um domínio de erro próprio;
- alguns retornos são descartados.

## 7.9 Decisão: TUI com contexto compartilhado

### Vantagens

- views simples;
- wiring rápido;
- fácil acesso a estado comum;
- testes de componentes convenientes.

### Limitações

- God Object;
- data races;
- transições implícitas;
- difícil reutilização;
- callbacks sem tipo de resultado.

## 7.10 Decisão: SQLite para histórico

### Vantagens

- zero servidor;
- queries e ordenação simples;
- persistência robusta;
- fácil expansão futura.

### Limitações

- schema mínimo e sem migrations;
- conexão concreta acoplada;
- dados atualmente pouco expressivos.

## 7.11 Decisão: binary self-update

### Vantagens

- UX simples para usuários sem package manager;
- distribuição rápida;
- versão centralizada.

### Limitações

- boundary de segurança crítico;
- permissões de instalação;
- somente Linux amd64;
- conflito com package managers;
- validação insuficiente.

## 7.12 Decisão: parsing HTML com regex

### Motivo provável

Evitar uma dependência DOM pesada e lidar com páginas conhecidas do IdP/Moodle.

### Trade-off

É compacto, porém frágil a mudanças de HTML, encoding, ordem de atributos, aspas simples e scripts. Para inputs hidden, a regex atual exige ordem `type`, `name`, `value`, o que pode quebrar com markup semanticamente equivalente.

## 7.13 Dívida arquitetural principal

A dívida não é “falta de microservices”; o projeto não precisa deles. Os pontos relevantes são:

1. boundaries de segurança insuficientes;
2. duplicação CLI/TUI;
3. tipos de domínio fracos;
4. concurrency model inseguro;
5. transporte HTTP pobre em metadata;
6. integração SSO não abstraída;
7. configuração espalhada;
8. testes majoritariamente happy-path.
