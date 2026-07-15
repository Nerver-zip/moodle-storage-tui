# 6. Segurança, performance e riscos técnicos

## 6.1 Modelo de ameaça resumido

O cliente manipula ativos sensíveis: senha institucional, cookie `MoodleSession`, token permanente, arquivos pessoais/acadêmicos e o próprio binário atualizado automaticamente.

As fronteiras não confiáveis são a rede, respostas HTML/JSON, URLs de download, arquivos locais, archives de release, diretórios temporários, configuração de tema e o Keyring/D-Bus indisponível.

## 6.2 Controles positivos existentes

- segredos principais são enviados ao Secret Service, não ao JSON;
- entrada de senha CLI desabilita echo;
- SQL de insert/select usa prepared statements;
- API REST encapsula token e detecta exceções Moodle;
- downloads procuram indícios de página de login antes de escrever;
- logout tenta limpar todos os tipos de segredo;
- testes isolam HOME e limpam credenciais;
- `std::expected` torna várias falhas explícitas;
- o update usa rename para reduzir janela de substituição parcial.

## 6.3 Risco crítico: TLS desabilitado

`CprClient` passa `cpr::VerifySsl{false}` em GET, POST, raw e multipart. `ShibbolethAuth` também chama `SetVerifySsl(false)` e desabilita TLS na captura de token.

Impacto:

- atacante na rede pode ler ou alterar credenciais, cookie e token;
- respostas Moodle podem ser adulteradas;
- downloads podem ser substituídos;
- metadata e artefato do updater podem ser adulterados;
- SAML deixa de ter a proteção de transporte esperada.

Esse comportamento invalida parte da segurança proporcionada pelo Keyring: segredos estão protegidos em repouso, mas não em trânsito.

Correção prioritária:

1. TLS verificado por padrão;
2. suporte a CA bundle configurável para ambientes legados;
3. nunca usar disable verification como fallback automático;
4. testes com certificado inválido;
5. logging claro do erro de cadeia de confiança.

## 6.4 Risco crítico: auto-update não autenticado

O updater baixa metadata e archive por uma camada sem TLS verificado, não valida checksum/assinatura, executa `tar` por shell, extrai em diretório temporário global, substitui o executável e recomenda `sudo` se não houver permissão.

Cenário de impacto: execução remota de código com privilégios root.

Correção recomendada:

- publicar SHA-256 assinado ou usar Sigstore;
- verificar assinatura/provenance antes de extrair;
- usar biblioteca de archive sem shell;
- rejeitar entries absolutos, `..`, symlinks e hardlinks perigosos;
- usar diretório temporário exclusivo `0700`;
- fazer fsync e rollback;
- evitar executar updater como root; usar package manager quando instalado no sistema.

## 6.5 Risco alto: dump de HTML de autenticação

Quando o IdP não retorna SAMLResponse, o código grava toda a página em `/tmp/mstorage_idp_error.html`.

Esse HTML pode conter tokens hidden, identificador do usuário, estado de autenticação e detalhes internos do IdP. O nome previsível também permite colisão/symlink attack. O dump deve ser removido por padrão ou criado apenas com flag de diagnóstico, caminho exclusivo e permissão `0600`, após redaction.

## 6.6 Risco alto: erros HTTP não validados

`CprClient` considera sucesso qualquer resposta sem erro de transporte. Um HTTP 401, 404, 500 ou 502 retorna body como sucesso.

Consequências:

- HTML de proxy pode ser parseado como JSON;
- upload pode ser contado como sucesso;
- updater pode salvar página de erro como tarball;
- fallback decisions ficam imprecisas.

O resultado HTTP deve incluir status, headers, body e URL final, com política por endpoint.

## 6.7 Risco de integridade: histórico antes do commit

CLI e TUI registram cada arquivo após upload ao draft, antes do commit final. Uma falha no commit gera histórico falso.

Correção:

- acumular resultados em memória;
- commitar;
- inserir histórico em transação SQLite somente após sucesso;
- opcionalmente registrar falhas com status explícito.

## 6.8 Risco funcional: CLI download retorna sucesso em falhas

`DownloadCommand` ignora erros de `download_file` em arquivos, ZIP e recursão. O comando pode terminar com exit code 0 sem criar um arquivo válido.

Correção:

- propagar o primeiro erro ou produzir aggregate result;
- remover `(void)`;
- verificar tamanho/flush/close;
- escrever em `.part` e renomear após sucesso;
- testar falhas de rede e disco.

## 6.9 Risco de precisão: quota total hardcoded

