# 3. Módulos, componentes e dependências internas

## 3.1 Mapa de módulos

| Módulo | Responsabilidade | Dependências internas | Consumidores |
|---|---|---|---|
| `core` | bootstrap, sessão, atualização, versão | network, moodle, storage, commands, tui | `main`, testes |
| `commands` | casos de uso da CLI | moodle, core, storage, models | `Application`, testes |
| `moodle` | gateway REST/AJAX e SSO | network, models | commands, TUI, session helper |
| `network` | porta HTTP e adapter CPR | nenhuma interna | MoodleClient, updater, testes |
| `storage` | histórico SQLite | models | commands, TUI, Application |
| `models` | estruturas de dados | biblioteca padrão | todos os módulos de negócio |
| `tui` | interface interativa e orquestração | core, moodle, storage, models | Application, testes |
| `utils` | logging | nenhuma interna | Application |

## 3.2 `core::Application`

### Responsabilidade

É simultaneamente:

- composition root;
- parser CLI;
- registro de comandos;
- dispatcher;
- adaptador de erros para exit codes;
- seletor CLI/TUI.

### Pontos de entrada

- construtor: logger + registro de comandos;
- `setup_commands()`;
- `execute(argc, argv)`.

### Dependências

Possui concretamente `CprClient`, `SessionManager` e `HistoryManager`. Isso torna o bootstrap simples, mas dificulta testar `Application` isoladamente, pois não há construtor alternativo para injetar mocks.

### Acoplamento

Alto acoplamento estrutural: inclui todos os headers de comandos e TUI. Coesão razoável enquanto o número de comandos for pequeno. Se o projeto crescer, o registro e dispatch manual tenderão a virar um hotspot.

## 3.3 Hierarquia `Command`

`commands::Command` define um único contrato:

```cpp
virtual std::expected<void, std::error_code> execute() = 0;
```

### Implementações

#### `LoginCommand`

- lê credenciais do terminal;
- executa SSO;
- opcionalmente extrai token;
- opcionalmente salva credenciais;
- depende de `SessionManager` e recebe um `HttpClient` atualmente não utilizado.

#### `LogoutCommand`

- remove segredos e `session.json` via `SessionManager`.

#### `UploadCommand`

- prepara draft;
- percorre arquivos/diretórios;
- envia multipart;
- registra histórico;
- commita.

#### `ListCommand`

- lista recursivamente via REST;
- separa diretórios e arquivos;
- ordena e formata saída Unicode.

#### `DownloadCommand`

- garante sessão;
- resolve nome na raiz;
- prefere ZIP para diretórios;
- recua para download recursivo.

#### `DeleteCommand`

- classifica alvos;
- solicita delete no gateway;
- commita.

#### `MkdirCommand`

- depende de cookie/AJAX;
- cria apenas na raiz na CLI.

#### `HistoryCommand`

- consulta ou limpa SQLite.

#### `StorageUsageCommand`

- consulta quota e renderiza barra.
- mantém uma referência a `SessionManager` sem uso.

### Coesão e duplicação

Os Commands têm alta coesão por caso de uso. Entretanto, helpers de path, formatação e recursão são duplicados entre Commands e TUI. O benefício do Command Pattern é parcialmente perdido porque a TUI não reutiliza os Commands.

## 3.4 `network::HttpClient` e `CprClient`

### Papel arquitetural

`HttpClient` é a abstração mais importante para testabilidade. Ele permite que `MoodleClient` seja exercitado com `MockHttpClient` sem rede real.

### Contrato

- `get`;
- `post` form-urlencoded;
- `post_raw`;
- `post_multipart`.

### Limitação da abstração

A interface expõe tipos CPR. Portanto, ela abstrai a execução, mas não o modelo de transporte. Trocar CPR por outra biblioteca exigiria manter adapters para `cpr::Payload`, `cpr::Cookies` e `cpr::Multipart` ou mudar todos os consumidores.

### Adapter concreto

`CprClient`:

