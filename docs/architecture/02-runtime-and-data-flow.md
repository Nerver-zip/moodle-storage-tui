# 2. Bootstrap, fluxo de execução, comunicação e fluxo de dados

## 2.1 Inicialização da aplicação

### Etapa 1 — entrada mínima

`src/main.cpp` contém apenas:

```cpp
mstorage::core::Application app;
return app.execute(argc, argv);
```

Isso mantém o entrypoint sem regra de negócio e concentra o bootstrap em `Application`.

### Etapa 2 — construção do composition root

O construtor `Application::Application()`:

1. cria o parser `CLI::App`;
2. inicializa o logger em `~/.local/share/mstorage/mstorage.log`;
3. registra flags e subcomandos;
4. já possui como membros as dependências concretas:
   - `network::CprClient`;
   - `core::SessionManager`;
   - `storage::HistoryManager`.

O `HistoryManager` abre/cria `~/.config/mstorage/uploads.db` durante sua construção. O `SessionManager` garante a existência de `~/.config/mstorage`.

### Etapa 3 — parsing e decisão CLI/TUI

`Application::execute()` chama `app_.parse(argc, argv)`.

- `--version`: imprime `core::VERSION`.
- `--update`: executa o updater.
- nenhum subcomando: constrói `TuiApplication` e entra no loop FTXUI.
- subcomando: carrega sessão quando necessário, constrói um `MoodleClient`, injeta token/cookie e instancia o Command correspondente.

### Etapa 4 — execução e código de saída

Todos os Commands retornam `std::expected<void, std::error_code>`. `Application::handle_result` converte sucesso em exit code 0 e erro em exit code 1.

A TUI não usa esse mecanismo; converte erros em strings de status e eventos de repaint.

## 2.2 Inicialização da TUI

`TuiApplication` recebe referências às mesmas dependências do composition root. Seu construtor:

1. constrói `TuiContext`;
2. registra callbacks do contexto para métodos de ação;
3. cria temas padrão em `~/.config/mstorage/themes`;
4. carrega `default.conf`.

`run()`:

1. cria `ScreenInteractive::TerminalOutput()`;
2. guarda um ponteiro para a screen no contexto;
3. tenta carregar a sessão;
4. escolhe tela de login ou browser;
5. dispara refresh inicial se houver sessão;
6. constrói as views e entra em `screen.Loop()`.

A TUI é uma máquina de estados implícita controlada por `TuiContext::active_tab`, com inteiros de 0 a 9. Overlays são renderizados sobre o browser por `ftxui::dbox`.

## 2.3 Comunicação entre componentes

### Chamadas diretas

A maior parte da aplicação usa chamadas síncronas diretas:

```text
Application -> Command -> MoodleClient -> HttpClient
Application -> Command -> SessionManager
Application -> Command -> HistoryManager
```

Não há event bus, fila, broker, pub/sub ou mediador central.

### Callbacks na TUI

`TuiContext` contém `std::function<void()>` para refresh, login, mkdir, upload, download e delete. `TuiApplication` registra lambdas que capturam `this`. Views invocam os callbacks sem conhecer a implementação das operações.

Esse mecanismo funciona como uma forma leve de inversão de controle entre views e orquestrador.

### Concorrência e eventos

A TUI usa:

- `refresh_thread_` para leitura remota e quota;
- `action_thread_` para operações de usuário;
- `spinner_thread_` para animação;
- `ScreenInteractive::PostEvent(Event::Custom)` para solicitar repaint.

Não há pool de threads nem executor. Antes de iniciar nova operação, o código normalmente chama `join()` na thread anterior, o que serializa ações e pode bloquear a thread chamadora até a anterior terminar.

### Async/await, promises, streams e sockets

Não são utilizados. CPR executa requisições bloqueantes. Downloads trafegam o corpo inteiro como `std::string` antes da escrita em disco; não há streaming incremental.

## 2.4 Fluxo geral de dados

```text
Entrada do usuário
   ↓
CLI11 ou evento FTXUI
   ↓
Validação superficial / normalização de caminho
   ↓
Command ou método perform_* da TUI
   ↓
SessionManager carrega URL/token/cookie
   ↓
MoodleClient escolhe REST ou AJAX
   ↓
HttpClient/CPR envia requisição
   ↓
JSON/HTML é parseado em models
   ↓
Commit de draft quando há mutação
   ↓
Persistência local opcional (histórico/sessão)
   ↓
stdout/exit code ou atualização do TuiContext
```

A validação é majoritariamente operacional, não baseada em schemas. Erros são representados por `std::error_code`; detalhes específicos do Moodle frequentemente são reduzidos a `permission_denied`, `bad_message` ou `network_unreachable`.

## 2.5 Fluxo de autenticação

1. Usuário fornece URL, CPF e senha.
2. `ShibbolethAuth::login_web` inicia uma sessão CPR.
3. GET em `/auth/shibboleth/index.php` segue redirect ao IdP.
4. Regex extrai action e inputs hidden.
5. POST envia credenciais e campos ocultos.
6. Regex extrai `SAMLResponse` e `RelayState`.
7. POST envia assertion ao Service Provider.
8. Cookie `MoodleSession` é extraído.
9. Opcionalmente, GET sem redirect em `admin/tool/mobile/launch.php` captura URL customizada com token Base64.
10. O token é decodificado e separado de `siteid`/`privatetoken`.
11. `SessionManager::save` escreve URL/username em JSON e segredos no Keyring.

