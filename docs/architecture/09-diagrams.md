# 9. Diagramas arquiteturais

## 9.1 Componentes em alto nível

```mermaid
flowchart TB
    User[Usuário ou Script]
    CLI[CLI11 / Application]
    TUI[FTXUI / TuiApplication]
    CMD[Commands]
    CTX[TuiContext + Views]
    MC[MoodleClient]
    SH[ShibbolethAuth]
    SM[SessionManager]
    HM[HistoryManager]
    UP[Updater]
    HTTP[HttpClient]
    CPR[CprClient / CPR]
    KEY[(Secret Service)]
    DB[(SQLite uploads.db)]
    FS[(Filesystem local)]
    REST[Moodle REST]
    AJAX[Moodle Web/AJAX]
    IDP[Shibboleth IdP]
    GH[GitHub Releases]

    User --> CLI
    User --> TUI
    CLI --> CMD
    TUI --> CTX
    CMD --> MC
    CMD --> SM
    CMD --> HM
    CTX --> MC
    CTX --> SM
    CTX --> HM
    CLI --> UP
    MC --> HTTP
    UP --> HTTP
    HTTP --> CPR
    SM --> KEY
    SM --> FS
    HM --> DB
    MC --> REST
    MC --> AJAX
    SH --> IDP
    SH --> AJAX
    UP --> GH
```

## 9.2 Dependências internas

```mermaid
flowchart LR
    main --> core
    core --> commands
    core --> tui
    core --> network
    core --> storage
    commands --> moodle
    commands --> storage
    commands --> core_session[core/session]
    tui --> moodle
    tui --> storage
    tui --> core_session
    moodle --> network
    moodle --> models
    storage --> models
```

## 9.3 Bootstrap

```mermaid
sequenceDiagram
    participant OS
    participant Main
    participant App as Application
    participant Logger
    participant CLI as CLI11
    participant TUI as TuiApplication

    OS->>Main: main(argc, argv)
    Main->>App: construct
    App->>Logger: init()
    App->>CLI: register flags/subcommands
    Main->>App: execute(argc, argv)
    App->>CLI: parse
    alt --version
        App-->>OS: print version, exit 0
    else --update
        App->>App: Updater::perform_update
    else no subcommand
        App->>TUI: construct/run
        TUI->>TUI: ScreenInteractive::Loop
    else CLI command
        App->>App: load session + create command
        App->>App: command.execute()
        App-->>OS: exit 0/1
    end
```

## 9.4 Login Shibboleth

```mermaid
sequenceDiagram
    actor U as Usuário
    participant UI as CLI/TUI
    participant A as ShibbolethAuth
    participant I as IdP UFPel
    participant M as Moodle SP
    participant S as SessionManager
    participant K as Keyring

    U->>UI: URL + CPF + senha
    UI->>A: login_web
    A->>M: GET /auth/shibboleth/index.php
    M-->>A: redirect/página IdP
    A->>I: POST credenciais + hidden fields
    I-->>A: HTML SAMLResponse + RelayState
    A->>M: POST SAML assertion
    M-->>A: MoodleSession
    opt token permanente
        A->>M: GET mobile launch sem redirect
        M-->>A: mstorageapp://token=BASE64
        A->>A: decode token
    end
    UI->>S: save SessionData
    S->>K: store token/cookie/credentials
    S->>S: write session.json metadata
```

## 9.5 Upload com draft

```mermaid
sequenceDiagram
    participant UI as Command/TUI
    participant S as SessionManager
    participant M as MoodleClient
    participant R as Moodle REST/AJAX
    participant H as HistoryManager

    UI->>S: load session
    UI->>M: get_draft_info
    M->>R: prepare private files or scrape web draft
    R-->>M: draftitemid/sesskey
    loop arquivos
        UI->>M: upload_file(local, remote, draft)
        M->>R: multipart upload
        R-->>M: response
        UI->>H: record_upload (atual: antes do commit)
    end
    UI->>M: commit_draft
    M->>R: update private files / dynamic form
    R-->>UI: result
```

