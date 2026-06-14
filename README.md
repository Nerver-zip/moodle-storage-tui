# Moodle Storage TUI 🚀

Uma ferramenta robusta de linha de comando (CLI) e interface interativa (TUI) em C++23 para gerenciar arquivos privados no Moodle (e-AULA UFPel).

## ✨ Funcionalidades

-   **Upload Automatizado:** Envio de arquivos simplificado escondendo a complexidade de tokens e rascunhos.
-   **Gerenciamento de Sessão:** Armazenamento seguro de cookies e chaves de sessão com permissões restritas.
-   **Listagem e Download:** Navegue e baixe seus arquivos privados diretamente pelo terminal.
-   **Histórico Local:** Persistência de atividades recentes em banco de dados SQLite3.
-   **Segurança:** Proteção de dados sensíveis e logs estruturados.

## 🏗️ Arquitetura

O projeto segue princípios modernos de engenharia de software:
-   **C++23:** Uso extensivo de `std::expected`, `std::filesystem` e ranges.
-   **Desacoplamento:** Camada de rede (`libcurl/cpr`) totalmente mockável para testes.
-   **Padrão Command:** Lógica de negócio encapsulada em comandos reutilizáveis por CLI e TUI.
-   **Persistência:** SQLite3 para histórico e JSON para configurações.

## 🚀 Como Começar

### Pré-requisitos
-   CMake 3.25+
-   Compilador com suporte a C++23 (GCC 13+ ou Clang 16+)
-   Dependências de sistema: `libcurl`, `sqlite3`, `openssl`.

### Build
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Uso Básico
1.  **Login:**
    Obtenha seu cookie `MoodleSession` no navegador e execute:
    ```bash
    ./moodle-storage login --url https://e-aula.ufpel.edu.br --cookie <SEU_COOKIE>
    ```

2.  **Upload:**
    ```bash
    ./moodle-storage upload documento.pdf
    ```

3.  **Listar:**
    ```bash
    ./moodle-storage list
    ```

4.  **Baixar:**
    ```bash
    ./moodle-storage download documento.pdf
    ```

## 🧪 Testes
O projeto possui uma suíte de testes extensiva utilizando GoogleTest e GoogleMock:
```bash
cd build
./tests/unit_tests
```

## 🛠️ Stack Tecnológica
-   **FTXUI:** Interface de terminal moderna.
-   **CLI11:** Parsing de argumentos de linha de comando.
-   **cpr:** Requisições HTTP simplificadas.
-   **nlohmann/json:** Manipulação de JSON.
-   **spdlog:** Logging estruturado e performante.
-   **SQLite3:** Banco de dados local.
