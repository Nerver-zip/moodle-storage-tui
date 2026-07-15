# 8. Guia de extensão sem romper a arquitetura

## 8.1 Princípio geral

Uma extensão deve preservar a direção:

```text
entrada (CLI/TUI) -> caso de uso -> gateway/manager -> adapter externo
```

Evite colocar CPR, SQLite, libsecret ou FTXUI em um caso de uso que não precise dessas tecnologias.

## 8.2 Adicionar um novo comando CLI

Exemplo: `mstorage rename <old> <new>`.

1. Crie `src/commands/rename_command.hpp`.
2. Derive de `Command`.
3. Injete `MoodleClient`, `SessionManager` e apenas dependências necessárias.
4. Faça `execute()` retornar `std::expected<void, std::error_code>`.
5. Adicione operação de gateway ao `MoodleClient` ou a uma facade nova; não monte payload CPR no Command.
6. Registre subcomando e opções em `Application::setup_commands()`.
7. Adicione dispatch em `Application::execute()`.
8. Adicione teste de caminho feliz e falhas em `test_commands.cpp`.
9. Se houver novo endpoint, teste parsing/contrato em `test_moodle_client.cpp`.

Melhoria recomendada antes de muitos comandos: criar uma tabela/factory de comandos para reduzir `if (got_subcommand)`.

## 8.3 Adicionar uma nova tela TUI

1. Adicione `enum class Screen` antes de ampliar estados; não continue com inteiros.
2. Crie `src/tui/views/<name>_view.hpp/.cpp`.
3. Exponha `Create<Name>View(TuiContext&)`.
4. Adicione somente estado específico necessário ao contexto ou, idealmente, crie um submodel da tela.
5. Registre callback de intenção no contexto.
6. Implemente ação no `TuiApplication` ou em um Use Case compartilhado.
7. Inclua view no `Container::Tab`.
8. Garanta transições Escape/Enter/Tab.
9. Adicione teste headless.

Não mutar `TuiContext` diretamente de worker threads. Produza resultado e aplique na UI thread.

## 8.4 Compartilhar um caso de uso entre CLI e TUI

A forma preferível para novas funcionalidades é introduzir um **Use Case sem UI**:

```cpp
class RenameUseCase {
public:
    expected<RenameResult, RenameError> execute(const RenameRequest&);
};
```

- Command traduz args para request e imprime resultado.
- TUI traduz formulário para request e renderiza resultado.
- Use Case depende de interfaces/gateways, não de stdout ou FTXUI.

Essa abordagem reduz a duplicação atual observada em upload/download/delete.

## 8.5 Adicionar uma função Moodle REST

1. Confirme o contrato na versão alvo do Moodle.
2. Adicione método de alto nível ao gateway.
3. Centralize parâmetros comuns em `call_rest`.
4. Preserve `errorcode` e mensagem em um tipo de erro próprio.
5. Valide tipo e presença dos campos JSON.
6. Não use `.value()`/conversões que possam lançar sem catch no boundary.
7. Adicione teste com resposta válida, exception Moodle, JSON inválido e campos ausentes.
8. Documente o endpoint em `docs/MOODLE_API.md`.

## 8.6 Adicionar um endpoint AJAX/Web

1. Declare explicitamente que exige capability Web/Cookie.
2. Chame `ensure_web_session`, não `ensure_rest_session`.
3. Modele `sesskey`, itemid e cookie de forma tipada.
4. Detecte redirect/login por status e URL final, não apenas substrings.
5. Valide JSON/HTML defensivamente.
6. Defina fallback REST, se possível, e documente perda de funcionalidade.

## 8.7 Adicionar outro provedor de autenticação

A implementação atual é UFPel-specific. Para suportar outro Moodle:

1. Extraia interface `AuthProvider`:

```cpp
class AuthProvider {
public:
    virtual expected<WebSession, AuthError> login(...) = 0;
    virtual expected<WebServiceToken, AuthError> issue_token(...) = 0;
};
```

2. Implemente `UfpelShibbolethProvider`.
3. Adicione `DirectMoodleProvider`, OAuth/OIDC ou browser-assisted provider conforme necessidade.
4. Crie `AuthProviderFactory` baseada em profile/config, não hostname hardcoded.
5. Mova CPR direto para um client que exponha redirects, cookies, headers e status.
6. Evite salvar senha quando provider oferecer refresh token.

## 8.8 Adicionar profiles múltiplos

Estrutura sugerida:

```text
~/.config/mstorage/
├── config.json
├── profiles/
│   ├── ufpel.json
│   └── outro.json
└── themes/
```

Keyring deve indexar por `profile_id`, não apenas URL. CLI pode receber `--profile`; TUI pode oferecer selector.

Inclua migration do formato atual e limpeza segura de registros legados.

## 8.9 Evoluir o banco

1. Crie tabela `schema_version`.
2. Aplique migrations transacionais.
3. Renomeie semanticamente `url` para `source` ou crie colunas adequadas.
4. Registre operation id, kind, local/remote path, bytes, timestamps, status, erro e confirmação de commit.
5. Use transaction para batch após commit.
6. Adicione índice por timestamp/status.

## 8.10 Trocar a implementação HTTP

Antes, remova tipos CPR da interface. Defina tipos próprios:

```cpp
struct HttpRequest { Method method; Url url; Headers headers; Body body; };
struct HttpResponse { int status; Headers headers; Bytes body; Url final_url; };
```

Depois implemente adapters CPR/libcurl. Isso permitirá política TLS central, timeout, streaming, redirects controlados, retry idempotente, métricas e testes mais expressivos.

## 8.11 Adicionar cache

Cache seguro para árvore remota deve ser associado a profile/usuário, invalidado após commit, versionado, considerado não autoritativo e armazenado sem tokens nas URLs.

## 8.12 Adicionar paralelismo

Não paralelize uploads/downloads antes de corrigir thread safety do contexto, streaming, cancelamento, rate limiting e atomicidade de output.

Depois use um pool com limite pequeno e preserve ordem/aggregate errors. Mutações no mesmo draft podem não ser seguras em paralelo sem confirmação do Moodle.

## 8.13 Adicionar um plugin system

O projeto atual não precisa de plugins para operações básicas. Se surgir demanda real:

- prefira plugins de autenticação/integração com ABI estável ou processos separados;
- evite carregar `.so` arbitrário sem trust model;
- defina capabilities e versão de contrato;
- não exponha token bruto por padrão.

## 8.14 Checklist de contribuição arquitetural

- O novo código está no módulo correto?
- UI não contém payload de rede?
- Gateway não imprime para stdout?
- Segredo não é gravado em arquivo/log?
- TLS permanece verificado?
- Erros são propagados?
- Há teste de falha, não apenas happy path?
- Paths são normalizados e confinados?
- Commit remoto foi confirmado antes do histórico local?
- Estado TUI é mutado somente pela UI thread?
- Documentação da API e arquitetura foi atualizada?
- CI/release cobre o novo target/plataforma?