## 9.6 Download de diretório

```mermaid
sequenceDiagram
    participant UI as CLI/TUI
    participant M as MoodleClient
    participant A as Moodle AJAX
    participant R as Moodle REST
    participant F as Filesystem

    UI->>M: list_files
    M->>R: core_files_get_files
    R-->>M: folder node
    alt ZIP habilitado + cookie válido
        UI->>M: zip_folder
        M->>A: downloadselected(folder)
        A-->>M: fileurl ZIP
        UI->>M: download_file(fileurl)
        M->>F: write ZIP
    else fallback
        loop cada diretório
            M->>R: core_files_get_files(path)
            loop cada arquivo
                M->>R: GET URL com token
                M->>F: write file
            end
        end
    end
```

## 9.7 Deleção híbrida

```mermaid
flowchart TD
    Start[Delete request] --> Draft[Obter draft]
    Draft --> Classify[Classificar file/folder]
    Classify --> Cookie{Cookie web válido?}
    Cookie -- Sim --> Ajax[AJAX deleteselected]
    Ajax --> AjaxOk{Sucesso?}
    AjaxOk -- Sim --> Commit[Commit draft]
    AjaxOk -- Não --> Token
    Cookie -- Não --> Token{Token REST válido?}
    Token -- Sim --> Collect[Coletar arquivos recursivamente]
    Collect --> RestDelete[core_files_delete_draft_files]
    RestDelete --> Commit
    Token -- Não --> Error[Erro]
    Commit --> Done[Concluído]
```

## 9.8 Estado da TUI

```mermaid
stateDiagram-v2
    [*] --> Login: sem sessão
    [*] --> Browser: sessão existente
    Login --> Browser: autenticação válida
    Browser --> Mkdir
    Browser --> Upload
    Browser --> Download
    Browser --> Delete
    Browser --> History
    Browser --> MainMenu
    MainMenu --> Settings
    Settings --> Themes
    Mkdir --> Browser: sucesso/Esc
    Upload --> Browser: sucesso/Esc
    Download --> Browser: sucesso/Esc
    Delete --> Browser: sucesso/Esc
    History --> Browser: Esc
    Themes --> Settings: aplicar/Esc
    Settings --> Browser: Esc
```

## 9.9 Classes centrais

```mermaid
classDiagram
    class Application {
      -CLI::App app_
      -CprClient http_client_
      -SessionManager session_manager_
      -HistoryManager history_manager_
      +execute(argc, argv) int
    }

    class Command {
      <<interface>>
      +execute() expected~void,error_code~
    }

    class MoodleClient {
      -HttpClient& client_
      -string wstoken_
      -string web_cookie_
      -int cached_context_id_
      +call_rest()
      +list_files()
      +upload_file()
      +download_file()
      +delete_items()
      +commit_draft()
    }

    class HttpClient {
      <<interface>>
      +get()
      +post()
      +post_raw()
      +post_multipart()
    }

    class CprClient
    class SessionManager
    class HistoryManager
    class TuiApplication
    class TuiContext

    Application --> Command
    Application --> TuiApplication
    Application --> CprClient
    Application --> SessionManager
    Application --> HistoryManager
    Command --> MoodleClient
    MoodleClient --> HttpClient
    CprClient ..|> HttpClient
    TuiApplication *-- TuiContext
    TuiContext --> SessionManager
    TuiContext --> HistoryManager
    TuiContext --> MoodleClient
```

## 9.10 Pipeline de build e release

```mermaid
flowchart LR
    Push[Push/PR source] --> CI[CMake CI]
    CI --> Checkout
    Checkout --> Deps[Deps + GCC 13 + Keyring]
    Deps --> Configure[CMake Release]
    Configure --> Build
    Build --> Test[ctest em dbus-run-session]

    Tag[Tag v*] --> CD[Release CD]
    CD --> RBuild[Build Release]
    RBuild --> Package[tar.gz com mstorage]
    Package --> Release[GitHub Release]
    Release --> Updater[--update baixa asset]
```
