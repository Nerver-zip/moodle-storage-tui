# Arquitetura do Moodle Storage

> Repositório analisado: `Nerver-zip/moodle-storage-tui`  
> Nome histórico solicitado: `Nerver-zip/moodle-storage`  
> Versão observada: `v1.0.6`  
> Branch de referência: `main`

Esta pasta documenta a arquitetura interna do **Moodle Storage (`mstorage`)**, um cliente Linux em C++23 que oferece CLI e TUI para administrar a área de arquivos privados do Moodle. A documentação foi construída por inspeção do código-fonte, testes, CMake, workflows do GitHub Actions, documentação existente e histórico recente de commits.

## Como ler esta documentação

A ordem recomendada para um novo desenvolvedor é:

1. [Visão geral e estrutura do repositório](01-overview-and-repository.md)
2. [Bootstrap, fluxo de execução e fluxo de dados](02-runtime-and-data-flow.md)
3. [Módulos e componentes](03-modules-and-components.md)
4. [Integração Moodle, autenticação e persistência](04-integration-and-data.md)
5. [Build, testes e CI/CD](05-build-test-and-delivery.md)
6. [Segurança, performance e riscos](06-security-performance-and-risks.md)
7. [Padrões, convenções e decisões arquiteturais](07-patterns-decisions-and-conventions.md)
8. [Guia de extensão](08-extension-guide.md)
9. [Diagramas Mermaid](09-diagrams.md)
10. [Resumo executivo e roadmap técnico](10-executive-assessment.md)

## Mapa das seções solicitadas

| Requisito | Documento principal |
|---|---|
| 1. Visão Geral | `01-overview-and-repository.md` |
| 2. Estrutura do Repositório | `01-overview-and-repository.md` |
| 3. Fluxo Geral da Aplicação | `02-runtime-and-data-flow.md` |
| 4. Arquitetura em Alto Nível | `01-overview-and-repository.md`, `09-diagrams.md` |
| 5. Módulos | `03-modules-and-components.md` |
| 6. Fluxo de Dados | `02-runtime-and-data-flow.md` |
| 7. Componentes | `03-modules-and-components.md` |
| 8. Dependências Internas | `03-modules-and-components.md` |
| 9. Dependências Externas | `01-overview-and-repository.md`, `05-build-test-and-delivery.md` |
| 10. Modelagem de Dados | `04-integration-and-data.md` |
| 11. Comunicação entre Componentes | `02-runtime-and-data-flow.md` |
| 12. Fluxo de uma Requisição | `02-runtime-and-data-flow.md` |
| 13. Configuração | `04-integration-and-data.md` |
| 14. Sistema de Build | `05-build-test-and-delivery.md` |
| 15. Testes | `05-build-test-and-delivery.md` |
| 16. CI/CD | `05-build-test-and-delivery.md` |
| 17. Segurança | `06-security-performance-and-risks.md` |
| 18. Performance | `06-security-performance-and-risks.md` |
| 19. Pontos de Extensão | `08-extension-guide.md` |
| 20. Padrões de Projeto | `07-patterns-decisions-and-conventions.md` |
| 21. Convenções do Projeto | `07-patterns-decisions-and-conventions.md` |
| 22. Decisões Arquiteturais | `07-patterns-decisions-and-conventions.md` |
| 23. Diagramas | `09-diagrams.md` |
| 24. Resumo Executivo | `10-executive-assessment.md` |

## Legenda de confiança

- **Confirmado:** comportamento diretamente observável no código ou configuração atual.
- **Inferência:** intenção provável deduzida da estrutura, nomenclatura e histórico; deve ser validada pelos mantenedores antes de ser tratada como contrato.
- **Risco:** comportamento atual que pode provocar falha funcional, vulnerabilidade, perda de confiabilidade ou custo elevado de manutenção.

## Fonte de verdade

O código é a fonte de verdade. A documentação existente em `README.md` e `docs/MOODLE_API.md` explica a intenção do projeto, mas alguns detalhes divergem do estado atual — por exemplo, a camada HTTP desabilita verificação TLS, a quota total é fixa em 100 MiB e o arquivo `session.json` não recebe explicitamente permissão `0600` no código atual.