- adiciona User-Agent;
- executa requests síncronos;
- mapeia erro CPR para `network_unreachable`;
- retorna apenas body.

Não retorna status, headers, URL final, content type ou tamanho. Isso limita decisões seguras e tratamento fino.

## 3.5 `moodle::MoodleClient`

### Responsabilidade

Funciona como **Gateway + Facade** da API Moodle. Reúne:

- chamada REST genérica;
- descoberta de context id;
- preparação/commit de draft;
- upload;
- listagem;
- quota;
- download;
- mkdir;
- ZIP;
- delete e fallback recursivo;
- refresh de sessão web.

### Estado interno

- referência para `HttpClient`;
- URL base normalizada;
- `wstoken_`;
- `web_cookie_`;
- `use_wstoken_`;
- cache de context id.

### Contratos externos encapsulados

- `webservice/rest/server.php`;
- `webservice/upload.php`;
- `repository/repository_ajax.php`;
- `repository/draftfiles_ajax.php`;
- `lib/ajax/service.php`;
- `/user/files.php`.

### Coesão

A classe é coesa em “interação com Moodle”, mas ampla demais. Mistura autenticação auxiliar, parsing HTML, transporte REST, seleção de strategy REST/AJAX, manipulação de arquivo local e formatação de tamanho.

Uma evolução natural seria separar `MoodleRestApi`, `MoodleWebApi`, `DraftService` e `FileTransferService` atrás de uma facade.

## 3.6 `moodle::ShibbolethAuth`

### Responsabilidade

Simula o browser necessário ao SAML/Shibboleth da UFPel e converte uma sessão web em token mobile.

### Dependências

Usa CPR diretamente, regex, Base64 manual e logging. Não utiliza `HttpClient`, logo não é mockável pelo mesmo mecanismo do restante da integração.

### Pontos de fragilidade

- seletores regex dependem do HTML;
- detecção de falha é textual;
- campos `username`/`password` são específicos do IdP;
- esquema customizado `mstorageapp://` é codificado;
- TLS está desabilitado;
- HTML de erro é salvo em `/tmp`.

## 3.7 `core::SessionManager`

### Responsabilidade

Coordena duas formas de persistência:

- JSON para metadados não secretos;
- Secret Service para segredos.

### Métodos

- `save(SessionData, username, password)`;
- `load()`;
- `load_credentials(url)`;
- `clear_credentials()`.

### Contratos

Schema libsecret: `org.mstorage.Generic`, atributos `moodle_url`, `type`, `username`.

Tipos de segredo:

- `wstoken`;
- `web_cookie`;
- `credentials`.

### Limitações

- não há interface abstrata;
- código de filesystem e Keyring está misturado;
- erros intermediários de `secret_password_clear_sync` compartilham um único `GError*` sem tratamento por chamada;
- não há lock entre threads/processos;
- não há atomic write para `session.json`;
- permissões não são explicitamente definidas.

## 3.8 `core::ensure_web_session`

É um **Application Service** implementado como função inline. Centraliza validação, renovação e sincronização de credenciais com `MoodleClient`.

Pontos positivos:

- remove duplicação de re-login;
- valida token e cookie proativamente;
- persiste renovação.

Pontos de atenção:

- nome sugere validar apenas cookie, mas também invalida/renova token;
- exige os dois canais válidos;
- instancia `ShibbolethAuth` concretamente;
- se persistir a sessão renovada falhar, apenas loga e continua;
- retorna `DraftInfo`, acoplando gestão de sessão ao workflow de draft.

## 3.9 `storage::HistoryManager`

### Responsabilidade

Repository-like component para histórico local de uploads.

### Banco

Uma conexão SQLite permanece aberta durante a vida do objeto. Prepared statements são usados em insert/select. `clear` usa `sqlite3_exec` com SQL constante.

### Interface

- `record_upload`;
- `get_history(limit)`;
- `clear`.

### Limitações