`MoodleClient::get_usage` usa `100 * 1024 * 1024` como total. A mesma constante aparece no contexto TUI.

Impacto: barra e percentual incorretos para instalações/usuários com quota diferente. Deve usar campo retornado por `core_user_get_private_files_info` quando disponível e representar quota ilimitada/ausente.

## 6.10 Risco de sessão: capabilities acopladas

`ensure_web_session` exige token e cookie válidos. Isso força re-login em operações REST-only e impede uso do token permanente quando o usuário optou por não salvar senha.

Recomendação: modelar capabilities independentes: `ensure_rest_session()`, `ensure_web_session()` ou `ensure_capabilities({Rest, Web})`.

## 6.11 Risco de concorrência na TUI

A TUI possui três threads. Apenas parte de `all_files`, `usage` e loading é protegida por `data_mutex`. Muitos campos de status, `active_tab`, sets e strings são escritos por worker threads e lidos pelo renderer sem sincronização.

`loading` é `bool` comum lido pelo spinner thread e escrito pelo refresh thread, configurando data race em C++.

Consequências: comportamento indefinido, repaint inconsistente, crashes raros, corrupção de string/set e dificuldades de reprodução.

Correção arquitetural:

- worker produz resultado imutável;
- resultado é postado para a UI thread;
- somente UI thread muta `TuiContext`;
- `std::jthread` + `stop_token` para lifecycle;
- `enum class Screen` em vez de int;
- fila thread-safe de actions/results;
- ThreadSanitizer no CI.

## 6.12 Riscos de path e filesystem

- upload recursivo precisa de política explícita para symlinks e permissões;
- remote paths são concatenados como strings;
- download usa nomes remotos para criar caminhos locais;
- nome malicioso pode incluir `../`;
- archive updater pode conter traversal;
- arquivos são gravados diretamente no destino, sem `.part`.

Recomendação: tipos `RemotePath`/`SafeLocalPath`, normalização e validação de containment.

## 6.13 Riscos de memória e arquivos grandes

A interface HTTP retorna `std::string`, portanto downloads e respostas são carregados inteiramente na memória. Um arquivo de vários gigabytes pode exceder memória e causar cópias.

Melhoria: streaming CPR/libcurl para file sink, progress callbacks, limites de body, cancelamento e resume/range quando suportado.

## 6.14 Performance atual

### Otimizações existentes

- ccache no build;
- context id cache por cliente;
- ZIP server-side;
- threads para não congelar visualmente a TUI;
- uma conexão SQLite persistente;
- library core estática;
- listagem de diretórios ordenada localmente.

### Gargalos

#### Listagem N+1

A árvore inteira é obtida recursivamente, uma request por diretório, tanto na CLI quanto no refresh TUI. Refresh sempre repete tudo.

#### Operações sequenciais

Uploads e downloads recursivos são sequenciais. Isso reduz pressão no servidor, mas limita throughput.

#### Revalidação cara

`ensure_web_session` faz pelo menos uma chamada REST e uma web; em casos de uso frequentes, adiciona latência significativa.

#### Logger em flush debug

Cada mensagem debug força flush; em operações com muitos arquivos, pode aumentar I/O.

#### Renderização e estado

`update_visible_files` recalcula strings e largura para todos os itens. Para árvores grandes, virtualização/paginação ajudaria.

## 6.15 Estratégias de otimização seguras

1. corrigir segurança e correção antes de paralelizar;
2. streaming de downloads/uploads;
3. cache de árvore com refresh incremental;
4. limitar concorrência com pequeno worker pool;
5. batch de histórico em transação;
6. cache de validade da sessão com TTL curto;
7. paginação/virtualização na TUI;
8. benchmark com árvores e arquivos representativos.

## 6.16 Matriz de riscos

| Risco | Severidade | Probabilidade | Prioridade |
|---|---:|---:|---:|
| TLS desabilitado | Crítica | Alta | P0 |
| Updater sem integridade | Crítica | Média/Alta | P0 |
| Data races na TUI | Alta | Média | P1 |
| CLI download silencia falha | Alta | Média | P1 |
| HTML sensível em `/tmp` | Alta | Média | P1 |
| Status HTTP ignorado | Alta | Alta | P1 |
| Histórico antes do commit | Média | Média | P1 |
| Quota fixa | Média | Alta | P2 |
| Sessão REST/Web acoplada | Média | Alta | P2 |
| Listagem N+1 | Média | cresce com uso | P2 |
| Ausência de streaming | Média | cresce com tamanho | P2 |
| Paths temporários previsíveis | Alta | Baixa/Média | P1 |
