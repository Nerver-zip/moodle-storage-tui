# 10. Resumo executivo, pontos fortes, limitações e roadmap

## 10.1 Visão executiva

O Moodle Storage é um **cliente desktop terminal em C++23**, organizado como monólito modular, que encapsula uma integração Moodle incomum e complexa. Seu maior mérito arquitetural é transformar dois protocolos incompletos — REST e Web/AJAX — em uma experiência coerente de sistema de arquivos, compartilhada por CLI e TUI.

A base é adequada ao tamanho atual: entrypoint mínimo, Commands, gateway Moodle, porta HTTP mockável, Keyring, SQLite e testes. A complexidade real está na integração externa e no lifecycle de sessão, não no número de classes.

Entretanto, o projeto apresenta riscos críticos de segurança no transporte e no updater. Antes de expandir funcionalidades, é necessário restabelecer TLS, autenticar releases e corrigir propagação de erros/concurrency.

## 10.2 Pontos fortes

1. **Problema real bem encapsulado:** draft areas e fallbacks não vazam para o usuário.
2. **CLI e TUI no mesmo core:** distribuição simples e UX flexível.
3. **C++ moderno:** `std::expected`, filesystem e separação por namespaces.
4. **Rede mockável:** `HttpClient` habilita testes determinísticos.
5. **Conhecimento de integração documentado:** `docs/MOODLE_API.md` reduz risco de conhecimento tribal.
6. **Persistência segura em intenção:** uso de Secret Service em vez de plaintext.
7. **Fallback operacional:** AJAX para diretórios/ZIP e REST para disponibilidade.
8. **TUI modularizada em views:** melhor do que um único arquivo de renderização.
9. **Build reprodutível em CI:** GCC 13 e Ubuntu 22.04 fixados.
10. **Release automatizada:** artefato pronto para usuário final.

## 10.3 Pontos fracos

1. TLS desabilitado globalmente.
2. Updater sem checksum/assinatura e com extração via shell.
3. TUI com data races e contexto excessivamente amplo.
4. Duplicação de casos de uso CLI/TUI.
5. Erros HTTP e Moodle pouco expressivos.
6. CLI download pode reportar sucesso falso.
7. Histórico não é transacional com commit remoto.
8. Quota total fixa.
9. SSO altamente específico e não mockável.
10. Configuração espalhada e paths XDG ignorados.
11. Modelos usam sentinelas (`REST_TOKEN`, `.`, `DIR`).
12. Testes focam happy paths e payloads frouxos.
13. Sem streaming para arquivos grandes.
14. Apenas uma conta/profile.
15. Release sem provenance/SBOM/checksum.

## 10.4 Complexidade geral

| Dimensão | Avaliação | Motivo |
|---|---|---|
| Tamanho de código | Baixo/Médio | poucos módulos e um binário |
| Complexidade de domínio | Média | sistema de arquivos remoto e draft transactions |
| Complexidade de integração | Alta | REST + AJAX + SAML + Keyring + GitHub |
| Concorrência | Média/Alta | três threads e estado compartilhado |
| Operação | Baixa | sem servidor, estado local |
| Segurança | Alta criticidade | credenciais e updater |
| Testabilidade | Média | HTTP mockável, SSO/Keyring/concurrency menos isolados |
| Extensibilidade | Média | Commands e módulos ajudam, mas concretos e duplicação limitam |

## 10.5 Roadmap técnico priorizado

### P0 — Segurança imediata

- habilitar TLS verificado em todas as requisições;
- remover fallback inseguro global;
- autenticar release assets;
- remover `std::system(tar)` e paths temporários previsíveis;
- não recomendar updater privilegiado sem verificação forte;
- remover/redigir dump do IdP.

### P1 — Correção e confiabilidade

- propagar falhas de download CLI;
- validar status HTTP, headers e content type;
- registrar histórico somente após commit;
- tornar estado TUI thread-safe;
- adicionar writes atômicos e permissões explícitas;
- capturar JSON inválido no ZIP;
- adicionar testes negativos e sanitizers.

### P2 — Arquitetura e experiência

- separar capabilities REST/Web;
- tipos próprios para paths, sessions e errors;
- extrair Use Cases compartilhados;
- quota dinâmica;
- profiles múltiplos;
- respeitar XDG Base Directory;
- config central e timeouts;
- enum de telas.

### P3 — Escala e performance

- streaming;
- progress/cancelamento;
- cache incremental;
- worker pool limitado;
- virtualização de árvore;
- batch/transações no histórico;
- packages nativos e mais arquiteturas.

## 10.6 Arquitetura alvo incremental

Não é necessário reescrever tudo. Uma evolução segura pode ocorrer em camadas:

```text
CLI Adapter ─┐
             ├─> Use Cases ─> MoodleStorageGateway interface
TUI Adapter ─┘                    │
                                  ├─ RestMoodleApi
                                  ├─ WebMoodleApi
                                  └─ AuthProvider

Use Cases ─> SessionRepository interface ─> LibsecretSessionRepository
Use Cases ─> OperationHistory interface ─> SqliteOperationHistory
```

Fases:

1. enriquecer HTTP response e corrigir TLS;
2. introduzir tipos de erro/path/session sem mover arquivos;
3. extrair um Use Case por operação duplicada;
4. separar REST/Web internamente mantendo `MoodleClient` como facade;
5. mudar TUI para aplicar resultados na UI thread;
6. adicionar providers/profiles quando houver demanda.

## 10.7 Critérios de arquitetura saudável

O projeto estará em um estado arquitetural forte quando:

- nenhuma credencial trafegar sem TLS verificado;
- updater rejeitar qualquer artefato não autenticado;
- Commands e TUI compartilharem o mesmo núcleo de casos de uso;
- erro remoto chegar ao usuário com contexto e código;
- nenhuma operação reportar sucesso sem confirmar efeitos;
- ThreadSanitizer passar;
- downloads grandes não precisarem caber na RAM;
- profiles e capabilities forem explícitos;
- CI cobrir segurança, sanitizers e release artifact;
- documentação e código concordarem sobre os contratos.

## 10.8 Conclusão

A arquitetura atual é uma base funcional e compreensível, não um protótipo descartável. O uso de módulos, Commands, gateway e abstração HTTP demonstra uma direção correta. O maior retorno não virá de adotar frameworks ou aumentar o número de camadas, mas de fortalecer boundaries, eliminar estados implícitos e unificar casos de uso.

A prioridade absoluta é segurança: o mesmo cliente que protege segredos em repouso atualmente desativa a proteção de transporte e aceita atualizar seu próprio executável sem verificação de integridade. Corrigidos esses pontos, o projeto tem uma trajetória clara para se tornar um cliente Moodle terminal robusto, extensível e confiável.
