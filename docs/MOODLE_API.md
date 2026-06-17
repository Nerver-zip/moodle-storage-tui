# Arquitetura de Integração Moodle (Moodle Storage API)

Este documento descreve as escolhas técnicas, fluxos de rede, limitações e as descobertas de engenharia reversa que baseiam o cliente C++ (`moodle-storage`) para se comunicar com o e-AULA UFPel (Moodle 5.0.x).

## 1. O Paradigma da Arquitetura Híbrida

Ao longo do desenvolvimento, descobrimos que nenhuma API única do Moodle atende a todos os requisitos de uma ferramenta CLI de alta performance e livre de falhas de autenticação. Por isso, adotamos uma **Arquitetura Híbrida**:

1.  **API REST (Web Services):** Usada para 95% das operações (`list`, `upload`, `usage`, `history`). Baseia-se em um **Token Permanente** (`wstoken`), garantindo estabilidade imune a timeouts de sessão.
2.  **API AJAX (Web Frontend):** Usada como *fallback* cirúrgico exclusivamente para a deleção de diretórios (`delete`) e compressão de pastas (`download -zip`). Baseia-se em um **Cookie Temporário** (`MoodleSession`).

---

## 2. Autenticação e Gestão de Estado

### 2.1. O Problema do Shibboleth (IdP)
A UFPel não utiliza autenticação direta no banco de dados do Moodle. O login é delegado a um Identity Provider (IdP) via protocolo SAML 2.0. Isso impede o uso da função REST padrão de obter tokens enviando usuário e senha diretamente.

### 2.2. A Automação C++ (Simulação de Navegador)
Para obter uma sessão sem abrir o navegador do usuário, o sistema realiza um Scraping automatizado de alta velocidade:
1.  **Início:** Um `GET` em `/auth/shibboleth/index.php` desencadeia um redirect para o IdP (`idpv3.ufpel.edu.br`).
2.  **Extração de Tokens Ocultos:** O C++ lê a página de login do IdP e usa regex para extrair inputs do tipo `hidden` cruciais para o SAML, como o `RelayState` e `AuthState`.
3.  **A Submissão:** Um `POST` injeta `username` (CPF) e `password` (junto com os ocultos) na action do formulário. Note que os parâmetros no formulário da UFPel chamam-se `username` e `password` (e não `j_username` como em instalações Shibboleth puras).
4.  **O Redirecionamento Final:** O IdP retorna uma página HTML contendo um grande payload base64 chamado `SAMLResponse`. O C++ o extrai e faz o POST final para o SP (Service Provider) em `/Shibboleth.sso/SAML2/POST`.
5.  **Resultado:** O Moodle gera o cookie `MoodleSession`.

### 2.3. Sequestro do Token Mobile
O cookie da Web expira em poucas horas. Para obter persistência infinita, "fingimos" ser o Aplicativo Moodle Mobile.
*   **A Rota:** Fazemos um `GET` para `/admin/tool/mobile/launch.php?service=moodle_mobile_app&passport=12345`. Passamos o nosso `MoodleSession` recém-adquirido.
*   **O Comportamento:** Configuramos o cliente HTTP (CPR) com `Redirects{0}` para impedir que ele siga os redirecionamentos.
*   **A Extração:** O Moodle retorna um HTTP 303 com o cabeçalho `Location: moodlemobile://token=BASE64`.
*   **A Decodificação:** Decodificamos o Base64, que resulta em `siteid:::<TOKEN_MD5>`. Extraímos apenas o Token MD5. Este é o nosso `wstoken` imortal.

### 2.4. Gnome Keyring (libsecret)
Para garantir segurança:
*   A aplicação utiliza a biblioteca `libsecret-1` via chamadas DBus (`secret_password_store_sync`) para salvar o `wstoken`, o `web_cookie` e as credenciais (`username/password`) no chaveiro criptografado do sistema operacional (Gnome Keyring, KWallet).
*   O arquivo `.config/mstorage/session.json` guarda apenas a URL não sensível. O aplicativo está blindado contra roubo direto de tokens em disco.

---

## 3. A API REST (O Core Estável)

Toda a comunicação REST segue a regra de enviar payload URL-Encoded (ou Multipart) para `webservice/rest/server.php`.

### Context Level & Instance ID
Um ponto crítico descoberto é que muitas funções REST de gerenciamento de arquivos rejeitam requisições globais. Elas exigem um `contextid` atrelado ao usuário.
*   **Descoberta:** Encontramos o Context ID "vazando" na URL da imagem de perfil retornada pela função `core_webservice_get_site_info` (ex: `/pluginfile.php/23694/user/...`). O C++ extrai esse número (23694) via regex e faz o cache dele para o restante das requisições.