- a struct `HistoryEntry` está aninhada na implementação;
- nome da coluna `url` armazena valores semânticos como `REST_API` e `TUI`, não necessariamente URL;
- não registra status, tamanho, path remoto, hash, erro ou commit id;
- não há migrations versionadas;
- a classe possui raw pointer e deveria desabilitar copy/move inadequado explicitamente.

## 3.10 TUI

### `TuiApplication`

Orquestrador de lifecycle e casos de uso interativos. Cria threads, atualiza status, chama serviços e compõe views.

### `TuiContext`

State container compartilhado. Agrupa dependências externas, estado remoto, navegação, inputs, callbacks, componentes FTXUI e helpers de path/tema.

É prático, mas tende a um **God Object de estado**. Mudanças em uma tela podem afetar o mesmo contexto usado por todas as outras.

### Views

`src/tui/views/` separa renderização por tela: login, browser, mkdir, upload, download, delete, history, main menu, settings e themes.

As views conhecem `TuiContext` e invocam callbacks. Elas não deveriam executar transporte diretamente.

### Máquina de estados

`active_tab` representa estados com inteiros mágicos. A documentação dos valores existe em comentário e na ordem do `Container::Tab`, mas não há `enum class`. Inserir uma nova tela no meio exige sincronizar vários pontos.

## 3.11 `ThemeManager`

Carrega um formato simples:

```text
theme[main_bg]="#1e1e2e"
```

Regex extrai chave e RGB. Chaves desconhecidas são ignoradas. Temas padrão são gravados a partir de strings hardcoded em `TuiContext::ensure_default_themes`.

Ponto de extensão: mover temas padrão para resources instaláveis e validar arquivos com diagnóstico.

## 3.12 `utils::Logger`

Inicializa um logger global spdlog em arquivo, nível debug e flush em cada mensagem debug.

Impactos:

- simplicidade para todos os módulos;
- escrita síncrona frequente pode afetar performance;
- não há rotação, limite de tamanho ou redaction central;
- overwrite está desabilitado, logo o log cresce indefinidamente.

## 3.13 `core::Updater`

Componente autônomo para consulta, download, extração e troca do binário. Depende da porta HTTP, JSON, filesystem e shell externo.

Arquiteturalmente, ele cruza boundaries sensíveis: rede não confiável, metadata GitHub, archive não confiável, filesystem privilegiado e substituição de executável. Por isso deveria ter contratos e validações mais fortes do que o restante do cliente.

## 3.14 Componentes solicitados que não existem como categorias formais

| Categoria | Estado no projeto |
|---|---|
| Controllers | Não existem; `Application` e views cumprem papel de adapter/controller. |
| Services | Não há pasta formal; Commands, `MoodleClient` e `ensure_web_session` exercem esse papel. |
| Managers | `SessionManager`, `HistoryManager`, `ThemeManager`. |
| Repositories | `HistoryManager` é repository-like, sem interface. |
| DTOs/Entities | Structs em `models.hpp`; não há distinção formal. |
| Middlewares | Não existem. Validação de sessão é chamada explicitamente. |
| Hooks | Callbacks da TUI e lifecycle do GitHub Actions, não um sistema de hooks. |
| Providers | `CprClient`, libsecret e SQLite podem ser entendidos como providers/adapters. |
| Factories | Não há factory formal; `TuiContext::get_client` funciona como factory method simples. |
| Workers/Jobs | Threads da TUI executam trabalho, sem scheduler persistente. |
| Pipelines | Workflows CMake/Release e o draft workflow Moodle. |
| Plugins | Não há sistema de plugins. |

## 3.15 Grafo de dependências e direção

Direção predominante:

```text
main -> core::Application
core::Application -> commands + tui + concrete infrastructure
commands -> moodle + core session + storage
TUI -> moodle + core session + storage
moodle -> network + models + CPR types
storage -> SQLite + models
```

A inversão de dependência ocorre claramente apenas no transporte HTTP. Sessão, banco, SSO e gateway Moodle são concretos. Portanto, a arquitetura tem **inversão parcial**, não generalizada.
