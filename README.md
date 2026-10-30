# Projeto Apolo

**Versão atual: 0.3.0 (piloto) | Windows 11 x64 | C++20/Win32**

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
| Compilação Windows x64 | Manual no Visual Studio do desenvolvedor |
| CPU total (amostragem de tempo ocioso) | Novo na v0.3 |
| Taxas de leitura e gravação em disco (MiB/s) | Novo na v0.3 |
| Três processos da conta atual com maior working set | Novo na v0.3 |
| Sinais simples de atenção (CPU alta/RAM disponível baixa) | Novo na v0.3, **não é diagnóstico completo** |
| Diagnóstico avançado e painel central | Planejado |

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

**Feche a versão antiga do Apolo antes de abrir a v0.3.** O Windows pode manter a v0.1 aberta enquanto você inicia outra compilação.

**Fluxo adotado:** o GitHub armazena somente código, histórico e documentação. Não executamos build automática no GitHub Actions; o desenvolvedor faz `git pull`, compila localmente no Visual Studio e valida a execução.

## Painel de desempenho da v0.3

**CPU:** percentual de utilização total calculado pela diferença entre duas amostras de GetSystemTimes; a primeira amostra pode exibir "aguardando". Trata-se da utilização global aproximada no intervalo, não de frequência, temperatura ou gargalo confirmado.

**Disco:** taxas agregadas de leitura e gravação em **MiB/s**, coletadas com contadores documentados do Windows pela API PDH. Contadores com nomes em inglês são utilizados independentemente do idioma do Windows. São necessárias pelo menos duas amostras com intervalo. Se os contadores estiverem desabilitados ou indisponíveis, o Apolo mostra "indisponível" — **não inventa valores**. As taxas não representam o percentual de ocupação nem indicam, por si, lentidão do disco.

**Processos:** mostra até três processos acessíveis **da mesma conta e sessão**, ordenados por *working set* (memória física residente no processo), consultados aproximadamente a cada cinco segundos. Processos protegidos ou inacessíveis não são exibidos; assim, a lista não equivale ao ranking de todo o Windows nem à memória privada/comprometida. Os nomes consultados **não** são gravados no histórico ou enviados para a rede.

**Sinais de atenção:** CPU acima de 90% em uma amostra ou RAM física disponível inferior a 10%. São apenas indicadores instantâneos, sem promessa de desempenho ou ação automática associada.

Estas leituras são **somente leitura** e não alteram a rotina de limpeza nem habilitam a automação.

Referências de APIs documentadas: [GetSystemTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getsystemtimes), [Coleta de dados PDH](https://learn.microsoft.com/en-us/windows/win32/perfctrs/collecting-performance-data), [GetProcessMemoryInfo](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo).

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

## Checklist de validação v0.3 no Windows

1. Compilar Release x64, iniciar o programa e comparar as métricas de RAM com o Gerenciador de Tarefas.
1a. Aguardar pelo menos duas atualizações (aproximadamente 5 s) e comparar CPU com o Gerenciador de Tarefas, permitindo pequenas diferenças de amostragem.
1b. Testar leitura/gravação de arquivos e observar se a E/S do disco varia; quando PDH estiver indisponível, registrar isso sem tratar como falha fatal.
1c. Conferir se os processos exibidos são da conta atual e se os valores de working set são coerentes. A lista pode omitir processos protegidos.
2. Testar Atualizar e Limpar memória; confirmar que surge uma linha no CSV ao clicar em Ver histórico.
3. Fechar a janela, recuperar o Apolo pela bandeja e terminar pelo menu Sair.
4. Modificar configurações, clicar Salvar e conferir persistência após reiniciar.
5. Confirmar que a limpeza automática permanece desligada por padrão; só testar em máquina piloto autorizada.
6. Testar política com ACLs verificadas, confirmando que ela prevalece e bloqueia a edição.
7. Conferir a saída de compilação do Visual Studio e registrar os erros encontrados para correção no próximo commit.

## Histórico

- **v0.1.0:** monitoramento e limpeza manual. Compilada e testada pelo proprietário no Windows 11.
- **v0.2.0 (piloto):** bandeja, configurações, histórico, automação opcional, política de TI. Compilação realizada manualmente no Visual Studio.
- **v0.3.0 (piloto):** monitoramento de CPU, taxa de E/S do disco, principais processos da conta atual e sinais simples de atenção. **Validação funcional no Windows ainda pendente.**

## Roadmap

1. Estabilizar e testar a v0.3 em Windows 11, especialmente custo de monitoramento e compatibilidade de contadores.
2. Refinar interface e monitorar pressão real de memória (paginação, commit, falhas de página relevantes).
3. Construir diagnósticos com base em histórico e métricas mensuráveis.
4. Planejar instalação assinada e distribuição corporativa, depois painel central.

## Fluxo de desenvolvimento

A cada atualização, código e `README.md` seguem juntos em commit no GitHub. Na máquina Windows do desenvolvedor, atualizar com `git pull origin main` e compilar com o Visual Studio (`x64-Release`). **Não usamos GitHub Actions para compilar automaticamente.**

A licença formal para distribuição pública ainda será definida.
