# 4. Integração Moodle, modelagem de dados, configuração e persistência

## 4.1 O modelo híbrido REST + Web/AJAX

O projeto mantém dois canais de autenticação e dois conjuntos de endpoints:

| Canal | Credencial | Vantagens | Usos principais |
|---|---|---|---|
| REST Web Services | `wstoken` | estável e adequado a automação | listar, upload, quota, preparar/commit de draft, delete de arquivos |
| Web/AJAX | `MoodleSession` + `sesskey` | acessa funções da UI web | mkdir, delete de diretório, ZIP server-side |

Essa duplicidade não é acidental; é uma resposta às limitações reais da API Moodle observadas no projeto.

## 4.2 Draft areas como unidade transacional

Para mutações, o Moodle trabalha em duas fases:

1. preparar uma cópia editável da área privada;
2. aplicar uploads/deletes/mkdir no draft;
3. commitar o draft para a área real.

`MoodleClient::DraftInfo` contém:

- `sesskey` — CSRF/session key do fluxo web ou sentinela `REST_TOKEN`;
- `itemid` — id da draft area;
- `contextid` — contexto Moodle.

A sentinela `REST_TOKEN` permite que o mesmo tipo seja usado para dois protocolos, mas cria branches implícitos em vários métodos. Um tipo variante explícito reduziria estados inválidos.

## 4.3 REST calls

`call_rest(function, params)` sempre adiciona:

- `wstoken`;
- `wsfunction`;
- `moodlewsrestformat=json`.

Funções observadas:

- `core_webservice_get_site_info`;
- `core_user_prepare_private_files_for_edition`;
- `core_user_update_private_files`;
- `core_files_get_files`;
- `core_user_get_private_files_info`;
- `core_files_delete_draft_files`.

Erros Moodle com campo `exception` são convertidos para `permission_denied`, perdendo `errorcode` como tipo programático, embora o valor seja logado.

## 4.4 Descoberta do context id

O context id é extraído da URL da imagem de perfil retornada por `core_webservice_get_site_info`, usando regex `/pluginfile.php/(\d+)/`.

Esse é um workaround de engenharia reversa. É cacheado por `MoodleClient`, mas depende da forma atual da URL e pode falhar se o servidor alterar CDN, endpoint ou privacidade da imagem.

## 4.5 Representação de arquivos e diretórios

`models::MoodleFile`:

| Campo | Uso |
|---|---|
| `filename` | nome do arquivo; `.` é marcador interno de diretório em parte do fluxo |
| `filepath` | caminho remoto com barras |
| `url` | URL de download, enriquecida com token no REST |
| `size` | bytes |
| `size_f` | string formatada ou `DIR` na camada TUI |
| `datemodified` | timestamp Moodle |

Há mistura de dado e apresentação: `size_f == "DIR"` é usado para classificar diretório na TUI, enquanto o gateway inicialmente usa `filename == "."`. Isso cria dois protocolos internos implícitos.

Recomendação: adicionar `enum class NodeType { File, Directory }` e manter tamanho formatado apenas na apresentação.

## 4.6 Dados de sessão

`models::SessionData` contém `moodle_url`, `wstoken` e `web_cookie`.

### `session.json`

Local: `~/.config/mstorage/session.json`.

Conteúdo atual:

```json
{
  "moodle_url": "https://e-aula.ufpel.edu.br",
  "username": "... opcional ..."
}
```

Token, cookie e senha não são gravados nesse arquivo.

### Keyring

O Secret Service armazena três registros indexados por Moodle URL e tipo. Credenciais usam username como atributo e senha como secret.

### Lifecycle

- login cria/atualiza;
- silent reauth atualiza token/cookie;
- logout limpa registros para a URL do JSON e remove o arquivo.

### Limitações

- apenas um profile ativo por vez, porque existe um único `session.json`;
- se o JSON for removido sem limpar Keyring, segredos podem ficar órfãos;
- se a URL for alterada, registros antigos podem persistir;
- não há versão de schema;
- gravação não é atômica;
- username é metadado local, embora possa ser identificador pessoal.