### Reautenticação silenciosa

`ensure_web_session` valida:

- token permanente chamando `get_user_context_id`;
- cookie web chamando `get_draft_info`.

Se ambos não estiverem válidos simultaneamente, tenta ler credenciais do Keyring e repetir login + token extraction. Depois persiste a sessão renovada e atualiza o `MoodleClient`.

**Risco funcional:** operações que poderiam funcionar apenas com REST são impedidas quando o cookie expira e não há credenciais salvas, pois o helper exige token e cookie válidos em conjunto.

## 2.6 Fluxo de listagem

```text
list / refresh TUI
  ↓
get_user_context_id()
  ↓ core_webservice_get_site_info
extrai contextid de userpictureurl
  ↓
core_files_get_files(component=user, filearea=private, filepath=...)
  ↓
JSON files[] -> MoodleFile
  ↓
recursão por diretórios
  ↓
árvore CLI ou TuiContext::all_files
```

O contexto do usuário é armazenado em `cached_context_id_` por instância de `MoodleClient`. A listagem recursiva executa uma chamada por diretório, portanto uma árvore com `D` diretórios causa aproximadamente `D + 1` requests.

## 2.7 Fluxo de upload

### CLI

1. Carrega sessão.
2. `get_draft_info()` prefere cookie web; se não houver, prepara draft REST.
3. Para cada input:
   - arquivo: upload multipart;
   - diretório: percorre recursivamente e calcula paths remotos.
4. Registra cada arquivo em SQLite assim que o upload individual retorna sucesso.
5. Ao final, chama `commit_draft` uma vez.

### TUI

A TUI seleciona arquivos locais por uma árvore própria. Ela:

1. garante sessão;
2. obtém draft;
3. cria diretórios no draft via AJAX;
4. envia arquivos;
5. registra histórico individual;
6. commita;
7. refresha a árvore.

**Risco de consistência:** o histórico é persistido antes do commit. Se o commit falhar, o banco local informa um upload que não chegou à área privada final.

## 2.8 Fluxo de criação de diretório

```text
mkdir
  ↓
ensure_web_session
  ↓
get_draft_info(cookie)
  ↓
POST draftfiles_ajax.php?action=mkdir
  ↓
commit_draft via core_form_dynamic_form
```

`MoodleClient::create_folder` retorna sucesso sem fazer nada quando recebe `sesskey == "REST_TOKEN"`. Isso confirma que mkdir depende da sessão web/AJAX; o contrato da função, porém, não diferencia “não suportado” de “sucesso”.

## 2.9 Fluxo de deleção

1. Valida sessão web; CLI tenta fallback para draft REST se necessário.
2. Lista o diretório pai para classificar alvo como arquivo ou pasta.
3. Constrói `DeleteItem`.
4. Prioriza AJAX `deleteselected`, usando `filename="."` para diretórios.
5. Se AJAX falhar e há token, coleta arquivos recursivamente e chama `core_files_delete_draft_files`.
6. Commita o draft.

O fallback REST pode liberar dados e quota, mas deixar “pastas fantasmas”, conforme documentado em `docs/MOODLE_API.md`.

## 2.10 Fluxo de download

### Arquivo

1. Lista raiz e localiza item pelo nome.
2. A URL retornada pelo REST já recebe `?token=...` no parsing.
3. `download_file` faz GET, procura indícios de HTML do IdP e grava o corpo em binário.

### Diretório com ZIP

1. Garante cookie web.
2. Obtém draft info.
3. POST AJAX `downloadselected` com representação de diretório.
4. Recebe `fileurl` do ZIP.
5. Baixa um único artefato.
6. Se falhar, percorre diretório e baixa arquivos individualmente.

### Divergência CLI/TUI

A TUI propaga falhas de download. A CLI descarta vários retornos com `(void)moodle_client_.download_file(...)` e retorna sucesso mesmo quando a escrita ou rede falham.

## 2.11 Fluxo do updater

```text
--update
  ↓
descobre /proc/self/exe
  ↓
verifica permissão escrevendo arquivo temporário
  ↓
GET GitHub releases/latest
  ↓
compara SemVer simplificado
  ↓
GET asset .tar.gz
  ↓
salva em /tmp
  ↓
std::system("tar -xzf ...")
  ↓
copia mstorage.new e rename sobre executável
```

Não há validação de hash, assinatura, tamanho, MIME, conteúdo do archive ou origem efetiva após redirects. Como a camada HTTP desativa TLS e a documentação sugere executar o updater com `sudo`, esse fluxo é um boundary de segurança crítico.

## 2.12 Tratamento de erros

Pontos positivos:

- uso consistente de `std::expected` em APIs centrais;
- Commands retornam códigos de saída previsíveis;
- erros de parsing JSON são capturados em `call_rest`;
- SQLite utiliza prepared statements.

Limitações:

- `HttpClient` não valida status HTTP;
- vários parsers transformam erros distintos no mesmo `std::errc`;
- `zip_folder` faz parse JSON sem `try/catch`;
- recursões da TUI podem silenciar erro;
- CLI download ignora erros;
- `HistoryManager` não torna falha de inicialização observável no construtor.