### 3.1. Listagem de Arquivos (`mstorage list`)
*   **Função:** `core_files_get_files`
*   **Parâmetros Mágicos:** Passamos `component=user`, `filearea=private`, `filepath=/` e o `contextid` extraído.
*   **Representação de Diretórios:** O Moodle REST sinaliza uma pasta não pelo seu nome, mas setando a flag `isdir: true` em um registro cujo caminho físico existe. No parsing, convertemos a representação `isdir=true` em uma estrutura hierárquica usando um marcador visual de "nome oculto" para pastas pai.

### 3.2. Fluxo de Edição & Rascunhos (Draft Areas)
No Moodle, **você nunca edita a área privada diretamente**. Você cria um "Rascunho" (Draft), edita-o e faz o "Commit".
1.  **Prepare:** Chamamos `core_user_prepare_private_files_for_edition`. O Moodle clona sua área privada inteira para uma área temporária em RAM/Disco e nos retorna um ID numérico (`draftitemid`).
2.  **Upload/Delete:** Trabalhamos com os arquivos usando esse `draftitemid`.
3.  **Commit:** Chamamos `core_user_update_private_files` passando o `draftitemid`. O Moodle sincroniza as alterações sobrepondo o estado final do rascunho na área real.

### 3.3. O Envio (Upload)
Diferente das requisições URL-Encoded, o upload utiliza um arquivo dedicado (`webservice/upload.php`).
*   Injetamos o Token na query string (`?token=...`).
*   Usamos `cpr::Multipart` para envelopar o arquivo físico de forma compatível com os Web Services, injetando os parâmetros `itemid` e `filepath`.

---

## 4. Limitações Críticas da API REST e o Fallback AJAX

### 4.1. O Problema das "Pastas Fantasmas"
A descoberta técnica mais dolorosa foi que **a API REST do Moodle 5.x não permite apagar registros de diretórios**.
*   Se você apagar todos os arquivos dentro da pasta `fotos/` via REST e der o Commit, os arquivos somem, mas o registro vazio da pasta `fotos/` persiste para sempre no Moodle.
*   Tentativas de passar `filename="."` ou passar o nome da pasta no endpoint `core_files_delete_draft_files` resultam no erro: `Invalid external api parameter: the value is ".", the server was expecting "file" type`.

### 4.2. A Solução: Arquitetura de Fallback Transparente
Para garantir que o comando `mstorage delete pasta/` realmente remova o lixo do banco de dados, o `MoodleClient` foi desenhado com inteligência de contingência:

1.  **Prioridade AJAX:** O sistema recupera o `web_cookie` temporário do Keyring e submete um payload JSON super especializado para a URL antiga: `/repository/draftfiles_ajax.php?action=deleteselected`.
    *   *O Payload Mágico:* O AJAX aceita a deleção de pastas inteiras passando a representação interna de diretório do Moodle: `[{"filepath": "/nome_da_pasta/", "filename": "."}]`.
2.  **Re-autenticação Silenciosa (Self-Healing):** Se a requisição AJAX falhar (o cookie expirou), o sistema **lê sua senha criptografada no Keyring**, refaz o fluxo Shibboleth (SAML) de fundo sem você perceber, salva o novo cookie, e submete a deleção novamente.
3.  **Rest Deletion (O Último Recurso):** Se por algum motivo catastrófico a renovação de sessão web falhar, o código recua para a deleção manual via REST (`collect_files_recursive`), percorrendo cada subdiretório, achando cada arquivo individual e apagando-os. A pasta ficará vazia (fantasma), mas os dados e o espaço do servidor estarão livres.

---

## 5. Downloads Nativos de Diretórios

O mesmo princípio de fallback descrito acima foi aplicado para o comando `download -r`.
Ao baixar uma pasta:
1.  **Prioridade AJAX:** Tentamos usar `/repository/draftfiles_ajax.php?action=downloadselected` enviando o payload de pasta (com `filename="."`).
2.  **Vantagem Extrema:** O servidor Moodle executa um processo de Compressão ZIP Server-Side (nativamente no backend PHP) dezenas de vezes mais rápido, e devolve apenas a URL de um único arquivo `.zip` pronto para download.
3.  **REST Fallback:** Se não houver web session e a reautenticação falhar, o C++ varre recursivamente a pasta na nuvem via REST e faz o download de arquivo por arquivo manualmente (`Iterative Download`), preservando o contrato com o usuário, mas penalizando a performance.
