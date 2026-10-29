# Projeto Apolo

**Versão atual: 0.2.0 (piloto) | Windows 11 x64 | C++20/Win32**

O **Apolo** é um monitor e gerenciador conservador de memória desenvolvido de forma independente. Não incorpora código do Mem Reduct nem utiliza telemetria, rede, atualização remota, contorno de UAC ou APIs de limpeza de cache do kernel não documentadas.

## Funcionalidades

| Recurso | Estado |
| --- | --- |
| RAM usada, total, disponível e memória comprometida | Funcional na v0.1 |
| Atualização automática da leitura a cada 2,5 segundos | Funcional na v0.1 |
| Limpeza manual com confirmação | Funcional na v0.1 |
| Ícone na bandeja do Windows com menu | Novo na v0.2 |
| Preferências persistentes | Novo na v0.2 |
| Limpeza automática configurável | Novo na v0.2; desligada por padrão |
| Histórico CSV de cada operação | Novo na v0.2 |
| Modelo de política de TI (INI) | Novo na v0.2; apenas piloto |
| Compilação automática Windows x64 | Configurada via GitHub Actions |
| Diagnóstico CPU/disco e painel central | Planejado |

## Obter e atualizar o projeto

Primeira vez:

~~~powershell
git clone https://github.com/LucasEspindula/project-apolo.git
cd project-apolo
~~~

Atualizações seguintes, dentro da pasta clonada:

~~~powershell
git pull origin main
~~~

Se o projeto anterior estiver em uma pasta extraída de ZIP, faça o clone em uma pasta nova para usar o Git.

## Compilar no Windows 11

Requer Visual Studio com a carga **Desenvolvimento para desktop com C++**, Windows SDK e CMake.

- Abra a pasta do repositório no Visual Studio.
- Selecione x64-Release; o CMakeSettings.json já inclui x64-Debug e x64-Release.
- Clique em **Compilação > Compilar Tudo**.
- Normalmente o executável estará em out\build\x64-Release\Apolo.exe.

Alternativamente, abra o Developer PowerShell do Visual Studio na pasta do projeto:

~~~powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\Apolo.exe
~~~

**Feche a versão antiga do Apolo antes de abrir a v0.2.** O Windows pode manter a v0.1 aberta enquanto você inicia outra compilação.

O repositório também possui um workflow na aba Actions, que tenta compilar usando Visual Studio/MSVC em Windows x64. O resultado da compilação deve ser verificado nessa aba; build bem-sucedida não substitui os testes funcionais.

## Interface, opções e histórico

- **Fechar a janela** deixa o Apolo na bandeja, quando o ícone estiver ativo. Para encerrar, clique com o botão direito no ícone e escolha **Sair do Apolo**.
- **Iniciar na bandeja** apenas abre a interface oculta. Não configura inicialização automática do Windows.
- **Limpeza automática** é desligada por padrão. Para ligá-la, é preciso marcar a opção, clicar **Salvar** e aceitar a advertência. Só atua se o uso percentual de RAM atingir o limiar e houver transcorrido o intervalo mínimo desde a última tentativa ou mudança de configuração.
- Limiares: 70, 75, 80, 85, 90 e 95%. Intervalos: 15, 30, 60, 120 e 240 minutos.
- O histórico registra data/hora, origem manual/automática, processos tratados e variação observada de memória disponível. **Não** registra nomes de processos/usuários e não tem remoção automática.

Arquivos:

| Arquivo | Local |
| --- | --- |
| Preferências locais | %LOCALAPPDATA%\Apolo\settings.ini |
| Histórico CSV | %LOCALAPPDATA%\Apolo\history.csv |
| Política corporativa opcional | C:\ProgramData\Apolo\policy.ini |

## Instalação corporativa piloto

Em deployment/policy.ini está um **modelo**, com automação desligada. Se instalado em ProgramData\Apolo\policy.ini, os valores prevalecem sobre as preferências locais e os controles são bloqueados. Reinicie o Apolo após modificar a política.

**Atenção:** proteja a pasta e o arquivo com ACLs NTFS adequadas (usuários: leitura; administradores: escrita). O código e o script não definem essas permissões. A configuração não deve ser considerada uma barreira de segurança sem revisar as ACLs.

Para copiar os arquivos em uma máquina piloto, compile Release, encerre o Apolo e execute em PowerShell elevado:

~~~powershell
.\deployment\Install-Apolo.ps1 -ExecutablePath "C:\CAMINHO\project-apolo\out\build\x64-Release\Apolo.exe"
~~~

O script copia o executável para Program Files\Apolo e o modelo de política para ProgramData\Apolo. Ele não instala serviço, não cria tarefas ou inicialização automática. Preserva a política existente, a menos que você utilize -ReplacePolicy.

## Limites e segurança

- A limpeza usa a API documentada EmptyWorkingSet em processos acessíveis da mesma conta e sessão, excluindo processos principais do Windows e o Apolo.
- Reduzir o working set **não garante aceleração** e pode aumentar paginação, acessos ao disco e travamentos temporários. RAM liberada não significa memória comprometida reduzida.
- A automação baseada apenas na porcentagem de RAM é experimental; recomendamos mantê-la desligada até realizar testes de desempenho e responsividade.
- A limpeza ocorre na thread de interface; a janela pode ficar momentaneamente sem responder.
- As operações não usam privilégio administrativo de forma silenciosa.
- Para uso em 11–30 computadores, ainda são necessários testes, revisão de segurança, ACLs, antivírus, assinatura digital e política de distribuição.

## Checklist de validação v0.2 no Windows

1. Compilar Release x64, iniciar o programa e comparar as métricas de RAM com o Gerenciador de Tarefas.
2. Testar Atualizar e Limpar memória; confirmar que surge uma linha no CSV ao clicar em Ver histórico.
3. Fechar a janela, recuperar o Apolo pela bandeja e terminar pelo menu Sair.
4. Modificar configurações, clicar Salvar e conferir persistência após reiniciar.
5. Confirmar que a limpeza automática permanece desligada por padrão; só testar em máquina piloto autorizada.
6. Testar política com ACLs verificadas, confirmando que ela prevalece e bloqueia a edição.
7. Consultar o resultado da compilação na aba Actions do GitHub.

## Histórico

- **v0.1.0:** monitoramento e limpeza manual. Compilada e testada pelo proprietário no Windows 11.
- **v0.2.0 (piloto):** bandeja, configurações, histórico, automação opcional, política de TI e build no GitHub Actions. **Ainda depende de testes no Windows.**

## Roadmap

1. Estabilizar a v0.2 e melhorar a interface.
2. Monitoramento de CPU, disco e pressão real de memória.
3. Diagnósticos fundamentados e recomendações mensuráveis.
4. Instalação, assinatura e distribuição corporativa, futuramente painel central.

A licença formal para distribuição pública ainda será definida.