## 4.7 Banco SQLite

### Arquivo

`~/.config/mstorage/uploads.db`

### Tabela

```sql
CREATE TABLE IF NOT EXISTS history (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  filename TEXT NOT NULL,
  url TEXT NOT NULL,
  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

### Relacionamentos

Não há relacionamentos nem foreign keys. É uma tabela append-only, exceto pelo clear total.

### ORM e migrations

Não há ORM. A aplicação usa diretamente a C API do SQLite. Não existem migrations versionadas; `CREATE TABLE IF NOT EXISTS` é a única inicialização.

### Semântica real

A coluna `url` é usada como “origem/tipo” (`REST_API`, `TUI`) em uploads recentes. O nome da coluna não representa mais adequadamente o dado. Isso deve ser corrigido em uma migration futura.

### Consistência

O histórico registra sucesso do upload ao draft, não sucesso do commit final. Consequentemente, ele é um log de tentativa parcial, embora a UI o apresente como histórico concluído.

## 4.8 Temas

Diretório: `~/.config/mstorage/themes/`.

- arquivos `.conf`: definição de cores;
- `default.conf`: cópia do tema ativo;
- temas padrão: gerados no primeiro uso a partir de strings no binário.

Não há variável de ambiente para mudar esse diretório. O projeto usa diretamente `$HOME/.config`, em vez de respeitar `XDG_CONFIG_HOME`.

## 4.9 Logs

Diretório: `~/.local/share/mstorage/mstorage.log`.

O caminho não respeita `XDG_DATA_HOME`. O logger escreve em debug, sincronamente, sem rotação. Logs podem conter URLs, mensagens de exceção e detalhes de autenticação; o código não implementa redaction central.

## 4.10 Arquivos temporários

- `/tmp/mstorage_idp_error.html`: dump do HTML quando login falha;
- `/tmp/mstorage-update.tar.gz`: pacote de atualização;
- `/tmp/mstorage`: binário extraído esperado;
- `.mstorage_update_test`: teste transitório no diretório do executável.

Esses nomes são previsíveis e compartilhados. Em sistemas multiusuário, devem ser substituídos por diretórios temporários exclusivos com permissões restritas.

## 4.11 Configuração de runtime

O projeto não possui `.env`, config loader central ou flags para a maioria dos parâmetros. Configurações são distribuídas no código:

| Configuração | Local atual |
|---|---|
| URL padrão da TUI | `TuiContext::login_url` |
| GitHub repo do updater | `Updater::perform_update` |
| asset name | `Updater::perform_update` |
| quota total | `MoodleClient::get_usage`, default no `TuiContext` |
| User-Agent | `CprClient` |
| SSL verification | `CprClient`, `ShibbolethAuth` |
| paths XDG-like | SessionManager, HistoryManager, Logger, TUI |
| temas padrão | `TuiContext::ensure_default_themes` |
| versão | `core/version.hpp` |

Uma futura `AppConfig` deveria centralizar defaults, paths, endpoints, timeouts e política TLS.

## 4.12 Variáveis de ambiente

A única variável consumida diretamente é `HOME`. Testes a sobrescrevem para isolar arquivos. Não há suporte explícito a:

- `XDG_CONFIG_HOME`;
- `XDG_DATA_HOME`;
- `XDG_CACHE_HOME`;
- proxy customizado;
- CA bundle;
- timeout;
- log level;
- profile ativo.

## 4.13 Modelagem ausente ou implícita

O sistema não possui entidades de domínio ricas. Conceitos importantes permanecem implícitos:

- `MoodleProfile`;
- `AuthenticatedSession` com capabilities REST/Web;
- `RemotePath` normalizado;
- `DraftTransaction`;
- `TransferResult`;
- `OperationHistory` com status;
- `Quota` retornada pelo servidor;
- `ReleaseArtifact` verificado.

Formalizar esses tipos reduziria uso de strings sentinela e estados inconsistentes.
